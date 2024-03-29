/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include "vtkPlus3dConnexionTracker.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkPlusDataSource.h"
#include "vtkTransform.h"
#include "vtkXMLDataElement.h"
#include "vtkXMLUtilities.h"
#include "vtksys/SystemTools.hxx"
#include <deque>
#include <math.h>
#include <sstream>

#if ( _MSC_VER >= 1300 ) // Visual studio .NET
#pragma warning ( disable : 4311 )
#pragma warning ( disable : 4312 )
#  define vtkGetWindowLong GetWindowLongPtr
#  define vtkSetWindowLong SetWindowLongPtr
#  define vtkGWL_USERDATA GWLP_USERDATA
#  define GWL_USERDATA_TYPE LONG_PTR
#else // regular Visual studio 
#  define vtkGetWindowLong GetWindowLong
#  define vtkSetWindowLong SetWindowLong
#  define vtkGWL_USERDATA GWL_USERDATA
#  define GWL_USERDATA_TYPE LONG;
#endif // 


vtkStandardNewMacro(vtkPlus3dConnexionTracker);

//-------------------------------------------------------------------------
vtkPlus3dConnexionTracker::vtkPlus3dConnexionTracker()
  : Mutex(vtkSmartPointer<vtkIGSIORecursiveCriticalSection>::New())
  , SpaceNavigatorTool(NULL)
  , LatestMouseTransform(vtkMatrix4x4::New())
  , DeviceToTrackerTransform(vtkMatrix4x4::New())
  , OperatingMode(JOYSTICK_MODE)
  , CaptureWindowClassName("")
  , CaptureWindowHandle(0)
  , RegisteredRawInputDevices(NULL)
  , NumberOfRegisteredRawInputDevices(0)
{
  this->RequirePortNameInDeviceSetConfiguration = true;

  this->TranslationScales[0] = 0.001;
  this->TranslationScales[1] = 0.001;
  this->TranslationScales[2] = 0.001;
  this->RotationScales[0] = 0.001;
  this->RotationScales[1] = 0.001;
  this->RotationScales[2] = 0.001;

  // Although there is a callback function, it is not called regularly (when the joystick is moved
  // then it is called too frequently, other times it's not called at all), so we use the capture
  // thread to provide regular transform updates
  this->StartThreadForInternalUpdates = true;
  this->AcquisitionRate = 125;
}

//-------------------------------------------------------------------------
vtkPlus3dConnexionTracker::~vtkPlus3dConnexionTracker()
{
  if (this->Recording)
  {
    this->StopRecording();
  }
  DestroyCaptureWindow();
  if (this->DeviceToTrackerTransform != NULL)
  {
    this->DeviceToTrackerTransform->Delete();
    this->DeviceToTrackerTransform = NULL;
  }
  if (this->LatestMouseTransform != NULL)
  {
    this->LatestMouseTransform->Delete();
    this->LatestMouseTransform = NULL;
  }
}

//-------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::RegisterDevice()
{
  // Get a list of all attached raw input devices
  UINT nDevices = 0;
  if (GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)) != 0)
  {
    LOG_ERROR("No RawInput devices attached");
    return PLUS_FAIL;
  }
  PRAWINPUTDEVICELIST pRawInputDeviceList = NULL;
  if ((pRawInputDeviceList = (PRAWINPUTDEVICELIST)malloc(sizeof(RAWINPUTDEVICELIST) * nDevices)) == NULL)
  {
    LOG_ERROR("Error allocating memory for device list");
    return PLUS_FAIL;
  }

  // Now get the data on the attached devices
  if (GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST)) == -1)
  {
    LOG_ERROR("Error getting raw input device list");
    free(pRawInputDeviceList);
    return PLUS_FAIL;
  }

  this->RegisteredRawInputDevices = (PRAWINPUTDEVICE)malloc(nDevices * sizeof(RAWINPUTDEVICE));
  this->NumberOfRegisteredRawInputDevices = 0;

  // Look through device list for RIM_TYPEHID devices with UsagePage == 1, Usage == 8
  for (UINT i = 0; i < nDevices; i++)
  {
    if (pRawInputDeviceList[i].dwType == RIM_TYPEHID)
    {
      UINT nchars = 300;
      TCHAR deviceName[300];
      if (GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, deviceName, &nchars) >= 0)
      {
        LOG_DEBUG("Device[" << i << "]: handle=" << pRawInputDeviceList[i].hDevice << " name = " << deviceName);
      }

      RID_DEVICE_INFO dinfo;
      UINT sizeofdinfo = sizeof(dinfo);
      dinfo.cbSize = sizeofdinfo;
      if (GetRawInputDeviceInfo(pRawInputDeviceList[i].hDevice, RIDI_DEVICEINFO, &dinfo, &sizeofdinfo) < 0)
      {
        continue;
      }
      if (dinfo.dwType != RIM_TYPEHID)
      {
        continue;
      }

      RID_DEVICE_INFO_HID* phidInfo = &dinfo.hid;
      LOG_DEBUG("VID = " << phidInfo->dwVendorId);
      LOG_DEBUG("PID = " << phidInfo->dwProductId);
      LOG_DEBUG("Version = " << phidInfo->dwVersionNumber);
      LOG_DEBUG("UsagePage = " << phidInfo->usUsagePage);
      LOG_DEBUG("Usage = " << phidInfo->usUsage);

      // Add this one to the list of interesting devices?
      // Actually only have to do this once to get input from all usage 1, usagePage 8 devices
      // This just keeps out the other usages.
      // You might want to put up a list for users to select amongst the different devices.
      // In particular, to assign separate functionality to the different devices.
      if (phidInfo->usUsagePage == 1 && phidInfo->usUsage == 8)
      {
        this->RegisteredRawInputDevices[this->NumberOfRegisteredRawInputDevices].usUsagePage = phidInfo->usUsagePage;
        this->RegisteredRawInputDevices[this->NumberOfRegisteredRawInputDevices].usUsage     = phidInfo->usUsage;
        this->RegisteredRawInputDevices[this->NumberOfRegisteredRawInputDevices].dwFlags     = RIDEV_INPUTSINK;
        this->RegisteredRawInputDevices[this->NumberOfRegisteredRawInputDevices].hwndTarget  = this->CaptureWindowHandle;
        this->NumberOfRegisteredRawInputDevices++;
      }
    }
  }

  free(pRawInputDeviceList);

  // Register for input from the devices in the list
  if (RegisterRawInputDevices(this->RegisteredRawInputDevices, this->NumberOfRegisteredRawInputDevices, sizeof(RAWINPUTDEVICE)) == FALSE)
  {
    LOG_ERROR("Error calling RegisterRawInputDevices");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
void vtkPlus3dConnexionTracker::UnregisterDevice()
{
  if (this->RegisteredRawInputDevices)
  {
    for (UINT i = 0; i < this->NumberOfRegisteredRawInputDevices; i++)
    {
      this->RegisteredRawInputDevices[i].dwFlags = RIDEV_REMOVE;
      this->RegisteredRawInputDevices[i].hwndTarget = NULL;
    }
    if (RegisterRawInputDevices(this->RegisteredRawInputDevices, this->NumberOfRegisteredRawInputDevices, sizeof(RAWINPUTDEVICE)) == FALSE)
    {
      LOG_ERROR("Error unregistering input devices");
    }
    free(this->RegisteredRawInputDevices);
    this->RegisteredRawInputDevices = NULL;
    this->NumberOfRegisteredRawInputDevices = 0;
  }
}

//----------------------------------------------------------------------------
LONG FAR PASCAL vtkPlus3dConnexionTrackerWinProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  vtkPlus3dConnexionTracker* self = (vtkPlus3dConnexionTracker*)(vtkGetWindowLong(hwnd, vtkGWL_USERDATA));
  switch (message)
  {
    case WM_INPUT:
      self->ProcessDeviceInputEvent(lParam);
      break;
    case WM_DESTROY:
      LOG_TRACE("WM_DESTROY");
      self->OnCaptureWindowDestroy();
      break;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

//----------------------------------------------------------------------------
void vtkPlus3dConnexionTracker::ProcessDeviceInputEvent(LPARAM lParam)
{
  LOG_TRACE("WM_INPUT lParam=" << lParam);

  RAWINPUTHEADER header;
  UINT size = sizeof(header);
  if (GetRawInputData((HRAWINPUT)lParam, RID_HEADER, &header,  &size, sizeof(RAWINPUTHEADER)) == -1)
  {
    LOG_ERROR("Error from GetRawInputData(RID_HEADER)");
    return;
  }

  LOG_TRACE("rawEvent.header: hDevice = " << header.hDevice);

  // Set aside enough memory for the full event
  size = header.dwSize;
  LPRAWINPUT event = (LPRAWINPUT)malloc(size);
  if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, event, &size, sizeof(RAWINPUTHEADER)) == -1)
  {
    LOG_ERROR("Error from GetRawInputData(RID_INPUT)");
    return;
  }
  if (event->header.dwType != RIM_TYPEHID)
  {
    // not a human interface device message
    return;
  }

  static BOOL bGotTranslation = FALSE;
  static BOOL bGotRotation = FALSE;
  static int all6DOFs[6] = {0};
  LPRAWHID pRawHid = &event->data.hid;

#ifdef SHOW_DETAILS
  LOG_TRACE("rawInput count: " << pRawHid->dwCount << "   size: " << pRawHid->dwSizeHid);
  for (UINT i = 0; i < pRawHid->dwSizeHid; i++)
  {
    LOG_TRACE("value: " << pRawHid->bRawData[i]);
  }
#endif

  // Translation and rotation values come in separate messages
  if (pRawHid->bRawData[0] == 1)
  {
    // translation
    all6DOFs[0] = (pRawHid->bRawData[1] & 0x000000ff) | ((signed short)(pRawHid->bRawData[2] << 8) & 0xffffff00);
    all6DOFs[1] = (pRawHid->bRawData[3] & 0x000000ff) | ((signed short)(pRawHid->bRawData[4] << 8) & 0xffffff00);
    all6DOFs[2] = (pRawHid->bRawData[5] & 0x000000ff) | ((signed short)(pRawHid->bRawData[6] << 8) & 0xffffff00);
    bGotTranslation = TRUE;
  }
  else if (pRawHid->bRawData[0] == 2)
  {
    // rotation
    all6DOFs[3] = (pRawHid->bRawData[1] & 0x000000ff) | ((signed short)(pRawHid->bRawData[2] << 8) & 0xffffff00);
    all6DOFs[4] = (pRawHid->bRawData[3] & 0x000000ff) | ((signed short)(pRawHid->bRawData[4] << 8) & 0xffffff00);
    all6DOFs[5] = (pRawHid->bRawData[5] & 0x000000ff) | ((signed short)(pRawHid->bRawData[6] << 8) & 0xffffff00);
    bGotRotation = TRUE;
  }
  else if (pRawHid->bRawData[0] == 3)
  {
    // buttons
    LOG_TRACE("Button status: " << (unsigned char)pRawHid->bRawData[3] << ", " << (unsigned char)pRawHid->bRawData[2] << ", " << (unsigned char)pRawHid->bRawData[1]);
  }

  if (bGotTranslation && bGotRotation)
  {
    bGotTranslation = bGotRotation = FALSE;
    LOG_TRACE("all6DOFs: " << all6DOFs[0] << "," << all6DOFs[1] << "," << all6DOFs[2] << "," << all6DOFs[3] << "," << all6DOFs[4] << "," << all6DOFs[5]);
    vtkSmartPointer<vtkTransform> currentTransform = vtkSmartPointer<vtkTransform>::New();
    currentTransform->Identity();
    currentTransform->Translate(all6DOFs[0]*this->TranslationScales[0], all6DOFs[1]*this->TranslationScales[1], all6DOFs[2]*this->TranslationScales[2]);
    currentTransform->RotateX(all6DOFs[3]*this->RotationScales[0]);
    currentTransform->RotateY(all6DOFs[4]*this->RotationScales[1]);
    currentTransform->RotateZ(all6DOFs[5]*this->RotationScales[2]);

    {
      igsioLockGuard<vtkIGSIORecursiveCriticalSection> updateMutexGuardedLock(this->Mutex);
      currentTransform->GetMatrix(this->LatestMouseTransform);
    }
  }

}

//-------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::CreateCaptureWindow()
{
  // get necessary process info
  HINSTANCE hinstance = GetModuleHandle(NULL);

  const char CAPTURE_WINDOW_CLASS_NAME_PREFIX[] = "3dConnexionTracker capture window";

  if (this->CaptureWindowClassName.empty())
  {
    this->CaptureWindowClassName = CAPTURE_WINDOW_CLASS_NAME_PREFIX;
  }

  // set up a class for the main window
  WNDCLASS wc;
  wc.lpszClassName = this->CaptureWindowClassName.c_str();
  wc.hInstance = hinstance;
  wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&vtkPlus3dConnexionTrackerWinProc);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hIcon = NULL;
  wc.lpszMenuName = NULL;
  wc.hbrBackground = NULL;
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.cbClsExtra = sizeof(void*);
  wc.cbWndExtra = 0;

  bool registrationSuccessful = false;
  const int MAX_WINDOW_CLASS_REGISTRATION_ATTEMPTS = 32;
  for (int i = 1; i <= MAX_WINDOW_CLASS_REGISTRATION_ATTEMPTS; i++)
  {
    if (RegisterClass(&wc))
    {
      registrationSuccessful = true;
      break;
    }
    // try with a slightly different name at each registration attempt
    std::ostringstream windowName;
    windowName << CAPTURE_WINDOW_CLASS_NAME_PREFIX << " " << i << std::ends;
    this->CaptureWindowClassName = windowName.str();
    wc.lpszClassName = this->CaptureWindowClassName.c_str();
  }
  if (!registrationSuccessful)
  {
    LOG_ERROR("Initialize: failed to register 3dConnexionTracker capture window class (" << GetLastError() << ")");
    return PLUS_FAIL;
  }

  this->CaptureWindowHandle = CreateWindow(this->CaptureWindowClassName.c_str(), "Plus 3dConnexion capture window", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, hinstance, NULL);
  if (!this->CaptureWindowHandle)
  {
    LOG_ERROR("Initialize: failed to create window (" << GetLastError() << ")");
    return PLUS_FAIL;
  }

  // set the user data to 'this'
  vtkSetWindowLong(this->CaptureWindowHandle, vtkGWL_USERDATA, (GWL_USERDATA_TYPE)this);

  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
void vtkPlus3dConnexionTracker::OnCaptureWindowDestroy()
{
  Disconnect();
  this->CaptureWindowHandle = NULL;
}

//----------------------------------------------------------------------------
void vtkPlus3dConnexionTracker::DestroyCaptureWindow()
{
  if (this->CaptureWindowHandle)
  {
    DestroyWindow(this->CaptureWindowHandle);
    this->CaptureWindowHandle = 0;
  }

  if (!this->CaptureWindowClassName.empty())
  {
    // Window class name is valid
    UnregisterClass(this->CaptureWindowClassName.c_str(), GetModuleHandle(NULL));
    this->CaptureWindowClassName.clear();
  }

}

//-------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::InternalConnect()
{
  LOG_TRACE("vtkPlus3dConnexionTracker::Connect");

  if (CreateCaptureWindow() != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to create capture window");
    return PLUS_FAIL;
  }
  if (RegisterDevice() != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to register the 3dConnexion device");
    DestroyCaptureWindow();
    return PLUS_FAIL;
  }

  this->SpaceNavigatorTool = NULL;
  this->GetToolByPortName("SpaceNavigator", this->SpaceNavigatorTool);

  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::InternalDisconnect()
{
  LOG_TRACE("vtkPlus3dConnexionTracker::Disconnect");
  this->StopRecording();

  UnregisterDevice();
  DestroyCaptureWindow();

  this->SpaceNavigatorTool = NULL;
  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::Probe()
{
  LOG_TRACE("vtkPlus3dConnexionTracker::Probe");
  LOG_ERROR("vtkPlus3dConnexionTracker::Probe is not implemented");
  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::InternalStartRecording()
{
  LOG_TRACE("vtkPlus3dConnexionTracker::InternalStartRecording");
  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::InternalStopRecording()
{
  LOG_TRACE("vtkPlus3dConnexionTracker::InternalStopRecording");
  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::InternalUpdate()
{
  LOG_TRACE("vtkPlus3dConnexionTracker::InternalUpdate");

  const double unfilteredTimestamp = vtkIGSIOAccurateTimer::GetSystemTime();

  if (this->SpaceNavigatorTool != NULL)
  {
    {
      igsioLockGuard<vtkIGSIORecursiveCriticalSection> updateMutexGuardedLock(this->Mutex);
      switch (this->OperatingMode)
      {
        case MOUSE_MODE:
          this->DeviceToTrackerTransform->DeepCopy(this->LatestMouseTransform);
          break;
        case JOYSTICK_MODE:
        {
          vtkSmartPointer<vtkMatrix4x4> cumulatedTransform = vtkSmartPointer<vtkMatrix4x4>::New();
          vtkMatrix4x4::Multiply4x4(this->LatestMouseTransform, this->DeviceToTrackerTransform, cumulatedTransform);

          // Perform the translation in the tracker coordinate frame
          cumulatedTransform->Element[0][3] = this->DeviceToTrackerTransform->Element[0][3] + this->LatestMouseTransform->Element[0][3];
          cumulatedTransform->Element[1][3] = this->DeviceToTrackerTransform->Element[1][3] + this->LatestMouseTransform->Element[1][3];
          cumulatedTransform->Element[2][3] = this->DeviceToTrackerTransform->Element[2][3] + this->LatestMouseTransform->Element[2][3];

          this->DeviceToTrackerTransform->DeepCopy(cumulatedTransform);
        }
        break;
        default:
          LOG_ERROR("Operating mode is invalid: " << this->OperatingMode);
      }
    }

    // This device has no frame numbering, so just auto increment tool frame number
    unsigned long frameNumber = this->SpaceNavigatorTool->GetFrameNumber() + 1 ;
    ToolTimeStampedUpdate(this->SpaceNavigatorTool->GetId(), this->DeviceToTrackerTransform, TOOL_OK, frameNumber, unfilteredTimestamp);
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  if (deviceConfig->GetAttribute("Mode"))
  {
    if (STRCASECMP(deviceConfig->GetAttribute("Mode"), "MOUSE") == 0)
    {
      this->OperatingMode = MOUSE_MODE;
    }
    else if (STRCASECMP(deviceConfig->GetAttribute("Mode"), "JOYSTICK") == 0)
    {
      this->OperatingMode = JOYSTICK_MODE;
    }
    else
    {
      LOG_ERROR("Unknown Mode: " << deviceConfig->GetAttribute("Mode") << ". Valid options: MOUSE, JOYSTICK.");
    }
  }

  double translationScales[3] = {0.001, 0.001, 0.001};
  int translationScalesComponentRead = deviceConfig->GetVectorAttribute("TranslationScales", 3, translationScales);
  if (translationScalesComponentRead == 1)
  {
    this->TranslationScales[0] = translationScales[0];
    this->TranslationScales[1] = translationScales[0];
    this->TranslationScales[2] = translationScales[0];
  }
  else if (translationScalesComponentRead == 3)
  {
    this->TranslationScales[0] = translationScales[0];
    this->TranslationScales[1] = translationScales[1];
    this->TranslationScales[2] = translationScales[2];
  }
  else if (translationScalesComponentRead > 0)
  {
    LOG_ERROR("TranslationScales vector has " << translationScalesComponentRead << " components. Valid number of components: 1 or 3");
  }

  double rotationScales[3] = {0.001, 0.001, 0.001};
  int rotationScalesComponentRead = deviceConfig->GetVectorAttribute("RotationScales", 3, rotationScales);
  if (rotationScalesComponentRead == 1)
  {
    this->RotationScales[0] = rotationScales[0];
    this->RotationScales[1] = rotationScales[0];
    this->RotationScales[2] = rotationScales[0];
  }
  else if (rotationScalesComponentRead == 3)
  {
    this->RotationScales[0] = rotationScales[0];
    this->RotationScales[1] = rotationScales[1];
    this->RotationScales[2] = rotationScales[2];
  }
  else if (rotationScalesComponentRead > 0)
  {
    LOG_ERROR("RotationScales vector has " << rotationScalesComponentRead << " components. Valid number of components: 1 or 3");
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlus3dConnexionTracker::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(trackerConfig, rootConfigElement);

  switch (this->OperatingMode)
  {
    case MOUSE_MODE:
      trackerConfig->SetAttribute("Mode", "MOUSE");
      break;
    case JOYSTICK_MODE:
      trackerConfig->SetAttribute("Mode", "JOYSTICK");
      break;
    default:
      LOG_ERROR("Cannot write unknown operating mode into config file: " << this->OperatingMode);
  }

  trackerConfig->SetVectorAttribute("TranslationScales", 3, this->TranslationScales);
  trackerConfig->SetVectorAttribute("RotationScales", 3, this->RotationScales);

  return PLUS_SUCCESS;
}
