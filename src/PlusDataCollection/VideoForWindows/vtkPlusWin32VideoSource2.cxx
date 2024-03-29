/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

/*=========================================================================
The following copyright notice is applicable to parts of this file:
Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
All rights reserved.
See Copyright.txt or http://www.kitware.com/Copyright.htm for details.
Authors include: Danielle Pace
=========================================================================*/

#include "PixelCodec.h"
#include "PlusConfigure.h"
#include "vtkObjectFactory.h"
#include "vtkPlusChannel.h"
#include "vtkPlusDataSource.h"
#include "vtkUnsignedCharArray.h"
#include "vtkPlusWin32VideoSource2.h"

#include <ctype.h>

// because of warnings in windows header push and pop the warning level
#ifdef _MSC_VER
  #pragma warning (push, 3)
#endif

#include "vtkWindows.h"
#include <vfw.h>
#include <winuser.h>

#ifdef _MSC_VER
  #pragma warning (pop)
#endif

class vtkPlusWin32VideoSource2Internal
{
public:
  //----------------------------------------------------------------------------
  vtkPlusWin32VideoSource2Internal()
  {
    CapWnd = NULL;
    ParentWnd = NULL;
    BitMapInfoSize = 0;
    BitMapInfoPtr = NULL;
  }
  //----------------------------------------------------------------------------
  virtual ~vtkPlusWin32VideoSource2Internal()
  {
    delete [](char*)(BitMapInfoPtr);
    BitMapInfoPtr = NULL;
    BitMapInfoSize = 0;
  }
  //----------------------------------------------------------------------------
  PlusStatus GetBitmapInfoFromCaptureDevice()
  {
    if (CapWnd == NULL)
    {
      LOG_ERROR("Cannot get bitmap info, capture window has not been created yet");
      return PLUS_FAIL;
    }
    int formatSize = capGetVideoFormatSize(CapWnd);
    if (formatSize > this->BitMapInfoSize)
    {
      delete []((char*)BitMapInfoPtr);
      BitMapInfoPtr = (LPBITMAPINFO) new char[formatSize];
      BitMapInfoSize = formatSize;
    }
    if (!capGetVideoFormat(CapWnd, BitMapInfoPtr, formatSize))
    {
      LOG_ERROR("Cannot get bitmap info from capture window");
      return PLUS_FAIL;
    }
    return PLUS_SUCCESS;
  }
  //----------------------------------------------------------------------------
  PlusStatus SetBitmapInfoInCaptureDevice()
  {
    if (!capSetVideoFormat(CapWnd, BitMapInfoPtr, BitMapInfoSize))
    {
      LOG_ERROR("Cannot set bitmap video format for capture window");
      return PLUS_FAIL;
    }
    return PLUS_SUCCESS;
  }

  HWND CapWnd;
  HWND ParentWnd;
  CAPSTATUS CapStatus;
  CAPDRIVERCAPS CapDriverCaps;
  CAPTUREPARMS CaptureParms;
  LPBITMAPINFO BitMapInfoPtr;
  int BitMapInfoSize;
};

vtkStandardNewMacro(vtkPlusWin32VideoSource2);

#if ( _MSC_VER >= 1300 ) // Visual studio .NET
  #pragma warning ( disable : 4311 )
  #pragma warning ( disable : 4312 )
  #define vtkGetWindowLong GetWindowLongPtr
  #define vtkSetWindowLong SetWindowLongPtr
  #define vtkGWL_USERDATA GWLP_USERDATA
#else // regular Visual studio
  #define vtkGetWindowLong GetWindowLong
  #define vtkSetWindowLong SetWindowLong
  #define vtkGWL_USERDATA GWL_USERDATA
#endif //

//----------------------------------------------------------------------------
vtkPlusWin32VideoSource2::vtkPlusWin32VideoSource2()
  : Internal(new vtkPlusWin32VideoSource2Internal)
  , WndClassName(NULL)
  , Preview(0)
  , FrameIndex(0)
{
  this->RequireImageOrientationInConfiguration = true;

  // No need for StartThreadForInternalUpdates, as we are notified about each new frame through a callback function
}

//----------------------------------------------------------------------------
vtkPlusWin32VideoSource2::~vtkPlusWin32VideoSource2()
{
  this->vtkPlusWin32VideoSource2::ReleaseSystemResources();

  delete this->Internal;
  this->Internal = NULL;
}

//----------------------------------------------------------------------------
void vtkPlusWin32VideoSource2::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Preview: " << (this->Preview ? "On\n" : "Off\n");
}

//----------------------------------------------------------------------------
LONG FAR PASCAL vtkPlusWin32VideoSource2WinProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  vtkPlusWin32VideoSource2* self = (vtkPlusWin32VideoSource2*)(vtkGetWindowLong(hwnd, vtkGWL_USERDATA));
  switch (message)
  {
    case WM_MOVE:
      LOG_TRACE("WM_MOVE");
      break;
    case WM_SIZE:
      LOG_TRACE("WM_SIZE");
      break;
    case WM_DESTROY:
      LOG_TRACE("WM_DESTROY");
      self->OnParentWndDestroy();
      break;
    case WM_CLOSE:
      LOG_TRACE("WM_CLOSE");
      self->PreviewOff();
      return 0;
  }
  return DefWindowProc(hwnd, message, wParam, lParam);
}

//----------------------------------------------------------------------------
LRESULT PASCAL vtkPlusWin32VideoSource2CapControlProc(HWND hwndC, int nState)
{
  vtkPlusWin32VideoSource2* self = (vtkPlusWin32VideoSource2*)(capGetUserData(hwndC));
  if (nState == CONTROLCALLBACK_PREROLL)
  {
    LOG_TRACE("controlcallback preroll");
  }
  else if (nState == CONTROLCALLBACK_CAPTURING)
  {
    LOG_TRACE("controlcallback capturing");
  }
  return TRUE;
}

//----------------------------------------------------------------------------
LRESULT PASCAL vtkPlusWin32VideoSource2CallbackProc(HWND hwndC, LPVIDEOHDR lpVideoHeader)
{
  vtkPlusWin32VideoSource2* self = (vtkPlusWin32VideoSource2*)(capGetUserData(hwndC));
  self->AddFrameToBuffer(lpVideoHeader);
  return 0;
}

//----------------------------------------------------------------------------
// this callback is left in for debug purposes
LRESULT PASCAL vtkPlusWin32VideoSource2StatusCallbackProc(HWND vtkNotUsed(hwndC), int nID, LPCSTR vtkNotUsed(lpsz))
{
  //vtkPlusWin32VideoSource2 *self = (vtkPlusWin32VideoSource2 *)(capGetUserData(hwndC));
  if (nID == IDS_CAP_BEGIN)
  {
    LOG_TRACE("start of capture");
  }
  if (nID == IDS_CAP_END)
  {
    LOG_TRACE("end of capture");
  }
  return 1;
}

//----------------------------------------------------------------------------
LRESULT PASCAL vtkPlusWin32VideoSource2ErrorCallbackProc(HWND hwndC, int ErrID, LPSTR lpErrorText)
{
  if (ErrID)
  {
    LOG_ERROR("Video for Windows error: #" << ErrID);
  }
  return 1;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::InternalConnect()
{
  if (this->GetConnected())
  {
    // already connected
    return PLUS_SUCCESS;
  }

  // get necessary process info
  HINSTANCE hinstance = GetModuleHandle(NULL);

  // set up a class for the main window
  WNDCLASS wc;
  wc.lpszClassName = this->WndClassName;
  wc.hInstance = hinstance;
  wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&vtkPlusWin32VideoSource2WinProc);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hIcon = NULL;
  wc.lpszMenuName = NULL;
  wc.hbrBackground = NULL;
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.cbClsExtra = sizeof(void*);
  wc.cbWndExtra = 0;

  std::string windowNameStr;
  bool registrationSuccessful = false;
  const int MAX_WINDOW_CLASS_REGISTRATION_ATTEMPTS = 32;
  for (int i = 1; i <= MAX_WINDOW_CLASS_REGISTRATION_ATTEMPTS; i++)
  {
    if (RegisterClass(&wc))
    {
      SetWndClassName(wc.lpszClassName);
      registrationSuccessful = true;
      break;
    }
    // try with a slightly different name at each registration attempt
    std::ostringstream windowName;
    windowName << "VTKVideo " << i << std::ends;
    windowNameStr = windowName.str();
    wc.lpszClassName = windowNameStr.c_str();
  }
  if (!registrationSuccessful)
  {
    LOG_ERROR("Initialize: failed to register VTKVideo class (" << GetLastError() << ")");
    return PLUS_FAIL;
  }

  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

  if (this->Preview)
  {
    style |= WS_VISIBLE;
  }

  // set up the parent window, but don't show it
  vtkPlusDataSource* aSource(NULL);
  if (this->GetFirstVideoSource(aSource) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to retrieve the video source in the Win32Video device.");
    return PLUS_FAIL;
  }
  FrameSizeType frameSize = aSource->GetInputFrameSize();

  this->Internal->ParentWnd = CreateWindow(this->WndClassName, "Plus video capture window", style, 0, 0,
                              frameSize[0] + 2 * GetSystemMetrics(SM_CXFIXEDFRAME),
                              frameSize[1] + 2 * GetSystemMetrics(SM_CYFIXEDFRAME) + GetSystemMetrics(SM_CYBORDER) + GetSystemMetrics(SM_CYSIZE),
                              NULL, NULL, hinstance, NULL);

  if (!this->Internal->ParentWnd)
  {
    LOG_ERROR("Initialize: failed to create window (" << GetLastError() << ")");
    return PLUS_FAIL;
  }

  // set the user data to 'this'
  vtkSetWindowLong(this->Internal->ParentWnd, vtkGWL_USERDATA, (LONG_PTR)this);

  // Create the capture window
  this->Internal->CapWnd = capCreateCaptureWindow("Capture", WS_CHILD | WS_VISIBLE, 0, 0,
                           frameSize[0], frameSize[1], this->Internal->ParentWnd, 1);

  if (!this->Internal->CapWnd)
  {
    LOG_ERROR("Initialize: failed to create capture window (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  // connect to the driver
  if (!capDriverConnect(this->Internal->CapWnd, 0))
  {
    LOG_ERROR("Initialize: couldn't connect to driver (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  capDriverGetCaps(this->Internal->CapWnd, &this->Internal->CapDriverCaps, sizeof(CAPDRIVERCAPS));

  // set up the video capture format
  this->Internal->GetBitmapInfoFromCaptureDevice();
  //this->Internal->BitMapInfoPtr->bmiHeader.biWidth = frameSize[0];
  //this->Internal->BitMapInfoPtr->bmiHeader.biHeight = frameSize[1];
  if (this->Internal->SetBitmapInfoInCaptureDevice() != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to set requested frame size in the capture device");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  int width = this->Internal->BitMapInfoPtr->bmiHeader.biWidth;
  int height = this->Internal->BitMapInfoPtr->bmiHeader.biHeight;
  this->ReleaseSystemResources();
  this->Internal->ParentWnd = CreateWindow(this->WndClassName, "Plus video capture window", style, 0, 0,
                              width + 2 * GetSystemMetrics(SM_CXFIXEDFRAME),
                              height + 2 * GetSystemMetrics(SM_CYFIXEDFRAME) + GetSystemMetrics(SM_CYBORDER) + GetSystemMetrics(SM_CYSIZE),
                              NULL, NULL, hinstance, NULL);

  if (!this->Internal->ParentWnd)
  {
    LOG_ERROR("Initialize: failed to create window (" << GetLastError() << ")");
    return PLUS_FAIL;
  }

  // set the user data to 'this'
  vtkSetWindowLong(this->Internal->ParentWnd, vtkGWL_USERDATA, (LONG_PTR)this);

  // Create the capture window
  this->Internal->CapWnd = capCreateCaptureWindow("Capture", WS_CHILD | WS_VISIBLE, 0, 0,
                           frameSize[0], frameSize[1], this->Internal->ParentWnd, 1);

  if (!this->Internal->CapWnd)
  {
    LOG_ERROR("Initialize: failed to create capture window (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  // connect to the driver
  if (!capDriverConnect(this->Internal->CapWnd, 0))
  {
    LOG_ERROR("Initialize: couldn't connect to driver (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  capDriverGetCaps(this->Internal->CapWnd, &this->Internal->CapDriverCaps, sizeof(CAPDRIVERCAPS));

  // set the capture parameters
  capCaptureGetSetup(this->Internal->CapWnd, &this->Internal->CaptureParms, sizeof(CAPTUREPARMS));

  if (this->AcquisitionRate > 0)
  {
    this->Internal->CaptureParms.dwRequestMicroSecPerFrame = int(1000000 / this->AcquisitionRate);
  }
  else
  {
    this->Internal->CaptureParms.dwRequestMicroSecPerFrame = 0;
  }

  this->Internal->CaptureParms.fMakeUserHitOKToCapture = FALSE;
  this->Internal->CaptureParms.fYield = 1;
  this->Internal->CaptureParms.fCaptureAudio = FALSE;
  this->Internal->CaptureParms.vKeyAbort = 0x00;
  this->Internal->CaptureParms.fAbortLeftMouse = FALSE;
  this->Internal->CaptureParms.fAbortRightMouse = FALSE;
  this->Internal->CaptureParms.fLimitEnabled = FALSE;
  this->Internal->CaptureParms.wNumAudioRequested = 0;
  this->Internal->CaptureParms.wPercentDropForError = 100;
  this->Internal->CaptureParms.dwAudioBufferSize = 0;
  this->Internal->CaptureParms.AVStreamMaster = AVSTREAMMASTER_NONE;

  if (!capCaptureSetSetup(this->Internal->CapWnd, &this->Internal->CaptureParms, sizeof(CAPTUREPARMS)))
  {
    LOG_ERROR("Initialize: setup of capture parameters failed (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  // set user data for callbacks
  if (!capSetUserData(this->Internal->CapWnd, (LONG_PTR)this))
  {
    LOG_ERROR("Initialize: couldn't set user data for callback (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  // install the callback to precisely time beginning of grab
  if (!capSetCallbackOnCapControl(this->Internal->CapWnd, &vtkPlusWin32VideoSource2CapControlProc))
  {
    LOG_ERROR("Initialize: couldn't set control callback (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  // install the callback to copy frames into the buffer on sync grabs
  if (!capSetCallbackOnFrame(this->Internal->CapWnd, &vtkPlusWin32VideoSource2CallbackProc))
  {
    LOG_ERROR("Initialize: couldn't set frame callback (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }
  // install the callback to copy frames into the buffer on stream grabs
  if (!capSetCallbackOnVideoStream(this->Internal->CapWnd, &vtkPlusWin32VideoSource2CallbackProc))
  {
    LOG_ERROR("Initialize: couldn't set stream callback (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }
  // install the callback to get info on start/end of streaming
  if (!capSetCallbackOnStatus(this->Internal->CapWnd, &vtkPlusWin32VideoSource2StatusCallbackProc))
  {
    LOG_ERROR("Initialize: couldn't set status callback (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }
  // install the callback to send messages to user
  if (!capSetCallbackOnError(this->Internal->CapWnd, &vtkPlusWin32VideoSource2ErrorCallbackProc))
  {
    LOG_ERROR("Initialize: couldn't set error callback (" << GetLastError() << ")");
    this->ReleaseSystemResources();
    return PLUS_FAIL;
  }

  capOverlay(this->Internal->CapWnd, TRUE);

  // update framebuffer again to reflect any changes which
  // might have occurred
  this->UpdateFrameBuffer();

  return PLUS_SUCCESS;
}

PlusStatus vtkPlusWin32VideoSource2::InternalDisconnect()
{
  if (this->Internal->CapWnd)
  {
    LOG_DEBUG("capDriverDisconnect(this->Internal->CapWnd)");
    capDriverDisconnect(this->Internal->CapWnd);
    LOG_DEBUG("DestroyWindow(this->Internal->CapWnd)");
    DestroyWindow(this->Internal->CapWnd);
    this->Internal->CapWnd = NULL;
  }
  if (this->WndClassName != NULL && this->WndClassName[0] != '\0')
  {
    // window class name is valid
    UnregisterClass(this->WndClassName, GetModuleHandle(NULL));
    SetWndClassName("");
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
void vtkPlusWin32VideoSource2::SetPreview(int showPreview)
{
  if (this->Preview == showPreview)
  {
    return;
  }

  this->Preview = showPreview;

  if (GetConnected())
  {
    if (this->Internal->CapWnd == NULL || this->Internal->ParentWnd == NULL)
    {
      LOG_ERROR("Capture windows have not been intialized");
      return;
    }
    if (this->Preview)
    {
      ShowWindow(this->Internal->ParentWnd, SW_SHOWNORMAL);
    }
    else
    {
      ShowWindow(this->Internal->ParentWnd, SW_HIDE);
    }
  }
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkPlusWin32VideoSource2::ReleaseSystemResources()
{
  // destruction of ParentWnd causes OnParentWndDestroy to be called
  if (this->Internal->ParentWnd)
  {
    DestroyWindow(this->Internal->ParentWnd);
  }
}

//----------------------------------------------------------------------------
void vtkPlusWin32VideoSource2::OnParentWndDestroy()
{
  Disconnect();
  this->Internal->ParentWnd = NULL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::AddFrameToBuffer(void* lpVideoHeader)
{
  int inputCompression = this->Internal->BitMapInfoPtr->bmiHeader.biCompression;
  if (!PixelCodec::IsConvertToGraySupported(inputCompression))
  {
    LOG_ERROR("AddFrameToBuffer: video compression mode " << PixelCodec::GetCompressionModeAsString(inputCompression) << ": can't grab");
    return PLUS_FAIL;
  }

  LOG_TRACE("Grabbed");

  LPVIDEOHDR lpVHdr = static_cast<LPVIDEOHDR>(lpVideoHeader);

  // the VIDEOHDR has the following contents, for quick ref:
  //
  // lpData                 pointer to locked data buffer
  // dwBufferLength         Length of data buffer
  // dwBytesUsed            Bytes actually used
  // dwTimeCaptured         Milliseconds from start of stream
  // dwUser                 for client's use
  // dwFlags                assorted flags (see VFW.H)
  // dwReserved[4]          reserved for driver

  unsigned char* inputPixelsPtr = lpVHdr->lpData;

  FrameSizeType outputFrameSize;
  if (this->UncompressedVideoFrame.GetFrameSize(outputFrameSize) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to retrieve frame size.");
    return PLUS_FAIL;
  }

  if (PixelCodec::ConvertToGray(inputCompression, outputFrameSize[0], outputFrameSize[1], inputPixelsPtr, (unsigned char*)this->UncompressedVideoFrame.GetScalarPointer()) != PLUS_SUCCESS)
  {
    LOG_ERROR("Error while decoding the grabbed image");
    return PLUS_FAIL;
  }

  this->FrameIndex++;
  vtkPlusDataSource* aSource(NULL);
  if (this->GetFirstVideoSource(aSource) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to retrieve the video source in the Win32Video device.");
    return PLUS_FAIL;
  }
  double indexTime = aSource->GetStartTime() + 0.001 * lpVHdr->dwTimeCaptured;
  this->UncompressedVideoFrame.SetImageOrientation(aSource->GetInputImageOrientation());
  PlusStatus status = aSource->AddItem(&this->UncompressedVideoFrame, this->FrameIndex, indexTime, indexTime);

  this->Modified();
  return status;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::InternalUpdate()
{
  // just request the grab, the callback does the rest
  if (!capGrabFrameNoStop(this->Internal->CapWnd))
  {
    LOG_ERROR("Initialize: failed to request a single frame grab (" << GetLastError() << ")");
    return PLUS_FAIL;
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::InternalStartRecording()
{
  if (!capCaptureSequenceNoFile(this->Internal->CapWnd))
  {
    LOG_ERROR("Initialize: failed to request continuous frame grabbing (" << GetLastError() << ")");
    return PLUS_FAIL;
  }
  this->FrameIndex = 0;
  //double startTime = vtkIGSIOAccurateTimer::GetSystemTime();
  //this->Buffer->SetStartTime(startTime);
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::InternalStopRecording()
{
  if (!capCaptureStop(this->Internal->CapWnd))
  {
    LOG_ERROR("Initialize: failed to request continuous frame grabbing (" << GetLastError() << ")");
    return PLUS_FAIL;
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::VideoFormatDialog()
{
  if (!GetConnected())
  {
    LOG_ERROR("vtkPlusWin32VideoSource2::VideoFormatDialog failed, need to connect to the device first");
    return PLUS_FAIL;
  }

  //if (!this->Internal->CapDriverCaps.fHasDlgVideoFormat)
  //  {
  //  LOG_ERROR("vtkPlusWin32VideoSource2::VideoFormatDialog failed, the video device has no Format dialog.");
  //  return PLUS_FAIL;
  //  }

  capGetStatus(this->Internal->CapWnd, &this->Internal->CapStatus, sizeof(CAPSTATUS));
  if (this->Internal->CapStatus.fCapturingNow)
  {
    LOG_ERROR("vtkPlusWin32VideoSource2::VideoFormatDialog failed, can't alter video format while grabbing");
    return PLUS_FAIL;
  }

  int success = capDlgVideoFormat(this->Internal->CapWnd);
  if (!success)
  {
    LOG_ERROR("vtkPlusWin32VideoSource2::VideoFormatDialog failed (" << GetLastError() << ")");
    return PLUS_FAIL;
  }
  this->UpdateFrameBuffer();
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::VideoSourceDialog()
{
  if (!GetConnected())
  {
    LOG_ERROR("vtkPlusWin32VideoSource2::VideoSourceDialog failed, need to connect to the device first");
    return PLUS_FAIL;
  }

  //if (!this->Internal->CapDriverCaps.fHasDlgVideoSource)
  //  {
  //  LOG_ERROR("vtkPlusWin32VideoSource2::VideoFormatDialog failed, the video device has no Source dialog.");
  //  return PLUS_FAIL;
  //  }

  capGetStatus(this->Internal->CapWnd, &this->Internal->CapStatus, sizeof(CAPSTATUS));
  if (this->Internal->CapStatus.fCapturingNow)
  {
    LOG_ERROR("vtkPlusWin32VideoSource2::VideoFormatDialog failed, can't alter video source while grabbing");
    return PLUS_FAIL;
  }

  int success = capDlgVideoSource(this->Internal->CapWnd);
  if (!success)
  {
    LOG_ERROR("vtkPlusWin32VideoSource2::VideoSourceDialog failed (" << GetLastError() << ")");
    return PLUS_FAIL;
  }
  return this->UpdateFrameBuffer();
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::SetFrameSize(const FrameSizeType& frameSize)
{
  vtkPlusDataSource* aSource(NULL);
  if (this->GetFirstVideoSource(aSource) != PLUS_SUCCESS)
  {
    LOG_ERROR(this->GetDeviceId() << ": Unable to retrieve video source.");
    return PLUS_FAIL;
  }
  if (this->Superclass::SetInputFrameSize(*aSource, frameSize[0], frameSize[1], 1) != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }
  if (this->GetConnected())
  {
    // set up the video capture format
    this->Internal->GetBitmapInfoFromCaptureDevice();
    this->Internal->BitMapInfoPtr->bmiHeader.biWidth = frameSize[0];
    this->Internal->BitMapInfoPtr->bmiHeader.biHeight = frameSize[1];
    if (this->Internal->SetBitmapInfoInCaptureDevice() != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to set requested frame size in the capture device");
      return PLUS_FAIL;
    }
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::SetAcquisitionRate(double rate)
{
  if (rate == this->AcquisitionRate)
  {
    // no change
    return PLUS_SUCCESS;
  }

  this->AcquisitionRate = rate;

  if (GetConnected())
  {
    capCaptureGetSetup(this->Internal->CapWnd, &this->Internal->CaptureParms, sizeof(CAPTUREPARMS));
    if (this->AcquisitionRate > 0)
    {
      this->Internal->CaptureParms.dwRequestMicroSecPerFrame = int(1000000 / this->AcquisitionRate);
    }
    else
    {
      this->Internal->CaptureParms.dwRequestMicroSecPerFrame = 0;
    }
    capCaptureSetSetup(this->Internal->CapWnd, &this->Internal->CaptureParms, sizeof(CAPTUREPARMS));
  }

  this->Modified();
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::SetOutputFormat(int format)
{
  // convert color format to number of scalar components
  unsigned int numberOfScalarComponents = 0;
  switch (format)
  {
    case VTK_RGBA:
      numberOfScalarComponents = 4;
      break;
    case VTK_RGB:
      numberOfScalarComponents = 3;
      break;
    case VTK_LUMINANCE:
      numberOfScalarComponents = 1;
      break;
    default:
      numberOfScalarComponents = 0;
      LOG_ERROR("SetOutputFormat: Unrecognized color format.");
      return PLUS_FAIL;
  }

  if (numberOfScalarComponents != 1)
  {
    LOG_ERROR("Currently only 1 component image output is supported. Requested " << numberOfScalarComponents << " components");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* aSource(NULL);
  for (ChannelContainerIterator it = this->OutputChannels.begin(); it != this->OutputChannels.end(); ++it)
  {
    if ((*it)->GetVideoSource(aSource) != PLUS_SUCCESS)
    {
      LOG_ERROR("Unable to retrieve the video source in the win32video device on channel " << (*it)->GetChannelId());
      return PLUS_FAIL;
    }
    else
    {
      aSource->SetPixelType(VTK_UNSIGNED_CHAR);
    }
  }

  if (this->GetConnected())
  {
    // set up the video capture format
    this->Internal->GetBitmapInfoFromCaptureDevice();
    // TODO: update this->Internal->BitMapInfoPtr->bmiHeader
    if (this->Internal->SetBitmapInfoInCaptureDevice() != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to set requested frame size in the capture device");
      return PLUS_FAIL;
    }
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusWin32VideoSource2::UpdateFrameBuffer()
{
  // get the real video format
  this->Internal->GetBitmapInfoFromCaptureDevice();

  unsigned int width(this->Internal->BitMapInfoPtr->bmiHeader.biWidth);
  unsigned int height(this->Internal->BitMapInfoPtr->bmiHeader.biHeight);
  igsioCommon::VTKScalarPixelType pixelType(VTK_UNSIGNED_CHAR);   // always convert output to 8-bit grayscale
  unsigned int numberOfScalarComponents = 1;

  vtkPlusDataSource* aSource(NULL);
  if (this->GetFirstVideoSource(aSource) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to access video source in vtkPlusWin32VideoSource2. Critical failure.");
    return PLUS_FAIL;
  }
  aSource->SetInputFrameSize(width, height, 1);
  aSource->SetPixelType(pixelType);
  aSource->SetNumberOfScalarComponents(numberOfScalarComponents);

  FrameSizeType frameSize = {width, height, 1};
  this->UncompressedVideoFrame.AllocateFrame(frameSize, pixelType, numberOfScalarComponents);

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------

PlusStatus vtkPlusWin32VideoSource2::NotifyConfigured()
{
  if (this->OutputChannels.size() > 1)
  {
    LOG_WARNING("Win32VideoSource is expecting one output channel and there are " << this->OutputChannels.size() << " channels. First output channel will be used.");
    return PLUS_FAIL;
  }

  if (this->OutputChannels.size() == 0)
  {
    LOG_ERROR("No output channels defined for win32 video source. Cannot proceed.");
    this->CorrectlyConfigured = false;
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}