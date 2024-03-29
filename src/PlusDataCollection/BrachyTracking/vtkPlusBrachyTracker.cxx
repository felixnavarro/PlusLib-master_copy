/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include "PlusBrachyStepper.h"
#include "PlusCivcoBrachyStepper.h"
#include "PlusCmsBrachyStepper.h"
#include "igsioTrackedFrame.h"
#include "vtkIGSIOAccurateTimer.h"
#include "vtkPlusBrachyTracker.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkPlusChannel.h"
#include "vtkPlusDataSource.h"
#include "vtkIGSIOTrackedFrameList.h"
#include "vtkTransform.h"
#include "vtkXMLDataElement.h"
#include "vtksys/SystemTools.hxx"
#include <sstream>

vtkStandardNewMacro(vtkPlusBrachyTracker);

//----------------------------------------------------------------------------
vtkPlusBrachyTracker::vtkPlusBrachyTracker()
{
  this->Device = NULL;
  this->ModelVersion = NULL;
  this->ModelNumber = NULL;
  this->ModelSerialNumber = NULL;
  this->CalibrationAlgorithmVersion = NULL;
  this->CalibrationDate = NULL;

  this->SetSerialPort(1);
  this->BaudRate = 19200;

  this->SetToolReferenceFrameName("StepperHome");

  // Add tools to the tracker
  vtkSmartPointer<vtkPlusDataSource> probeTool = vtkSmartPointer<vtkPlusDataSource>::New();
  probeTool->SetId("Probe");
  std::ostringstream probePortName;
  probePortName << PROBEHOME_TO_PROBE_TRANSFORM;
  probeTool->SetPortName(probePortName.str().c_str());
  this->AddTool(probeTool);

  vtkSmartPointer<vtkPlusDataSource> templateTool = vtkSmartPointer<vtkPlusDataSource>::New();
  templateTool->SetId("Template");
  std::ostringstream templatePortName;
  templatePortName << TEMPLATEHOME_TO_TEMPLATE_TRANSFORM;
  templateTool->SetPortName(templatePortName.str().c_str());
  this->AddTool(templateTool);

  vtkSmartPointer<vtkPlusDataSource> encoderTool = vtkSmartPointer<vtkPlusDataSource>::New();
  encoderTool->SetId("StepperEncoderValues");
  std::ostringstream encoderPortName;
  encoderPortName << RAW_ENCODER_VALUES;
  encoderTool->SetPortName(encoderPortName.str().c_str());
  this->AddTool(encoderTool);

  this->BrachyStepperType = PlusBrachyStepper::BURDETTE_MEDICAL_SYSTEMS_DIGITAL_STEPPER;

  // Stepper calibration parameters
  this->CompensationEnabledOn();
  this->SetProbeTranslationAxisOrientation(0, 0, 1);
  this->SetTemplateTranslationAxisOrientation(0, 0, 1);
  this->SetProbeRotationAxisOrientation(0, 0, 1);
  this->SetProbeRotationEncoderScale(1.0);

  this->RequirePortNameInDeviceSetConfiguration = true;

  // No callback function provided by the device, so the data capture thread will be used to poll the hardware and add new items to the buffer
  this->StartThreadForInternalUpdates = true;
  this->AcquisitionRate = 20;
}

//----------------------------------------------------------------------------
vtkPlusBrachyTracker::~vtkPlusBrachyTracker()
{
  if (this->Recording)
  {
    this->StopRecording();
  }

  if (this->Device != NULL)
  {
    delete this->Device;
    this->Device = NULL;
  }

  this->SetModelVersion(NULL);
  this->SetModelNumber(NULL);
  this->SetModelSerialNumber(NULL);
  this->SetCalibrationAlgorithmVersion(NULL);
  this->SetCalibrationDate(NULL);

}

//----------------------------------------------------------------------------
void vtkPlusBrachyTracker::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::InternalConnect()
{
  if (this->Device == NULL)
  {
    LOG_ERROR("Failed to connect to brachy tracker - BrachyStepperType not selected, device is NULL!");
    return PLUS_FAIL;
  }

  return this->Device->Connect();
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::InternalDisconnect()
{
  this->Device->Disconnect();
  return this->StopRecording();
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::Probe()
{
  if (this->Recording)
  {
    return PLUS_SUCCESS;
  }

  if (!this->Connect())
  {
    LOG_ERROR("Unable to connect to stepper on port: " << this->GetSerialPort());
    return PLUS_FAIL;
  }

  this->Disconnect();

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::InternalStartRecording()
{
  if (this->IsRecording())
  {
    return PLUS_SUCCESS;
  }

  if (this->InitBrachyTracker() != PLUS_SUCCESS)
  {
    LOG_ERROR("Couldn't initialize brachy stepper.");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::InternalStopRecording()
{
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
std::string vtkPlusBrachyTracker::GetBrachyToolSourceId(BRACHY_STEPPER_TOOL tool)
{
  std::ostringstream toolPortName;
  toolPortName << tool;
  vtkPlusDataSource* trackerTool = NULL;
  if (this->GetToolByPortName(toolPortName.str(), trackerTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool source ID by port: " << toolPortName.str());
    return "";
  }
  std::string sourceId = trackerTool->GetId();
  return sourceId;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::InternalUpdate()
{
  ToolStatus status = TOOL_OK;

  if (!this->Recording)
  {
    LOG_ERROR("called Update() when Brachy stepper was not tracking");
    return PLUS_FAIL;
  }

  // get the transforms from stepper
  double dProbePosition(0), dTemplatePosition(0), dProbeRotation(0);
  unsigned long frameNum(0);
  if (!this->Device->GetEncoderValues(dProbePosition, dTemplatePosition, dProbeRotation, frameNum))
  {
    LOG_DEBUG("Tracker request timeout...");
    // Unable to get tracking information from tracker
    status = TOOL_REQ_TIMEOUT;
  }
  /*LOG_TRACE("Encoder values: "
    << "(Probe position) " << dProbePosition << ", "
    << "(Probe rotation) " << dProbeRotation << ", "
    << "(Template position) " << dTemplatePosition << ", "
    << "(Frame number) " << frameNum);*/

  const double unfilteredTimestamp = vtkIGSIOAccurateTimer::GetSystemTime();

  // Save probe position to the matrix (0,3) element
  // Save probe rotation to the matrix (1,3) element
  // Save grid position to the matrix (2,3) element
  vtkSmartPointer<vtkMatrix4x4> probePosition = vtkSmartPointer<vtkMatrix4x4>::New();
  probePosition->SetElement(ROW_PROBE_POSITION, 3, dProbePosition);
  probePosition->SetElement(ROW_PROBE_ROTATION, 3, dProbeRotation);
  probePosition->SetElement(ROW_TEMPLATE_POSITION, 3, dTemplatePosition);

  // Update encoder values tool
  /*LOG_TRACE("Calling ToolTimeStampedUpdate for RAW_ENCODER using: "
			<< "(Tool ID) " << GetBrachyToolSourceId(RAW_ENCODER_VALUES).c_str() <<", "
			<< "(ProbeHomeToProbe) " << probePosition->GetData() <<", "
			<< "(status) " << status <<", "
			<< "(frameNum) " << frameNum <<", "
			<< "(unfilteredTimestamp) " << unfilteredTimestamp);*/
  if (this->ToolTimeStampedUpdate(this->GetBrachyToolSourceId(RAW_ENCODER_VALUES).c_str(), probePosition, status, frameNum, unfilteredTimestamp) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to update tool: " << this->GetBrachyToolSourceId(RAW_ENCODER_VALUES));
    return PLUS_FAIL;
  }

  if (!this->CompensationEnabled)
  {
	LOG_TRACE("this->CompensationEnabled=TRUE");
    // Update template transform tool
    vtkSmartPointer<vtkTransform> tTemplateHomeToTemplate = vtkSmartPointer<vtkTransform>::New();
    tTemplateHomeToTemplate->Translate(0, 0, dTemplatePosition);
    if (this->ToolTimeStampedUpdate(this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM).c_str(), tTemplateHomeToTemplate->GetMatrix(), status, frameNum, unfilteredTimestamp) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to update tool: " << this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM));
      return PLUS_FAIL;
    }

    // Update probe transform tool
    vtkSmartPointer<vtkTransform> tProbeHomeToProbe = vtkSmartPointer<vtkTransform>::New();
    tProbeHomeToProbe->Translate(0, 0, dProbePosition);
    tProbeHomeToProbe->RotateZ(dProbeRotation);
    if (this->ToolTimeStampedUpdate(this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM).c_str(), tProbeHomeToProbe->GetMatrix(), status, frameNum, unfilteredTimestamp) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to update tool: " << this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM));
      return PLUS_FAIL;
    }
    return PLUS_SUCCESS;
  }
  //LOG_TRACE("If this->CompensationEnabled=TRUE was not printed then: FALSE");
  // Save template home to template transform
  vtkSmartPointer<vtkTransform> tTemplateHomeToTemplate = vtkSmartPointer<vtkTransform>::New();
  double templateTranslationAxisVector[3];
  this->GetTemplateTranslationAxisOrientation(templateTranslationAxisVector);
  vtkMath::MultiplyScalar(templateTranslationAxisVector, dTemplatePosition);
  tTemplateHomeToTemplate->Translate(templateTranslationAxisVector);
  // send the transformation matrix and status to the tool
  /*LOG_TRACE("Calling ToolTimeStampedUpdate for TEMPLATEHOME_TO_TEMPLATE_TRANSFORM using: "
			<< "(Tool ID) " << GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM).c_str() <<", "
			<< "(TemplateHome2Template) " << tTemplateHomeToTemplate->GetMatrix() <<", "
			<< "(status) " << status <<", "
			<< "(frameNum) " << frameNum <<", "
			<< "(unfilteredTimestamp) " << unfilteredTimestamp);*/
  /*LOG_TRACE("The matrix TemplateHome2Template is: " << endl 
            << tTemplateHomeToTemplate->GetMatrix()->GetElement(1,1) << " , " << tTemplateHomeToTemplate->GetMatrix()->GetElement(1,2) << " , " << tTemplateHomeToTemplate->GetMatrix()->GetElement(1,3) << endl
            << tTemplateHomeToTemplate->GetMatrix()->GetElement(2,1) << " , " << tTemplateHomeToTemplate->GetMatrix()->GetElement(2,2) << " , " << tTemplateHomeToTemplate->GetMatrix()->GetElement(2,3) << endl
            << tTemplateHomeToTemplate->GetMatrix()->GetElement(3,1) << " , " << tTemplateHomeToTemplate->GetMatrix()->GetElement(3,2) << " , " << tTemplateHomeToTemplate->GetMatrix()->GetElement(3,3) << endl);*/
  if (this->ToolTimeStampedUpdate(this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM).c_str(), tTemplateHomeToTemplate->GetMatrix(), status, frameNum, unfilteredTimestamp) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to update tool: " << this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM));
    return PLUS_FAIL;
  }

  // Save probehome to probe transform
  vtkSmartPointer<vtkTransform> tProbeHomeToProbe = vtkSmartPointer<vtkTransform>::New();
  // Translate the probe to the desired position
  double probeTranslationVector[3];
  this->GetProbeTranslationAxisOrientation(probeTranslationVector);
  vtkMath::MultiplyScalar(probeTranslationVector, dProbePosition);
  tProbeHomeToProbe->Translate(probeTranslationVector);

  // Translate the probe to the compensated rotation axis before the rotation
  double probeRotationVector[3];
  this->GetProbeRotationAxisOrientation(probeRotationVector); //This is not assigned to anything ¿?
  vtkMath::MultiplyScalar(probeRotationVector, dProbePosition);
  tProbeHomeToProbe->Translate(probeRotationVector);
  const double compensatedProbeRotation = this->ProbeRotationEncoderScale * dProbeRotation;
  tProbeHomeToProbe->RotateZ(compensatedProbeRotation);
  // Translate back the probe to the original position
  tProbeHomeToProbe->Translate(-probeRotationVector[0], -probeRotationVector[1], -probeRotationVector[2]);
  // send the transformation matrix and status to the tool
  /*LOG_TRACE("Calling ToolTimeStampedUpdate fro PROBEHOME_TO_PROBE_TRANSFORM using: "
			<< "(Tool ID) " << GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM).c_str() <<", "
			<< "(ProbeHomeToProbe) " << tProbeHomeToProbe->GetMatrix() <<", "
			<< "(status) " << status <<", "
			<< "(frameNum) " << frameNum <<", "
			<< "(unfilteredTimestamp) " << unfilteredTimestamp);*/
  /*LOG_TRACE("The matrix ProbeHomeToProbe is: " << endl 
            << tProbeHomeToProbe->GetMatrix()->GetElement(1,1) << " , " << tProbeHomeToProbe->GetMatrix()->GetElement(1,2) << " , " << tProbeHomeToProbe->GetMatrix()->GetElement(1,3) << endl
            << tProbeHomeToProbe->GetMatrix()->GetElement(2,1) << " , " << tProbeHomeToProbe->GetMatrix()->GetElement(2,2) << " , " << tProbeHomeToProbe->GetMatrix()->GetElement(2,3) << endl
            << tProbeHomeToProbe->GetMatrix()->GetElement(3,1) << " , " << tProbeHomeToProbe->GetMatrix()->GetElement(3,2) << " , " << tProbeHomeToProbe->GetMatrix()->GetElement(3,3) << endl);*/
  if (this->ToolTimeStampedUpdate(this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM).c_str(), tProbeHomeToProbe->GetMatrix(), status, frameNum, unfilteredTimestamp) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to update tool: " << this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM));
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::InitBrachyTracker()
{
  // Connect to device
  if (!this->Connect())
  {
    LOG_ERROR("Unable to connect to stepper on port: " << this->GetSerialPort());
    return PLUS_FAIL;
  }

  std::string version;
  std::string model;
  std::string serial;
  if (this->Device->GetDeviceModelInfo(version, model, serial) != PLUS_SUCCESS)
  {
    LOG_ERROR("Couldn't get version info from stepper.");
    return PLUS_FAIL;
  }

  this->SetModelVersion(version.c_str());
  this->SetModelNumber(model.c_str());
  this->SetModelSerialNumber(serial.c_str());

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(unsigned long, SerialPort, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(unsigned long, BaudRate, deviceConfig);

  if (!this->IsRecording())
  {
    const char* brachyStepperType = deviceConfig->GetAttribute("BrachyStepperType");
    if (brachyStepperType == NULL)
    {
      LOG_ERROR("Unable to find BrachyStepperType attribute in configuration file");
      return PLUS_FAIL;
    }

    // Delete device before we change it
    if (this->Device != NULL)
    {
      delete this->Device;
    }

    if (STRCASECMP(PlusBrachyStepper::GetBrachyStepperTypeInString(PlusBrachyStepper::BURDETTE_MEDICAL_SYSTEMS_DIGITAL_STEPPER).c_str(), brachyStepperType) == 0)
    {
      this->Device = new PlusCmsBrachyStepper(this->GetSerialPort(), this->GetBaudRate());
      this->Device->SetBrachyStepperType(PlusBrachyStepper::BURDETTE_MEDICAL_SYSTEMS_DIGITAL_STEPPER);
      this->BrachyStepperType = PlusBrachyStepper::BURDETTE_MEDICAL_SYSTEMS_DIGITAL_STEPPER;

    }
    else if (STRCASECMP(PlusBrachyStepper::GetBrachyStepperTypeInString(PlusBrachyStepper::BURDETTE_MEDICAL_SYSTEMS_DIGITAL_MOTORIZED_STEPPER).c_str(), brachyStepperType) == 0)
    {
      this->Device = new PlusCmsBrachyStepper(this->GetSerialPort(), this->GetBaudRate());
      this->Device->SetBrachyStepperType(PlusBrachyStepper::BURDETTE_MEDICAL_SYSTEMS_DIGITAL_MOTORIZED_STEPPER);
      this->BrachyStepperType = PlusBrachyStepper::BURDETTE_MEDICAL_SYSTEMS_DIGITAL_MOTORIZED_STEPPER;
    }
    else if (STRCASECMP(PlusBrachyStepper::GetBrachyStepperTypeInString(PlusBrachyStepper::CMS_ACCUSEED_DS300).c_str(), brachyStepperType) == 0)
    {
      this->Device = new PlusCmsBrachyStepper(this->GetSerialPort(), this->GetBaudRate());
      this->Device->SetBrachyStepperType(PlusBrachyStepper::CMS_ACCUSEED_DS300);
      this->BrachyStepperType = PlusBrachyStepper::CMS_ACCUSEED_DS300;
    }
    else if (STRCASECMP(PlusBrachyStepper::GetBrachyStepperTypeInString(PlusBrachyStepper::CIVCO_STEPPER).c_str(), brachyStepperType) == 0)
    {
	  LOG_TRACE("Setting type CIVCO_STEPPER");
      this->Device = new PlusCivcoBrachyStepper(this->GetSerialPort(), this->GetBaudRate());
      this->Device->SetBrachyStepperType(PlusBrachyStepper::CIVCO_STEPPER);
      this->BrachyStepperType = PlusBrachyStepper::CIVCO_STEPPER;
    }
    else
    {
      LOG_ERROR("Unable to recognize brachy stepper type: " << brachyStepperType);
      return PLUS_FAIL;
    }

    XML_READ_CSTRING_ATTRIBUTE_OPTIONAL(ModelNumber, deviceConfig);
    XML_READ_CSTRING_ATTRIBUTE_OPTIONAL(ModelVersion, deviceConfig);
    XML_READ_CSTRING_ATTRIBUTE_OPTIONAL(ModelSerialNumber, deviceConfig);
  }

  vtkXMLDataElement* calibration = deviceConfig->FindNestedElementWithName("StepperCalibrationResult");
  if (calibration != NULL)
  {
    const char* calibrationAlgorithmVersion = calibration->GetAttribute("AlgorithmVersion");
    if (calibrationAlgorithmVersion != NULL)
    {
      this->SetCalibrationAlgorithmVersion(calibrationAlgorithmVersion);
    }
    else
    {
      LOG_WARNING("Failed to read stepper calibration algorithm version from config file!");
      this->SetCalibrationAlgorithmVersion("Unknown");
    }

    const char* calibrationDate = calibration->GetAttribute("Date");
    if (calibrationDate != NULL)
    {
      this->SetCalibrationDate(calibrationDate);
    }
    else
    {
      LOG_WARNING("Failed to read stepper calibration date from config file!");
      this->SetCalibrationDate("Unknown");
    }

    XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 3, ProbeTranslationAxisOrientation, calibration);
    XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 3, TemplateTranslationAxisOrientation, calibration);
    XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 3, ProbeRotationAxisOrientation, calibration);
    XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, ProbeRotationEncoderScale, calibration);

  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(trackerConfig, rootConfigElement);

  if (this->Device != NULL)
  {
    PlusBrachyStepper::BRACHY_STEPPER_TYPE stepperType = this->Device->GetBrachyStepperType();
    std::string strStepperType = PlusBrachyStepper::GetBrachyStepperTypeInString(stepperType);
    trackerConfig->SetAttribute("BrachyStepperType", strStepperType.c_str());
  }

  trackerConfig->SetUnsignedLongAttribute("SerialPort", this->GetSerialPort());
  trackerConfig->SetDoubleAttribute("BaudRate", this->GetBaudRate());
  trackerConfig->SetAttribute("ModelVersion", this->GetModelVersion());
  trackerConfig->SetAttribute("ModelNumber", this->GetModelNumber());
  trackerConfig->SetAttribute("ModelSerialNumber", this->GetModelSerialNumber());

  // Save stepper calibration results to file
  vtkXMLDataElement* calibration = trackerConfig->FindNestedElementWithName("StepperCalibrationResult");
  if (calibration == NULL)
  {
    // create new element and add to trackerTool
    vtkSmartPointer<vtkXMLDataElement> newCalibration = vtkSmartPointer<vtkXMLDataElement>::New();
    newCalibration->SetName("StepperCalibrationResult");
    newCalibration->SetParent(trackerConfig);
    trackerConfig->AddNestedElement(newCalibration);
    calibration = newCalibration;
  }

  calibration->SetAttribute("Date", this->GetCalibrationDate());
  calibration->SetAttribute("AlgorithmVersion", this->GetCalibrationAlgorithmVersion());
  calibration->SetVectorAttribute("ProbeRotationAxisOrientation", 3, this->GetProbeRotationAxisOrientation());
  calibration->SetDoubleAttribute("ProbeRotationEncoderScale", this->GetProbeRotationEncoderScale());
  calibration->SetVectorAttribute("ProbeTranslationAxisOrientation", 3, this->GetProbeTranslationAxisOrientation());
  calibration->SetVectorAttribute("TemplateTranslationAxisOrientation", 3, this->GetTemplateTranslationAxisOrientation());

  return PLUS_SUCCESS;
}


//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::ResetStepper()
{
  return this->Device->ResetStepper();
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::InitializeStepper(std::string& calibMsg)
{
  return this->Device->InitializeStepper(calibMsg);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetTrackedFrame(double timestamp, igsioTrackedFrame* aTrackedFrame)
{
  if (!aTrackedFrame)
  {
    return PLUS_FAIL;
  }

  // PROBEHOME_TO_PROBE_TRANSFORM
  ToolStatus probehome2probeStatus = TOOL_OK;
  vtkSmartPointer<vtkMatrix4x4> probehome2probeMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  if (this->GetProbeHomeToProbeTransform(timestamp, probehome2probeMatrix, probehome2probeStatus) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get probe home to probe transform from buffer!");
    return PLUS_FAIL;
  }

  igsioTransformName probeToReferenceTransformName(this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM), this->ToolReferenceFrameName);
  if (!probeToReferenceTransformName.IsValid())
  {
    LOG_ERROR("Invalid probe to reference tranform name!");
    return PLUS_FAIL;
  }

  if (aTrackedFrame->SetFrameTransform(probeToReferenceTransformName, probehome2probeMatrix) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to set transform for tool " << this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM));
    return PLUS_FAIL;
  }

  // Convert
  if (aTrackedFrame->SetFrameTransformStatus(probeToReferenceTransformName, probehome2probeStatus) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to set transform status for tool " << this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM));
    return PLUS_FAIL;
  }


  // TEMPLATEHOME_TO_TEMPLATE_TRANSFORM
  ToolStatus templhome2templStatus = TOOL_OK;
  vtkSmartPointer<vtkMatrix4x4> templhome2templMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  if (this->GetTemplateHomeToTemplateTransform(timestamp, templhome2templMatrix, templhome2templStatus) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get template home to template transform from buffer!");
    return PLUS_FAIL;
  }

  igsioTransformName templateToReferenceTransformName(this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM), this->ToolReferenceFrameName);
  if (!templateToReferenceTransformName.IsValid())
  {
    LOG_ERROR("Invalid template to reference tranform name!");
    return PLUS_FAIL;
  }

  if (aTrackedFrame->SetFrameTransform(templateToReferenceTransformName, templhome2templMatrix) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to set transform for tool " << this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM));
    return PLUS_FAIL;
  }

  if (aTrackedFrame->SetFrameTransformStatus(templateToReferenceTransformName, templhome2templStatus) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to set transform status for tool " << this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM));
    return PLUS_FAIL;
  }


  // RAW_ENCODER_VALUES
  ToolStatus rawEncoderValuesStatus = TOOL_OK;
  vtkSmartPointer<vtkMatrix4x4> rawEncoderValuesMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  if (this->GetRawEncoderValuesTransform(timestamp, rawEncoderValuesMatrix, rawEncoderValuesStatus) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get raw encoder values from buffer!");
    return PLUS_FAIL;
  }

  igsioTransformName encoderToReferenceTransformName(this->GetBrachyToolSourceId(RAW_ENCODER_VALUES), this->ToolReferenceFrameName);
  if (!encoderToReferenceTransformName.IsValid())
  {
    LOG_ERROR("Invalid encoder to reference tranform name!");
    return PLUS_FAIL;
  }

  if (aTrackedFrame->SetFrameTransform(encoderToReferenceTransformName, rawEncoderValuesMatrix) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to set transform for tool " << this->GetBrachyToolSourceId(RAW_ENCODER_VALUES));
    return PLUS_FAIL;
  }

  if (aTrackedFrame->SetFrameTransformStatus(encoderToReferenceTransformName, rawEncoderValuesStatus) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to set transform status for tool " << this->GetBrachyToolSourceId(RAW_ENCODER_VALUES));
    return PLUS_FAIL;
  }

  // Get value for PROBE_POSITION, PROBE_ROTATION, TEMPLATE_POSITION tools
  ToolStatus encoderStatus = TOOL_OK;
  double probePos(0), probeRot(0), templatePos(0);
  if (vtkPlusBrachyTracker::GetStepperEncoderValues(timestamp, probePos, probeRot, templatePos, encoderStatus) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get stepper encoder values!");
    return PLUS_FAIL;
  }

  // PROBE_POSITION 
  std::ostringstream strProbePos;
  strProbePos << probePos;
  LOG_TRACE("Setting ProbePosition " << strProbePos.str());
  aTrackedFrame->SetFrameField("ProbePosition", strProbePos.str());

  // PROBE_ROTATION
  std::ostringstream strProbeRot;
  strProbeRot << probeRot;
  LOG_TRACE("Setting ProbeRotation " << strProbeRot.str());
  aTrackedFrame->SetFrameField("ProbeRotation", strProbeRot.str());

  // TEMPLATE_POSITION
  std::ostringstream strTemplatePos;
  strTemplatePos << templatePos;
  LOG_TRACE("Setting TemplatePosition " << strTemplatePos.str());
  aTrackedFrame->SetFrameField("TemplatePosition", strTemplatePos.str());

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetLatestStepperEncoderValues(double& probePosition, double& probeRotation, double& templatePosition, ToolStatus& status)
{
  std::string encoderToolSourceId = this->GetBrachyToolSourceId(RAW_ENCODER_VALUES);
  if (encoderToolSourceId.empty())
  {
    LOG_ERROR("Failed to get encoder values tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* encoderTool = NULL;
  if (this->GetTool(encoderToolSourceId.c_str(), encoderTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << encoderToolSourceId);
    return PLUS_FAIL;
  }

  if (encoderTool->GetNumberOfItems() < 1)
  {
    LOG_DEBUG("The buffer is empty"); // do not report as an error, it may be normal after a buffer clear
    probePosition = 0.0;
    probeRotation = 0.0;
    templatePosition = 0.0;
    status = TOOL_MISSING;
    return PLUS_SUCCESS;
  }
  BufferItemUidType latestUid = encoderTool->GetLatestItemUidInBuffer();

  return vtkPlusBrachyTracker::GetStepperEncoderValues(latestUid, probePosition, probeRotation, templatePosition, status);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetStepperEncoderValues(BufferItemUidType uid, double& probePosition, double& probeRotation, double& templatePosition, ToolStatus& status)
{
  std::string encoderToolSourceId = this->GetBrachyToolSourceId(RAW_ENCODER_VALUES);
  if (encoderToolSourceId.empty())
  {
    LOG_ERROR("Failed to get encoder values tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* encoderTool = NULL;
  if (this->GetTool(encoderToolSourceId.c_str(), encoderTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << encoderToolSourceId);
    return PLUS_FAIL;
  }

  StreamBufferItem bufferItem;
  if (encoderTool->GetStreamBufferItem(uid, &bufferItem) != ITEM_OK)
  {
    LOG_ERROR("Failed to get stepper encoder values from buffer by UID: " << uid);
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkMatrix4x4> mx = vtkSmartPointer<vtkMatrix4x4>::New();
  if (bufferItem.GetMatrix(mx) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get bufferitem matrix by UID: " << uid);
    return PLUS_FAIL;
  }
  probePosition = mx->GetElement(ROW_PROBE_POSITION, 3);
  probeRotation = mx->GetElement(ROW_PROBE_ROTATION, 3);
  templatePosition = mx->GetElement(ROW_TEMPLATE_POSITION, 3);
  status = bufferItem.GetStatus();

  return PLUS_SUCCESS;
}


//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetStepperEncoderValues(double timestamp, double& probePosition, double& probeRotation, double& templatePosition, ToolStatus& status)
{
  std::string encoderToolSourceId = this->GetBrachyToolSourceId(RAW_ENCODER_VALUES);
  if (encoderToolSourceId.empty())
  {
    LOG_ERROR("Failed to get encoder values tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* encoderTool = NULL;
  if (this->GetTool(encoderToolSourceId.c_str(), encoderTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << encoderToolSourceId);
    return PLUS_FAIL;
  }

  BufferItemUidType uid(0);
  if (encoderTool->GetItemUidFromTime(timestamp, uid) != ITEM_OK)
  {
    LOG_ERROR("Failed to get stepper encoder values from buffer by time: " << std::fixed << timestamp);
    return PLUS_FAIL;
  }

  return vtkPlusBrachyTracker::GetStepperEncoderValues(uid, probePosition, probeRotation, templatePosition, status);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetProbeHomeToProbeTransform(BufferItemUidType uid, vtkMatrix4x4* probeHomeToProbeMatrix, ToolStatus& status)
{
  if (probeHomeToProbeMatrix == NULL)
  {
    LOG_ERROR("Failed to get probe home to probe transform - input transform is NULL!");
    return PLUS_FAIL;
  }

  std::string probeToolSourceId = this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM);
  if (probeToolSourceId.empty())
  {
    LOG_ERROR("Failed to get probe tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* probeTool = NULL;
  if (this->GetTool(probeToolSourceId.c_str(), probeTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << probeToolSourceId);
    return PLUS_FAIL;
  }

  StreamBufferItem bufferItem;
  if (probeTool->GetStreamBufferItem(uid, &bufferItem) != ITEM_OK)
  {
    LOG_ERROR("Failed to get probe home to probe transform by UID: " << uid);
    return PLUS_FAIL;
  }

  status = bufferItem.GetStatus();
  if (bufferItem.GetMatrix(probeHomeToProbeMatrix) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get probeHomeToProbeMatrix");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetProbeHomeToProbeTransform(double timestamp, vtkMatrix4x4* probeHomeToProbeMatrix, ToolStatus& status)
{
  std::string probeToolSourceId = this->GetBrachyToolSourceId(PROBEHOME_TO_PROBE_TRANSFORM);
  if (probeToolSourceId.empty())
  {
    LOG_ERROR("Failed to get probe tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* probeTool = NULL;
  if (this->GetTool(probeToolSourceId.c_str(), probeTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << probeToolSourceId);
    return PLUS_FAIL;
  }

  BufferItemUidType uid(0);
  if (probeTool->GetItemUidFromTime(timestamp, uid) != ITEM_OK)
  {
    LOG_ERROR("Failed to get probe home to probe transform by timestamp: " << std::fixed << timestamp);
    PLUS_FAIL;
  }

  return this->GetProbeHomeToProbeTransform(uid, probeHomeToProbeMatrix, status);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetTemplateHomeToTemplateTransform(BufferItemUidType uid, vtkMatrix4x4* templateHomeToTemplateMatrix, ToolStatus& status)
{
  if (templateHomeToTemplateMatrix == NULL)
  {
    LOG_ERROR("Failed to get template home to template transform - input transform is NULL!");
    return PLUS_FAIL;
  }

  std::string templateToolSourceId = this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM);
  if (templateToolSourceId.empty())
  {
    LOG_ERROR("Failed to get template tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* templateTool = NULL;
  if (this->GetTool(templateToolSourceId.c_str(), templateTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << templateToolSourceId);
    return PLUS_FAIL;
  }

  StreamBufferItem bufferItem;
  if (templateTool->GetStreamBufferItem(uid, &bufferItem) != ITEM_OK)
  {
    LOG_ERROR("Failed to get template home to template transform by UID: " << uid);
    return PLUS_FAIL;
  }

  status = bufferItem.GetStatus();
  if (bufferItem.GetMatrix(templateHomeToTemplateMatrix) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get templateHomeToTemplateMatrix");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetTemplateHomeToTemplateTransform(double timestamp, vtkMatrix4x4* templateHomeToTemplateMatrix, ToolStatus& status)
{
  std::string templateToolSourceId = this->GetBrachyToolSourceId(TEMPLATEHOME_TO_TEMPLATE_TRANSFORM);
  if (templateToolSourceId.empty())
  {
    LOG_ERROR("Failed to get template tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* templateTool = NULL;
  if (this->GetTool(templateToolSourceId.c_str(), templateTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << templateToolSourceId);
    return PLUS_FAIL;
  }

  BufferItemUidType uid(0);
  if (templateTool->GetItemUidFromTime(timestamp, uid) != ITEM_OK)
  {
    LOG_ERROR("Failed to get template home to template transform by timestamp: " << std::fixed << timestamp);
    return PLUS_FAIL;
  }

  return this->GetTemplateHomeToTemplateTransform(uid, templateHomeToTemplateMatrix, status);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetRawEncoderValuesTransform(BufferItemUidType uid, vtkMatrix4x4* rawEncoderValuesTransform, ToolStatus& status)
{
  if (rawEncoderValuesTransform == NULL)
  {
    LOG_ERROR("Failed to get raw encoder values transform from buffer - input transform NULL!");
    return PLUS_FAIL;
  }

  std::string encoderToolSourceId = this->GetBrachyToolSourceId(RAW_ENCODER_VALUES);
  if (encoderToolSourceId.empty())
  {
    LOG_ERROR("Failed to get encoder values tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* encoderTool = NULL;
  if (this->GetTool(encoderToolSourceId.c_str(), encoderTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << encoderToolSourceId);
    return PLUS_FAIL;
  }

  StreamBufferItem bufferItem;
  if (encoderTool->GetStreamBufferItem(uid, &bufferItem) != ITEM_OK)
  {
    LOG_ERROR("Failed to get raw encoder values transform from buffer by UID: " << uid);
    return PLUS_FAIL;
  }

  if (bufferItem.GetMatrix(rawEncoderValuesTransform) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get rawEncoderValuesTransform");
    return PLUS_FAIL;
  }

  status = bufferItem.GetStatus();

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::GetRawEncoderValuesTransform(double timestamp, vtkMatrix4x4* rawEncoderValuesTransform, ToolStatus& status)
{
  std::string encoderToolSourceId = this->GetBrachyToolSourceId(RAW_ENCODER_VALUES);
  if (encoderToolSourceId.empty())
  {
    LOG_ERROR("Failed to get encoder values tool name!");
    return PLUS_FAIL;
  }

  vtkPlusDataSource* encoderTool = NULL;
  if (this->GetTool(encoderToolSourceId.c_str(), encoderTool) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get tool: " << encoderToolSourceId);
    return PLUS_FAIL;
  }

  BufferItemUidType uid(0);
  if (encoderTool->GetItemUidFromTime(timestamp, uid) != ITEM_OK)
  {
    LOG_ERROR("Failed to get raw encoder values transform by timestamp: " << std::fixed << timestamp);
    return PLUS_FAIL;
  }

  return this->GetRawEncoderValuesTransform(uid, rawEncoderValuesTransform, status);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBrachyTracker::NotifyConfigured()
{
  if (this->OutputChannels.size() > 1)
  {
    LOG_WARNING("vtkPlusBrachyTracker is expecting one output channel and there are " << this->OutputChannels.size() << " channels. First output channel will be used.");
    return PLUS_FAIL;
  }

  if (this->OutputChannels.empty())
  {
    LOG_ERROR("No output channels defined for vtkPlusBrachyTracker. Cannot proceed.");
    this->SetCorrectlyConfigured(false);
    return PLUS_FAIL;
  }

  vtkPlusChannel* outputChannel = this->OutputChannels[0];

  outputChannel->Clear();
  for (DataSourceContainerIterator it = this->Tools.begin(); it != this->Tools.end(); ++it)
  {
    outputChannel->AddTool(it->second);
  }

  return PLUS_SUCCESS;
}