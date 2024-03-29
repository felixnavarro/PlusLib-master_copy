/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include <limits.h>
#include <float.h>
#include <math.h>
#include <ctype.h>

// NDI header files require this on Windows
#if defined(_WIN32) && !defined(__WINDOWS_H)
#define __WINDOWS_H
#include "Windows.h"
#endif

// requires NDIoapi/ndlib
#include "ndtypes.h"
#include "ndpack.h"
#include "ndopto.h"

// uses ndicapi for quaternion conversion
#include "ndicapi_math.h"

#include "vtkTimerLog.h"
#include "vtkMatrix4x4.h"
#include "vtkTransform.h"
#include "vtkPlusNDICertusTracker.h"
#include "vtkPlusDataSource.h"
#include "vtkObjectFactory.h"

vtkStandardNewMacro(vtkPlusNDICertusTracker);

// turn this on to print lots of debug information
#define VTK_CERTUS_DEBUG_STATEMENTS 0

//----------------------------------------------------------------------------
// map values 0, 1, 2 to the proper Certus VLED state constant
static VLEDState vtkNDICertusMapVLEDState[] = { VLEDST_OFF, VLEDST_ON, VLEDST_BLINK };

//----------------------------------------------------------------------------
vtkPlusNDICertusTracker::vtkPlusNDICertusTracker()
{
  this->Version = NULL;
  this->SendMatrix = vtkMatrix4x4::New();
  this->NumberOfMarkers = 0;
  this->NumberOfRigidBodies = 0;

  for (int i = 0; i < VTK_CERTUS_NTOOLS; i++)
  {
    this->PortHandle[i] = 0;
    this->PortEnabled[i] = 0;
  }

  this->RequirePortNameInDeviceSetConfiguration = true;

  // No callback function provided by the device, so the data capture thread will be used to poll the hardware and add new items to the buffer
  this->StartThreadForInternalUpdates = true;
  this->AcquisitionRate = 50;
}

//----------------------------------------------------------------------------
vtkPlusNDICertusTracker::~vtkPlusNDICertusTracker()
{
  if (this->Recording)
  {
    this->StopRecording();
  }
  this->SendMatrix->Delete();
  if (this->Version)
  {
    delete [] this->Version;
  }
}

//----------------------------------------------------------------------------
std::string vtkPlusNDICertusTracker::GetSdkVersion()
{
  const int BUFSIZE = 200;
  char version[BUFSIZE + 1];
  version[BUFSIZE] = 0;
  OAPIGetVersionString(version, BUFSIZE);
  return std::string(version);
}

//----------------------------------------------------------------------------
void vtkPlusNDICertusTracker::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);

  os << indent << "SendMatrix: " << this->SendMatrix << "\n";
  this->SendMatrix->PrintSelf(os, indent.GetNextIndent());
  os << indent << "NumberOfRigidBodies: " << this->NumberOfRigidBodies << "\n";
  os << indent << "NumberOfMarkers: " << this->NumberOfMarkers << "\n";
}

//----------------------------------------------------------------------------
static char vtkCertusErrorString[MAX_ERROR_STRING_LENGTH + 1];

#define vtkPrintCertusErrorMacro() \
{ \
  if (OptotrakGetErrorString(vtkCertusErrorString, MAX_ERROR_STRING_LENGTH+1) == 0) \
{ \
  LOG_ERROR(vtkCertusErrorString); \
} \
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::InternalConnect()
{
  // Set the NIF (Network Information File)
  if (OptotrakSetProcessingFlags(OPTO_USE_INTERNAL_NIF) != OPTO_NO_ERROR_CODE)
  {
    LOG_ERROR("Call to OptotrakSetProcessingFlags() failed.");
    vtkPrintCertusErrorMacro();
    return PLUS_FAIL;
  }

  LOG_DEBUG("TransputerDetermineSystemCfg start...");
  // Write to the internal NIF
  if (TransputerDetermineSystemCfg(NULL) != OPTO_NO_ERROR_CODE)
  {
    LOG_ERROR("Call to TransputerDetermineSystemCfg() failed.");
    vtkPrintCertusErrorMacro();
    return PLUS_FAIL;
  }
  LOG_DEBUG("TransputerDetermineSystemCfg completed");

  // Do the initial load.
  if (TransputerLoadSystem("system") != OPTO_NO_ERROR_CODE)
  {
    LOG_ERROR("Call to Certus TransputerLoadSystem() failed");
    vtkPrintCertusErrorMacro();
    return PLUS_FAIL;
  }

  // Wait for 1 second, according to the Certus documentation
  vtkIGSIOAccurateTimer::Delay(1);

  // Do the initialization
  if (TransputerInitializeSystem(0) != OPTO_NO_ERROR_CODE)
  {
    // optionally, use "OPTO_LOG_ERRORS_FLAG" argument to above
    LOG_ERROR("Call to Certus TransputerInitializeSystem() failed");
    vtkPrintCertusErrorMacro();
    return PLUS_FAIL;
  }

  // Load the standard camera parameters
  if (OptotrakLoadCameraParameters("standard") != OPTO_NO_ERROR_CODE)
  {
    LOG_ERROR("Call to OptotrakLoadCameraParameters()  failed");
    vtkPrintCertusErrorMacro();
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::InternalDisconnect()
{
  this->StopRecording();

  // Shut down the system
  this->ShutdownCertusSystem();

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::InitializeCertusSystem()
{
  // Connect to device
  if (this->Connect() != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  // Get the Optotrak status
  int nNumSensors;
  int nNumOdaus;
  int nFlags;
  if (OptotrakGetStatus(&nNumSensors, &nNumOdaus, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &nFlags) != OPTO_NO_ERROR_CODE)
  {
    LOG_ERROR("Call to OptotrakGetStatus() failed");
    vtkPrintCertusErrorMacro();
    return PLUS_FAIL;
  }

  // Make sure that the attached system is a Certus
  if ((nFlags & (OPTOTRAK_3020_FLAG | OPTOTRAK_CERTUS_FLAG)) != OPTOTRAK_CERTUS_FLAG)
  {
    LOG_ERROR("Only Certus is supported. Attached device is not Certus.");
    return PLUS_FAIL;
  }

  // Check to make sure configuration is what we expect
  if ((nNumSensors != 1 || nNumOdaus != 1))
  {
    LOG_DEBUG("Certus configuration: " << nNumSensors << " sensors, "
              << nNumOdaus << " odaus");
    //successFlag = 0;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::ShutdownCertusSystem()
{
  // Just a simple shutdown command
  if (TransputerShutdownSystem() != OPTO_NO_ERROR_CODE)
  {
    vtkPrintCertusErrorMacro();
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::ActivateCertusMarkers()
{
  // count the number of markers on all tools first
  if (OptotrakSetupCollection(
        this->NumberOfMarkers,  /* Number of markers in the collection. */
        (float)100.0,   /* Frequency to collect data frames at. */
        (float)2500.0,  /* Marker frequency for marker maximum on-time. */
        30,             /* Dynamic or Static Threshold value to use. */
        160,            /* Minimum gain code amplification to use. */
        0,              /* Stream mode for the data buffers. */
        (float)0.35,    /* Marker Duty Cycle to use. */
        (float)7.0,     /* Voltage to use when turning on markers. */
        (float)1.0,     /* Number of seconds of data to collect. */
        (float)0.0,     /* Number of seconds to pre-trigger data by. */
        OPTOTRAK_BUFFER_RAW_FLAG) != OPTO_NO_ERROR_CODE)
  {
    return PLUS_FAIL;
  }

  // Activate the markers
  if (OptotrakActivateMarkers() != OPTO_NO_ERROR_CODE)
  {
    LOG_ERROR("Cannot activate the markers");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::DeActivateCertusMarkers()
{
  if (OptotrakDeActivateMarkers() != OPTO_NO_ERROR_CODE)
  {
    LOG_ERROR("Cannot activate the markers");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::Probe()
{
  // If device is already tracking, return success.
  if (this->Recording)
  {
    return PLUS_SUCCESS;
  }

  // Perform initialization of the system
  PlusStatus status = this->InitializeCertusSystem();

  // Shut down the system
  this->ShutdownCertusSystem();

  return status;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::InternalStartRecording()
{
  if (this->Recording)
  {
    return PLUS_SUCCESS;
  }

  // Attempt to initialize the Certus
  // and enable all the tools
  if (!this->InitializeCertusSystem()
      || !this->EnableToolPorts())
  {
    vtkPrintCertusErrorMacro();
    this->ShutdownCertusSystem();
    return PLUS_FAIL;
  }

  // count the number of markers on all tools first
  if (!this->ActivateCertusMarkers())
  {
    vtkPrintCertusErrorMacro();
    this->ShutdownCertusSystem();
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::InternalStopRecording()
{
  if (OptotrakDeActivateMarkers() != OPTO_NO_ERROR_CODE)
  {
    vtkPrintCertusErrorMacro();
  }

  if (!this->DisableToolPorts())
  {
    vtkPrintCertusErrorMacro();
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusNDICertusTracker::InternalUpdate()
{
  int tool;
  int missing[VTK_CERTUS_NTOOLS];
  long statusFlags[VTK_CERTUS_NTOOLS];
  double transform[VTK_CERTUS_NTOOLS][8];

  if (!this->Recording)
  {
    LOG_ERROR("called Update() when Certus was not tracking");
    return PLUS_FAIL;
  }

  // initialize transformations to identity
  for (tool = 0; tool < VTK_CERTUS_NTOOLS; tool++)
  {
    missing[tool] = 1;

    transform[tool][0] = 1.0;
    transform[tool][1] = transform[tool][2] = transform[tool][3] = 0.0;
    transform[tool][4] = transform[tool][5] = transform[tool][6] = 0.0;
    transform[tool][7] = 0.0;
  }

  unsigned int uFrameNumber = 0;
  unsigned int uElements = 0;
  unsigned int uFlags = 0;
  OptotrakRigidStruct* rigidBodyData;
  rigidBodyData = new OptotrakRigidStruct[this->NumberOfRigidBodies];

  if (DataGetLatestTransforms2(&uFrameNumber, &uElements, &uFlags,
                               rigidBodyData, 0) != OPTO_NO_ERROR_CODE)
  {
    vtkPrintCertusErrorMacro();
    delete [] rigidBodyData;
    return PLUS_FAIL;
  }

  LOG_TRACE("Found " << uElements << " rigid bodies, expected " << this->NumberOfRigidBodies
            << " with " << this->NumberOfMarkers << " markers");

  for (int rigidCounter = 0; rigidCounter < this->NumberOfRigidBodies;
       rigidCounter++)
  {
    OptotrakRigidStruct& rigidBody = rigidBodyData[rigidCounter];
    long rigidId = rigidBody.RigidId;

    std::map<int, int>::iterator rigidBodyMapIterator = this->RigidBodyMap.find(rigidId);
    if (rigidBodyMapIterator != this->RigidBodyMap.end())
    {
      tool = rigidBodyMapIterator->second;
    }
    else
    {
      LOG_ERROR("InternalUpdate: bad rigid body ID " << rigidId);
      continue;
    }

    if ((rigidBody.flags & OPTOTRAK_UNDETERMINED_FLAG) == 0)
    {
      // this is done to keep the code similar to POLARIS
      double* trans = transform[tool];
      trans[0] = rigidBody.transformation.quaternion.rotation.q0;
      trans[1] = rigidBody.transformation.quaternion.rotation.qx;
      trans[2] = rigidBody.transformation.quaternion.rotation.qy;
      trans[3] = rigidBody.transformation.quaternion.rotation.qz;
      trans[4] = rigidBody.transformation.quaternion.translation.x;
      trans[5] = rigidBody.transformation.quaternion.translation.y;
      trans[6] = rigidBody.transformation.quaternion.translation.z;
      trans[7] = rigidBody.QuaternionError;
      LOG_TRACE("Rigid body " << rigidCounter << " (rigidId=" << rigidId << ") translation: " << trans[4] << ", " << trans[5] << ", " << trans[6]);
    }
    else
    {
      LOG_TRACE("Rigid body " << rigidCounter << " (rigidId=" << rigidId << ") undetermined");
    }

    statusFlags[tool] = rigidBody.flags;
  }

  delete [] rigidBodyData;

  const double unfilteredTimestamp = vtkIGSIOAccurateTimer::GetSystemTime();

  for (tool = 0; tool < VTK_CERTUS_NTOOLS; tool++)
  {
    if (this->PortEnabled[tool] == 0)
    {
      // The tools that are not connected and not in the configuration file were disabled when connecting
      continue;
    }

    // convert status flags from Optotrak format to vtkTracker format
    ToolStatus status = TOOL_OK;
    this->SetToolLED(tool, 0, TR_LED_ON);
    if ((statusFlags[tool] & OPTOTRAK_UNDETERMINED_FLAG) != 0)
    {
      status = TOOL_MISSING;
      this->SetToolLED(tool, 0, TR_LED_FLASH);
    }

    ndiTransformToMatrixd(transform[tool], *this->SendMatrix->Element);
    this->SendMatrix->Transpose();

    std::ostringstream toolPortName;
    toolPortName << tool;
    vtkPlusDataSource* trackerTool = NULL;
    if (this->GetToolByPortName(toolPortName.str().c_str(), trackerTool) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to get tool by port name: " << toolPortName.str());
    }
    else
    {
      // send the matrix and status to the tool's vtkPlusDataBuffer
      this->ToolTimeStampedUpdate(trackerTool->GetId(), this->SendMatrix, status, (unsigned long)uFrameNumber, unfilteredTimestamp);
    }
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
// Enable all tool ports that have tools plugged into them.
PlusStatus vtkPlusNDICertusTracker::EnableToolPorts()
{
  int toolCounter = 0;

  // reset our information about the tool ports
  for (toolCounter = 0; toolCounter < VTK_CERTUS_NTOOLS; toolCounter++)
  {
    if (this->PortEnabled[toolCounter])
    {
      LOG_DEBUG("disabling tool " << toolCounter);
      if (RigidBodyDelete(this->PortHandle[toolCounter]) != OPTO_NO_ERROR_CODE)
      {
        vtkPrintCertusErrorMacro();
      }
    }
    this->PortEnabled[toolCounter] = 0;
  }

  // stop tracking
  if (this->Recording)
  {
    LOG_DEBUG("DeActivating Markers");
    if (!this->DeActivateCertusMarkers())
    {
      vtkPrintCertusErrorMacro();
    }
  }

  // device handles (including status)
  int nDeviceHandles = 0;
  DeviceHandle* deviceHandles;

  int allDevicesEnabled = 0;
  for (int trialNumber = 0;
       trialNumber < 3 && !allDevicesEnabled;
       trialNumber++)
  {
    LOG_DEBUG("Getting Number Device Handles");
    if (OptotrakGetNumberDeviceHandles(&nDeviceHandles) != OPTO_NO_ERROR_CODE)
    {
      vtkPrintCertusErrorMacro();
      return PLUS_FAIL;
    }

    if (nDeviceHandles <= 0)
    {
      LOG_ERROR("EnableToolPorts: no Optotrack strobers found");
      return PLUS_FAIL;
    }

    // get all device handles and the status of each one
    deviceHandles = new DeviceHandle[nDeviceHandles];

    unsigned int flags = 0;
    LOG_DEBUG("Getting Device Handles for " << nDeviceHandles << " devices");
    if (OptotrakGetDeviceHandles(deviceHandles, nDeviceHandles, &flags)
        != OPTO_NO_ERROR_CODE)
    {
      vtkPrintCertusErrorMacro();
      delete [] deviceHandles;
      return PLUS_FAIL;
    }

    // initialize this to 1 (set to 0 if unenabled handles found)
    allDevicesEnabled = 1;

    // free any unoccupied handles, enable any initialized handles
    for (int deviceCounter = 0;
         deviceCounter < nDeviceHandles;
         deviceCounter++)
    {
      int ph = deviceHandles[deviceCounter].nID;
      DeviceHandleStatus status = deviceHandles[deviceCounter].dtStatus;

      if (status == DH_STATUS_UNOCCUPIED)
      {
        LOG_DEBUG("Delete port handle " << ph);
        if (OptotrakDeviceHandleFree(ph) != OPTO_NO_ERROR_CODE)
        {
          vtkPrintCertusErrorMacro();
        }
        allDevicesEnabled = 0;
      }
      else if (status == DH_STATUS_INITIALIZED)
      {
        LOG_DEBUG("Enable port handle " << ph);
        if (OptotrakDeviceHandleEnable(ph) != OPTO_NO_ERROR_CODE)
        {
          vtkPrintCertusErrorMacro();
        }
        // enabling a strober will make other tools appear,
        // so let's be paranoid and always set this to zero
        allDevicesEnabled = 0;
      }
    }
  }

  // reset the number of markers and rigid bodies to zero
  this->NumberOfMarkers = 0;
  this->NumberOfRigidBodies = 0;

  // get information for all tools
  for (int deviceCounter = 0;
       deviceCounter < nDeviceHandles;
       deviceCounter++)
  {
    int ph = deviceHandles[deviceCounter].nID;
    DeviceHandleStatus status = deviceHandles[deviceCounter].dtStatus;

    if (status == DH_STATUS_UNOCCUPIED)
    {
      // this shouldn't happen, but just in case
      continue;
    }

    DeviceHandleProperty* properties = 0;
    int nProperties = 0;
    LOG_DEBUG("Getting number of properties for port handle " << ph);
    if (OptotrakDeviceHandleGetNumberProperties(ph, &nProperties)
        != OPTO_NO_ERROR_CODE
        || nProperties == 0)
    {
      vtkPrintCertusErrorMacro();
    }
    else
    {
      properties = new DeviceHandleProperty[nProperties];
      LOG_DEBUG("Getting " << nProperties << " properties for handle " << ph);
      if (OptotrakDeviceHandleGetProperties(ph, properties, nProperties)
          != OPTO_NO_ERROR_CODE)
      {
        vtkPrintCertusErrorMacro();
      }
      else
      {
        // the properties of interest
        static const int deviceNameMaxlen = 128;
        char deviceName[deviceNameMaxlen + 1];
        int hasROM = 0;
        int nToolPorts = 0;
        int nSwitches = 0;
        int nVLEDs = 0;
        int nSubPort = 0;
        int nMarkersToFire = 0;
        int status = 0;

        for (int propCounter = 0; propCounter < nProperties; propCounter++)
        {
          unsigned int propertyID = properties[propCounter].uPropertyID;
          if (propertyID == DH_PROPERTY_NAME)
          {
            strncpy(deviceName, properties[propCounter].dtData.szData,
                    deviceNameMaxlen);
            deviceName[deviceNameMaxlen] = '\0';
          }
          else if (propertyID == DH_PROPERTY_HAS_ROM)
          {
            hasROM = properties[propCounter].dtData.nData;
          }
          else if (propertyID == DH_PROPERTY_TOOLPORTS)
          {
            nToolPorts = properties[propCounter].dtData.nData;
          }
          else if (propertyID == DH_PROPERTY_SWITCHES)
          {
            nSwitches = properties[propCounter].dtData.nData;
          }
          else if (propertyID == DH_PROPERTY_VLEDS)
          {
            nVLEDs = properties[propCounter].dtData.nData;
          }
          else if (propertyID == DH_PROPERTY_SUBPORT)
          {
            nSubPort = properties[propCounter].dtData.nData;
          }
          else if (propertyID == DH_PROPERTY_MARKERSTOFIRE)
          {
            nMarkersToFire = properties[propCounter].dtData.nData;
          }
          else if (propertyID == DH_PROPERTY_STATUS)
          {
            status = properties[propCounter].dtData.nData;
          }
        }

        // verify that this is a tool, and not a strober
        if (hasROM && nToolPorts == 0)
        {
          // assume only one strober: index tools by SubPort
          int port = nSubPort - 1;

          LOG_INFO("Found tool port " << port << " for device " << deviceName);

          if (port >= 0 && port < VTK_CERTUS_NTOOLS)
          {
            if (this->PortEnabled[port] &&
                this->PortHandle[port] != ph)
            {
              LOG_ERROR("Port number " << port << " is already "
                        "taken by a different tool");
            }
            else
            {
              this->PortHandle[port] = ph;
              this->PortEnabled[port] = (status == DH_STATUS_ENABLED);

              std::ostringstream toolPortName;
              toolPortName << port;
              vtkPlusDataSource* trackerTool = NULL;
              if (this->GetToolByPortName(toolPortName.str().c_str(), trackerTool) != PLUS_SUCCESS)
              {
                LOG_WARNING("Undefined connected tool found in the strober on port '" << toolPortName.str() << "' with name '" << deviceName << "', disabled it until not defined in the config file: " << vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationFileName().c_str());
                this->PortEnabled[port] = 0;
              }
            }

            this->NumberOfMarkers += nMarkersToFire;
          }

          delete [] properties;
        }
      }
    }
  }

  if (deviceHandles)
  {
    delete [] deviceHandles;
  }

  this->RigidBodyMap.clear();
  // add rigid bodies
  for (toolCounter = 0; toolCounter < VTK_CERTUS_NTOOLS; toolCounter++)
  {
    if (this->PortEnabled[toolCounter])
    {
      int ph = this->PortHandle[toolCounter];
      int rigidID = this->NumberOfRigidBodies;
      LOG_DEBUG("Adding rigid body for port handle" << ph);
      if (RigidBodyAddFromDeviceHandle(ph,
                                       rigidID, // rigID is port handle
                                       OPTOTRAK_QUATERN_RIGID_FLAG |
                                       OPTOTRAK_RETURN_QUATERN_FLAG)
          != OPTO_NO_ERROR_CODE)
      {
        vtkPrintCertusErrorMacro();
      }
      else
      {
        this->RigidBodyMap[rigidID] = toolCounter;
        // increment the number of rigid bodies
        this->NumberOfRigidBodies++;

        std::ostringstream toolPortName;
        toolPortName << toolCounter;
        vtkPlusDataSource* trackerTool = NULL;
        if (this->GetToolByPortName(toolPortName.str().c_str(), trackerTool) != PLUS_SUCCESS)
        {
          LOG_ERROR("Failed to get tool by port name: " << toolPortName.str());
          continue;
        }
      }
    }
  }

  // re-start the tracking
  if (this->Recording)
  {
    LOG_DEBUG("Activating Markers");
    if (!this->ActivateCertusMarkers())
    {
      vtkPrintCertusErrorMacro();
      return PLUS_FAIL;
    }
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
// Disable all enabled tool ports.
PlusStatus vtkPlusNDICertusTracker::DisableToolPorts()
{
  // stop tracking
  if (this->Recording)
  {
    if (!this->DeActivateCertusMarkers())
    {
      vtkPrintCertusErrorMacro();
    }
  }

  // disable the enabled ports
  for (int toolCounter = 0; toolCounter < VTK_CERTUS_NTOOLS; toolCounter++)
  {
    if (this->PortEnabled[toolCounter])
    {
      if (RigidBodyDelete(this->PortHandle[toolCounter]) != OPTO_NO_ERROR_CODE)
      {
        vtkPrintCertusErrorMacro();
      }
    }
    this->PortEnabled[toolCounter] = 0;
  }

  // re-start the tracking
  if (this->Recording)
  {
    if (!this->ActivateCertusMarkers())
    {
      vtkPrintCertusErrorMacro();
    }
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
int vtkPlusNDICertusTracker::GetToolFromHandle(int handle)
{
  int tool;

  for (tool = 0; tool < VTK_CERTUS_NTOOLS; tool++)
  {
    if (this->PortHandle[tool] == handle)
    {
      return tool;
    }
  }

  return -1;
}

//----------------------------------------------------------------------------
// change the state of an LED on the tool
PlusStatus vtkPlusNDICertusTracker::SetToolLED(int portNumber, int led, vtkPlusNDICertusTracker::LedState state)
{
  if (this->Recording &&
      portNumber >= 0 && portNumber < VTK_CERTUS_NTOOLS &&
      led >= 0 && led < 3)
  {
    VLEDState pstate = vtkNDICertusMapVLEDState[led];
    int ph = this->PortHandle[portNumber];
    if (ph == 0)
    {
      return PLUS_FAIL;
    }

    OptotrakDeviceHandleSetVisibleLED(ph, led + 1, pstate);
  }

  return PLUS_FAIL;
}

