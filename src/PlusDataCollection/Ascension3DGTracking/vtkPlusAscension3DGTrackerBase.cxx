/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "PlusConfigure.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkPlusDataSource.h"
#include "vtkTransform.h"
#include "vtkXMLDataElement.h"
#include "vtksys/SystemTools.hxx"
#include <sstream>

static const char QUALITY_PORT_NAME_1[] = "quality1";
static const char QUALITY_PORT_NAME_2[] = "quality2";
static const char QUALITY_PORT_NAME_3[] = "quality3";

static const char PROP_QUALITY_ERROR_SLOPE[] = "QualityErrorSlope";
static const char PROP_QUALITY_ERROR_OFFSET[] = "QualityErrorOffset";
static const char PROP_QUALITY_ERROR_SENSITIVITY[] = "QualityErrorSensitivity";
static const char PROP_QUALITY_FILTER_ALPHA[] = "QualityFilterAlpha";

vtkStandardNewMacro(vtkPlusAscension3DGTrackerBase);
typedef DOUBLE_POSITION_ANGLES_MATRIX_QUATERNION_TIME_Q_BUTTON_RECORD AscensionRecordType;

//-------------------------------------------------------------------------
vtkPlusAscension3DGTrackerBase::vtkPlusAscension3DGTrackerBase()
{
  this->AscensionRecordBuffer = NULL;

  this->TransmitterAttached = false;
  this->NumberOfSensors = 0;
  this->FilterAcWideNotch = 0;
  this->FilterAcNarrowNotch = 0;
  this->FilterDcAdaptive = 0.0;
  this->FilterLargeChange = 0;
  this->FilterAlpha = false;
  this->Hemisphere = FRONT;

  this->RequirePortNameInDeviceSetConfiguration = true;

  // No callback function provided by the device, so the data capture thread will be used to poll the hardware and add new items to the buffer
  this->StartThreadForInternalUpdates = true;
  this->AcquisitionRate = 50;
}

//-------------------------------------------------------------------------
vtkPlusAscension3DGTrackerBase::~vtkPlusAscension3DGTrackerBase()
{
  if (this->Recording)
  {
    this->StopRecording();
  }

  if (this->AscensionRecordBuffer != NULL)
  {
    delete this->AscensionRecordBuffer;
    this->AscensionRecordBuffer = NULL;
  }
}

//-------------------------------------------------------------------------
void vtkPlusAscension3DGTrackerBase::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//-------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::InternalConnect()
{
  LOG_TRACE("vtkAscension3DGTracker::Connect");

  if (this->Probe() != PLUS_SUCCESS)
  {
    LOG_ERROR("Connection probe failed");
    return PLUS_FAIL;
  }

  this->CheckReturnStatus(InitializeBIRDSystem());

  SYSTEM_CONFIGURATION systemConfig;

  if (this->CheckReturnStatus(GetBIRDSystemConfiguration(&systemConfig)) != PLUS_SUCCESS)
  {
    LOG_ERROR("Connection initialization failed");
    return PLUS_FAIL;
  }

  // Change to metric units.
  int metric = 1;
  if (this->CheckReturnStatus(SetSystemParameter(METRIC, &metric, sizeof(metric))) != PLUS_SUCCESS)
  {
    LOG_ERROR("Connection set to metric units failed");
    return PLUS_FAIL;
  }

  // Go through all tools.

  DATA_FORMAT_TYPE formatType = DOUBLE_POSITION_ANGLES_MATRIX_QUATERNION_TIME_Q_BUTTON;

  for (int sensorID = 0; sensorID < systemConfig.numberSensors; ++ sensorID)
  {
    // Set data format
    this->CheckReturnStatus(SetSensorParameter(sensorID, DATA_FORMAT, &formatType, sizeof(formatType)));

    // Set filtering
    this->CheckReturnStatus(SetSensorParameter(sensorID, FILTER_AC_WIDE_NOTCH, &this->FilterAcWideNotch, sizeof(int)));
    this->CheckReturnStatus(SetSensorParameter(sensorID, FILTER_AC_NARROW_NOTCH, &this->FilterAcNarrowNotch, sizeof(int)));
    this->CheckReturnStatus(SetSensorParameter(sensorID, FILTER_DC_ADAPTIVE, &this->FilterDcAdaptive, sizeof(double)));
    this->CheckReturnStatus(SetSensorParameter(sensorID, FILTER_LARGE_CHANGE, &this->FilterLargeChange, sizeof(int)));
    this->CheckReturnStatus(SetSensorParameter(sensorID, HEMISPHERE, &this->Hemisphere, sizeof(int)));

    tagADAPTIVE_PARAMETERS alphaStruct;
    alphaStruct.alphaMin[0] = alphaStruct.alphaMin[1] = alphaStruct.alphaMin[2] = alphaStruct.alphaMin[3]
                              = alphaStruct.alphaMin[4] = alphaStruct.alphaMin[5] = alphaStruct.alphaMin[6] = 655;
    alphaStruct.alphaMax[0] = alphaStruct.alphaMax[1] = alphaStruct.alphaMax[2] = alphaStruct.alphaMax[3]
                              = alphaStruct.alphaMax[4] = alphaStruct.alphaMax[5] = alphaStruct.alphaMax[6] = 29491;
    alphaStruct.vm[0] = 2;
    alphaStruct.vm[1] = alphaStruct.vm[2] = alphaStruct.vm[3]
                                            = alphaStruct.vm[4] = alphaStruct.vm[5] = alphaStruct.vm[6] = 4;
    alphaStruct.alphaOn = this->FilterAlpha;
    this->CheckReturnStatus(SetSensorParameter(sensorID, FILTER_ALPHA_PARAMETERS, &alphaStruct, sizeof(alphaStruct)));

    DEVICE_STATUS status = GetSensorStatus(sensorID);

    this->SensorSaturated.push_back((status & SATURATED) ? true : false);
    this->SensorAttached.push_back((status & NOT_ATTACHED) ? false : true);
    this->SensorInMotion.push_back((status & OUT_OF_MOTIONBOX) ? false : true);
    this->TransmitterAttached = ((status & NO_TRANSMITTER_ATTACHED) ? false : true);

    std::ostringstream portName;
    portName << sensorID;
    vtkPlusDataSource* tool = NULL;
    if (this->GetToolByPortName(portName.str().c_str(), tool) != PLUS_SUCCESS)
    {
      // No tool is defined for this sensor
      if (this->SensorAttached[ sensorID ])
      {
        LOG_WARNING("A sensor is attached to port '" << portName.str() << "', but no tool is defined for this port. The sensor is disabled it until not defined in the config file: " << vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationFileName());
        this->SensorAttached[ sensorID ] = false;
      }
      continue;
    }
    // Tool is defined for this sensor
    if (tool == NULL)
    {
      LOG_ERROR("Invalid tool");
      continue;
    }

    if (!this->SensorAttached[ sensorID ])
    {
      // A sensor is expected but not attached. A warning will be logged later.
      continue;
    }

    // Sensor is attached and tool is defined

    std::string slopeStr = tool->GetCustomProperty(PROP_QUALITY_ERROR_SLOPE);
    std::string offsetStr = tool->GetCustomProperty(PROP_QUALITY_ERROR_OFFSET);
    std::string sensitivityStr = tool->GetCustomProperty(PROP_QUALITY_ERROR_SENSITIVITY);
    std::string alphaStr = tool->GetCustomProperty(PROP_QUALITY_FILTER_ALPHA);
    if (!slopeStr.empty() || !offsetStr.empty() || !sensitivityStr.empty() || !alphaStr.empty())
    {
      // at least one sensitivity parameter is defined
      tagQUALITY_PARAMETERS qualityStruct;
      // The slope should have a value between �127 and +127. (Default is 0)
      // The offset should have a value between �127 and +127. (The default is 0)
      // The sensitivity should have a value between 0 and +127 (Default is 2)
      // The alpha should have a value between 0 and 127. (The default is 12)
      qualityStruct.error_slope = 0;
      qualityStruct.error_offset = 0;
      qualityStruct.error_sensitivity = 2;
      qualityStruct.filter_alpha = 12;
      if (!slopeStr.empty() && igsioCommon::StringToDouble(slopeStr.c_str(), qualityStruct.error_slope) != PLUS_SUCCESS)
      {
        LOG_ERROR("Failed to parse " << PROP_QUALITY_ERROR_SLOPE << " attribute in tool " << tool->GetPortName());
      }
      if (!offsetStr.empty() && igsioCommon::StringToDouble(offsetStr.c_str(), qualityStruct.error_offset) != PLUS_SUCCESS)
      {
        LOG_ERROR("Failed to parse " << PROP_QUALITY_ERROR_OFFSET << " attribute in tool " << tool->GetPortName());
      }
      if (!sensitivityStr.empty() && igsioCommon::StringToDouble(sensitivityStr.c_str(), qualityStruct.error_sensitivity) != PLUS_SUCCESS)
      {
        LOG_ERROR("Failed to parse " << PROP_QUALITY_ERROR_SENSITIVITY << " attribute in tool " << tool->GetPortName());
      }
      if (!alphaStr.empty() && igsioCommon::StringToDouble(alphaStr.c_str(), qualityStruct.filter_alpha) != PLUS_SUCCESS)
      {
        LOG_ERROR("Failed to parse " << PROP_QUALITY_FILTER_ALPHA << " attribute in tool " << tool->GetPortName());
      }
      this->CheckReturnStatus(SetSensorParameter(sensorID, QUALITY, &qualityStruct, sizeof(qualityStruct)));
    }

  }

  this->NumberOfSensors = systemConfig.numberSensors;

  if (this->AscensionRecordBuffer == NULL)
  {
    this->AscensionRecordBuffer = new AscensionRecordType[this->NumberOfSensors ];
  }

  // Check that all tools were connected that was defined in the configuration file
  for (DataSourceContainerConstIterator it = this->GetToolIteratorBegin(); it != this->GetToolIteratorEnd(); ++it)
  {
    if (IsQualityPortName(it->second->GetPortName()))
    {
      // Quality port is a virtual port, no real sensor is associated
      continue;
    }
    std::stringstream convert(it->second->GetPortName());
    int port(-1);
    if (!(convert >> port))
    {
      LOG_ERROR("Failed to convert tool '" << it->second->GetSourceId() << "' port name '" << it->second->GetPortName() << "' to integer, please check config file: " << vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationFileName());
      return PLUS_FAIL;
    }

    if (!this->SensorAttached[ port ])
    {
      LOG_ERROR("Sensor not attached for tool '" << it->second->GetSourceId() << "' on port name '" << it->second->GetPortName() << "', please check config file: " << vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationFileName());
    }
  }
  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::InternalDisconnect()
{
  LOG_TRACE("vtkAscension3DGTracker::Disconnect");
  return this->StopRecording();
}

//-------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::Probe()
{
  LOG_TRACE("vtkAscension3DGTracker::Probe");

  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::InternalStartRecording()
{
  LOG_TRACE("vtkAscension3DGTracker::InternalStartRecording");
  if (this->Recording)
  {
    return PLUS_SUCCESS;
  }

  // Turn on the first attached transmitter.

  BOARD_CONFIGURATION boardConfig;
  this->CheckReturnStatus(GetBoardConfiguration(0, &boardConfig));

  short selectID = TRANSMITTER_OFF;
  int i = 0;
  bool found = false;
  while ((i < boardConfig.numberTransmitters) && (found == false))
  {
    TRANSMITTER_CONFIGURATION transConfig;
    this->CheckReturnStatus(GetTransmitterConfiguration(i, &transConfig));
    if (transConfig.attached)
    {
      selectID = i;
      found = true;
    }
    ++ i;
  }

  if (this->CheckReturnStatus(SetSystemParameter(SELECT_TRANSMITTER, &selectID, sizeof(selectID))) != PLUS_SUCCESS)
  {
    LOG_ERROR("Select transmitter failed");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::InternalStopRecording()
{
  LOG_TRACE("vtkAscension3DGTracker::InternalStopRecording");

  short selectID = vtkPlusAscension3DGTrackerBase::TRANSMITTER_OFF;
  if (this->CheckReturnStatus(SetSystemParameter(SELECT_TRANSMITTER, &selectID, sizeof(selectID)))
      != PLUS_SUCCESS)
  {
    LOG_ERROR("Select transmitter failed");
    return PLUS_FAIL;
  }

  if (this->CheckReturnStatus(CloseBIRDSystem())
      != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to close BIRD system");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//-------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::InternalUpdate()
{
  LOG_TRACE("vtkAscension3DGTracker::InternalUpdate");

  if (! this->Recording)
  {
    LOG_ERROR("called Update() when not tracking");
    return PLUS_FAIL;
  }

  SYSTEM_CONFIGURATION sysConfig;
  if (this->CheckReturnStatus(GetBIRDSystemConfiguration(&sysConfig))
      != PLUS_SUCCESS)
  {
    LOG_ERROR("Cannot get system configuration");
    return PLUS_FAIL;
  }

  if (this->GetNumberOfSensors() != sysConfig.numberSensors)
  {
    LOG_ERROR("Changing sensors while tracking is not supported. Reconnect necessary.");
    this->StopRecording();
    this->Disconnect();
    return PLUS_FAIL;
  }

  AscensionRecordType* record = static_cast<AscensionRecordType*>(this->AscensionRecordBuffer);
  if (record == NULL)
  {
    LOG_ERROR("Ascension record buffer is invalid, reconnect necessary.");
    return PLUS_FAIL;
  }

#ifdef ATC_READ_ALL_SENSOR_AT_ONCE
  // Request data for all the sensors at once
  if (this->CheckReturnStatus(GetAsynchronousRecord(ALL_SENSORS, record, sysConfig.numberSensors * sizeof(AscensionRecordType))) != PLUS_SUCCESS)
  {
    LOG_ERROR("Cannot get synchronous record");
    return PLUS_FAIL;
  }
#else
  // Request data from each sensor one-by-one. This method works well in some cases (with 3DGm systems?) when the
  // ALL_SENSORS data request times out.
  // Scan the sensors and request a record if the sensor is physically attached
  for (int sensorIndex = 0; sensorIndex < sysConfig.numberSensors; sensorIndex++)
  {
    if (this->SensorAttached[sensorIndex])
    {
      // sensor attached so get record
      if (this->CheckReturnStatus(GetAsynchronousRecord(sensorIndex, record + sensorIndex, sizeof(*record))) != PLUS_SUCCESS)
      {
        LOG_ERROR("Cannot get synchronous record for sensor " << sensorIndex);
        return PLUS_FAIL;
      }
    }
  }
#endif

  // Set up reference matrix.

  bool saturated(false), attached(false), inMotionBox(false);
  bool transmitterRunning(false), transmitterAttached(false), globalError(false);

  ToolStatus toolStatus = TOOL_OK;
  const double unfilteredTimestamp = vtkIGSIOAccurateTimer::GetSystemTime();
  int numberOfErrors(0);
  // Vector to store the quality value for each tool
  std::vector<unsigned short> qualityValues(sysConfig.numberSensors, 0);
  for (unsigned short sensorIndex = 0; sensorIndex < sysConfig.numberSensors; ++ sensorIndex)
  {
    if (!this->SensorAttached[sensorIndex])
    {
      // Sensor disabled because it was not defined in the configuration file
      continue;
    }

    DEVICE_STATUS status = GetSensorStatus(sensorIndex);
    if (status == 0)
    {
      toolStatus = TOOL_OK;
    }
    else
    {
      toolStatus = TOOL_INVALID;
      if (status & NO_TRANSMITTER_ATTACHED)
      {
        LOG_WARNING("Attempting to produce data but no transmitter is attached");
      }
      if (status & NO_TRANSMITTER_RUNNING)
      {
        LOG_WARNING("Attempting to produce data but the transmitter is not running");
      }
      if (status & SATURATED)
      {
        LOG_WARNING("Signal is saturated for sensor index " << sensorIndex);
      }
      if (status & NOT_ATTACHED)
      {
        // Don't log a warning, it may happen frequently and we encode the information in the toolStatus
        toolStatus = TOOL_MISSING;
      }
      else if (status & OUT_OF_MOTIONBOX)
      {
        // Don't log a warning, it may happen frequently and we encode the information in the toolStatus
        toolStatus = TOOL_OUT_OF_VIEW;
      }
    }

    vtkSmartPointer< vtkMatrix4x4 > mToolToTracker = vtkSmartPointer< vtkMatrix4x4 >::New();
    mToolToTracker->Identity();
    for (int row = 0; row < 3; ++ row)
    {
      for (int col = 0; col < 3; ++ col)
      {
        mToolToTracker->SetElement(row, col, record[ sensorIndex ].s[ row ][ col ]);
      }
    }
    qualityValues[sensorIndex] = record[sensorIndex].quality;

    mToolToTracker->Invert();

    mToolToTracker->SetElement(0, 3, record[ sensorIndex ].x);
    mToolToTracker->SetElement(1, 3, record[ sensorIndex ].y);
    mToolToTracker->SetElement(2, 3, record[ sensorIndex ].z);

    std::ostringstream toolPortName;
    toolPortName << sensorIndex;

    vtkPlusDataSource* tool = NULL;
    if (this->GetToolByPortName(toolPortName.str().c_str(), tool) != PLUS_SUCCESS)
    {
      LOG_ERROR("Unable to find tool on port: " << toolPortName.str());
      numberOfErrors++;
      continue;
    }

    // Devices has no frame numbering, so just auto increment tool frame number
    unsigned long frameNumber = tool->GetFrameNumber() + 1 ;
    this->ToolTimeStampedUpdate(tool->GetSourceId(), mToolToTracker, toolStatus, frameNumber, unfilteredTimestamp);
  }

  vtkPlusAscension3DGTrackerBase::QualityToolTimeStampedUpdate(QUALITY_PORT_NAME_1, 0, qualityValues, unfilteredTimestamp);
  vtkPlusAscension3DGTrackerBase::QualityToolTimeStampedUpdate(QUALITY_PORT_NAME_2, 3, qualityValues, unfilteredTimestamp);
  vtkPlusAscension3DGTrackerBase::QualityToolTimeStampedUpdate(QUALITY_PORT_NAME_3, 6, qualityValues, unfilteredTimestamp);

  return (numberOfErrors > 0 ? PLUS_FAIL : PLUS_SUCCESS);
}

//-------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::CheckReturnStatus(int status)
{
  if (status != BIRD_ERROR_SUCCESS)
  {
    char buffer[ 512 ];
    GetErrorText(status, buffer, sizeof(buffer), SIMPLE_MESSAGE);
    LOG_ERROR(buffer);
    return PLUS_FAIL;
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, FilterAcWideNotch, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, FilterAcNarrowNotch, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, FilterDcAdaptive, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, FilterLargeChange, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, FilterAlpha, deviceConfig);
  XML_READ_ENUM_ATTRIBUTE_OPTIONAL(Hemisphere, deviceConfig, vtkPlusAscension3DGTrackerBase::GetHemisphereTypeAsString, 0, 6);

  XML_FIND_NESTED_ELEMENT_REQUIRED(dataSourcesElement, deviceConfig, "DataSources");

  for (int toolIndex = 0; toolIndex < dataSourcesElement->GetNumberOfNestedElements(); toolIndex++)
  {
    vtkXMLDataElement* toolDataElement = dataSourcesElement->GetNestedElement(toolIndex);
    if (STRCASECMP(toolDataElement->GetName(), "DataSource") != 0)
    {
      // if this is not a data source element, skip it
      continue;
    }

    if (toolDataElement->GetAttribute("Type") != NULL && STRCASECMP(toolDataElement->GetAttribute("Type"), "Tool") != 0)
    {
      // if this is not a Tool element, skip it
      continue;
    }

    const char* portName = toolDataElement->GetAttribute("PortName");
    if (portName == NULL)
    {
      LOG_ERROR("Cannot set sensor-specific parameters: tool portname is undefined");
      continue;
    }
    vtkPlusDataSource* tool = NULL;
    if (this->GetToolByPortName(portName, tool) != PLUS_SUCCESS)
    {
      LOG_ERROR("Cannot set sensor-specific parameters: tool " << portName << " was not found");
      continue;
    }
    if (tool == NULL)
    {
      LOG_ERROR("Cannot set sensor-specific parameters: tool " << portName << " was not found");
      continue;
    }

    const char* paramValue = toolDataElement->GetAttribute(PROP_QUALITY_ERROR_SLOPE);
    if (paramValue != NULL)
    {
      tool->SetCustomProperty(PROP_QUALITY_ERROR_SLOPE, paramValue);
    }

    paramValue = toolDataElement->GetAttribute(PROP_QUALITY_ERROR_OFFSET);
    if (paramValue != NULL)
    {
      tool->SetCustomProperty(PROP_QUALITY_ERROR_OFFSET, paramValue);
    }

    paramValue = toolDataElement->GetAttribute(PROP_QUALITY_ERROR_SENSITIVITY);
    if (paramValue != NULL)
    {
      tool->SetCustomProperty(PROP_QUALITY_ERROR_SENSITIVITY, paramValue);
    }

    paramValue = toolDataElement->GetAttribute(PROP_QUALITY_FILTER_ALPHA);
    if (paramValue != NULL)
    {
      tool->SetCustomProperty(PROP_QUALITY_FILTER_ALPHA, paramValue);
    }

  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(trackerConfig, rootConfigElement);

  trackerConfig->SetIntAttribute("FilterAcWideNotch", this->GetFilterAcWideNotch());
  trackerConfig->SetIntAttribute("FilterAcNarrowNotch", this->GetFilterAcNarrowNotch());
  trackerConfig->SetDoubleAttribute("FilterDcAdaptive", this->GetFilterDcAdaptive());
  trackerConfig->SetIntAttribute("FilterLargeChange", this->GetFilterLargeChange());
  trackerConfig->SetIntAttribute("FilterAlpha", (this->GetFilterAlpha() ? 1 : 0));
  trackerConfig->SetAttribute("Hemisphere", vtkPlusAscension3DGTrackerBase::GetHemisphereTypeAsString(this->GetHemisphere()).c_str());

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
bool vtkPlusAscension3DGTrackerBase::IsQualityPortName(const char* name)
{
  if (name == NULL)
  {
    return false;
  }

  return this->IsQualityPortName(std::string(name));
}

//----------------------------------------------------------------------------
bool vtkPlusAscension3DGTrackerBase::IsQualityPortName(const std::string& name)
{
  if (name == QUALITY_PORT_NAME_1 || name == QUALITY_PORT_NAME_2 || name == QUALITY_PORT_NAME_3)
  {
    return true;
  }
  return false;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusAscension3DGTrackerBase::QualityToolTimeStampedUpdate(const char* qualityToolPortName, unsigned int sensorStartIndex, const std::vector<unsigned short>& qualityValues, double unfilteredTimestamp)
{
  vtkPlusDataSource* qualityTool = NULL;
  if (this->GetToolByPortName(qualityToolPortName, qualityTool) != PLUS_SUCCESS)
  {
    // the tool is not defined, no need to store the quality values in them
    return PLUS_SUCCESS;
  }
  if (qualityTool == NULL)
  {
    LOG_ERROR("Quality tool port name " << qualityToolPortName << " is invalid");
    return PLUS_FAIL;
  }
  vtkSmartPointer< vtkMatrix4x4 > qualityStorageMatrix = vtkSmartPointer< vtkMatrix4x4 >::New();
  qualityStorageMatrix->SetElement(0, 3, -1);
  qualityStorageMatrix->SetElement(1, 3, -1);
  qualityStorageMatrix->SetElement(2, 3, -1);
  for (unsigned short valueIndex = 0; valueIndex < 3; ++valueIndex)
  {
    unsigned int sensorIndex = valueIndex + sensorStartIndex;
    double qualityValue = -1;
    if (sensorIndex < qualityValues.size())
    {
      qualityValue = qualityValues[sensorIndex];
    }
    qualityStorageMatrix->SetElement(valueIndex, 3, qualityValue);
  }

  // Devices has no frame numbering, so just auto increment tool frame number
  unsigned long frameNumber = qualityTool->GetFrameNumber() + 1 ;
  return this->ToolTimeStampedUpdate(qualityTool->GetSourceId(), qualityStorageMatrix, TOOL_OK, frameNumber, unfilteredTimestamp);
}

//----------------------------------------------------------------------------
std::string vtkPlusAscension3DGTrackerBase::GetHemisphereTypeAsString(int type)
{
  switch (type)
  {
  case FRONT: return "FRONT";
  case BACK: return "BACK";
  case TOP: return "TOP";
  case BOTTOM: return "BOTTOM";
  case LEFT: return "LEFT";
  case RIGHT: return "RIGHT";
  default:
    LOG_ERROR("Unknown hemisphere type: " << type);
    return "";
  }
}

//----------------------------------------------------------------------------
int vtkPlusAscension3DGTrackerBase::GetHemisphereTypeFromString(const char* typeStr)
{
  if (typeStr == NULL)
  {
    LOG_ERROR("Invalid hemisphere type string");
    return -1;
  }
  for (int i = 0; i < 6; i++)
  {
    if (igsioCommon::IsEqualInsensitive(vtkPlusAscension3DGTrackerBase::GetHemisphereTypeAsString(i), typeStr))
    {
      return i;
    }
  }
  LOG_ERROR("Unknown hemisphere type string: "<<typeStr);
  return -1;
}
