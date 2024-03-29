/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

/*!
  \file vtkDataCollectorFileTest.cxx
  \brief This program tests if a recorded tracked ultrasound buffer can be read and replayed from file using vtkPlusDataCollectorFile
*/

#include "PlusConfigure.h"
#include "igsioTrackedFrame.h"
#include "vtkPlusDataCollector.h"
#include "vtkMatrix4x4.h"
#include "vtkPlusChannel.h"
#include "vtkPlusDataSource.h"
#include "vtkSmartPointer.h"
#include "vtkIGSIOTransformRepository.h"
#include "vtkXMLUtilities.h"
#include "vtksys/CommandLineArguments.hxx"

static const int COMPARE_TRANSFORM_TOLERANCE = 0.001;

PlusStatus CompareTransform(igsioTransformName& transformName, vtkIGSIOTransformRepository* transformRepository, double xExpected, double yExpected, double zExpected)
{
  vtkSmartPointer<vtkMatrix4x4> transformMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  ToolStatus toolStatus(TOOL_INVALID);
  if (transformRepository->GetTransform(transformName, transformMatrix, &toolStatus) != PLUS_SUCCESS || toolStatus != TOOL_OK)
  {
    std::string transformNameStr;
    transformName.GetTransformName(transformNameStr);
    LOG_ERROR("Unable to get transform " << transformNameStr);
    return PLUS_FAIL;
  }
  PlusStatus status = PLUS_SUCCESS;
  std::string transformNameStr;
  transformName.GetTransformName(transformNameStr);
  double actualValue = transformMatrix ->GetElement(0, 3);
  if (fabs(actualValue - xExpected) > COMPARE_TRANSFORM_TOLERANCE)
  {
    LOG_ERROR("Transform " << transformNameStr << " x translation does not match (actual=" << actualValue << ", expected=" << xExpected << ")");
    status = PLUS_FAIL;
  }
  actualValue = transformMatrix ->GetElement(1, 3);
  if (fabs(actualValue - yExpected) > COMPARE_TRANSFORM_TOLERANCE)
  {
    LOG_ERROR("Transform " << transformNameStr << " y translation does not match (actual=" << actualValue << ", expected=" << yExpected << ")");
    status = PLUS_FAIL;
  }
  actualValue = transformMatrix ->GetElement(2, 3);
  if (fabs(actualValue - zExpected) > COMPARE_TRANSFORM_TOLERANCE)
  {
    LOG_ERROR("Transform " << transformNameStr << " z translation does not match (actual=" << actualValue << ", expected=" << zExpected << ")");
    status = PLUS_FAIL;
  }
  return status;
}

int main(int argc, char** argv)
{

  // Check command line arguments.
  std::string  inputConfigFileName;
  int          verboseLevel = vtkPlusLogger::LOG_LEVEL_UNDEFINED;

  vtksys::CommandLineArguments args;
  args.Initialize(argc, argv);

  args.AddArgument("--config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputConfigFileName, "Name of the input configuration file.");
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug 5=trace)");

  if (! args.Parse())
  {
    std::cerr << "Problem parsing arguments." << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    return 1;
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  // Prepare and start data collection
  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkSmartPointer<vtkXMLDataElement>::New();
  if (PlusXmlUtils::ReadDeviceSetConfigurationFromFile(configRootElement, inputConfigFileName.c_str()) == PLUS_FAIL)
  {
    LOG_ERROR("Unable to read configuration from file " << inputConfigFileName.c_str());
    return EXIT_FAILURE;
  }

  vtkPlusConfig::GetInstance()->SetDeviceSetConfigurationData(configRootElement);

  vtkSmartPointer<vtkPlusDataCollector> dataCollector = vtkSmartPointer<vtkPlusDataCollector>::New();

  if (dataCollector->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Reading configuration file failed " << inputConfigFileName);
    exit(EXIT_FAILURE);
  }

  LOG_DEBUG("Initializing data collector... ");
  dataCollector->Connect();
  dataCollector->Start();

  if (!dataCollector->GetConnected())
  {
    LOG_ERROR("Unable to start data collection!");
    return 1;
  }

  // Create the used objects
  igsioTrackedFrame trackedFrame;

  igsioTransformName referenceToTrackerTransformName("Reference", "Tracker");
  igsioTransformName probeToTrackerTransformName("Probe", "Tracker");
  igsioTransformName stylusToTrackerTransformName("Stylus", "Tracker");

  vtkSmartPointer<vtkIGSIOTransformRepository> transformRepository = vtkSmartPointer<vtkIGSIOTransformRepository>::New();

  PlusStatus compareStatus = PLUS_SUCCESS;

  vtkIGSIOAccurateTimer::Delay(5.0); // wait for 5s until the frames are acquired into the buffer

  // Check some transforms to ensure that the correct data is returned by the data collector
  // THIS TEST ONLY WORKS WITH THIS SEQUENCE METAFILE: PlusLib\data\TestImages\fCal_Test_Calibration.mha

  // Replay starts with the first frame, acquired at SystemTime=0, therefore there is an offset between
  // the timestamps in the file and the acquisition timestamp. The offset is the timestamp of the first frame in the file.
  vtkPlusDevice* aDevice(NULL);
  vtkPlusChannel* aChannel(NULL);
  if (dataCollector->GetDevice(aDevice, "TrackedVideoDevice") != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to locate device \'TrackedVideoDevice\'");
    exit(EXIT_FAILURE);
  }
  if (aDevice->GetOutputChannelByName(aChannel, "TrackedVideoStream") != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to locate channel \'TrackedVideoStream\'");
    exit(EXIT_FAILURE);
  }
  vtkPlusDataSource* aSource = NULL;
  aChannel->GetVideoSource(aSource);
  double recordingStartTime = aSource->GetStartTime();
  double timestampOfFirstFrameInFile = 2572.905343;
  double timeOffset = timestampOfFirstFrameInFile - recordingStartTime;

  // Frame 0001
  aChannel->GetTrackedFrame(2572.983529 - timeOffset, trackedFrame);
  transformRepository->SetTransforms(trackedFrame);

  if (CompareTransform(referenceToTrackerTransformName, transformRepository, 338.415, -68.1145, -24.7944) != PLUS_SUCCESS)
  {
    LOG_ERROR("Test failed on frame 1");
    compareStatus = PLUS_FAIL;
  }
  if (CompareTransform(probeToTrackerTransformName, transformRepository, 284.39, -37.1955, -13.1199) != PLUS_SUCCESS)
  {
    LOG_ERROR("Test failed on frame 1");
    compareStatus = PLUS_FAIL;
  }

  vtkSmartPointer<vtkMatrix4x4> stylusToTrackerTransformMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  ToolStatus toolStatus(TOOL_INVALID);
  if (transformRepository->GetTransform(stylusToTrackerTransformName, stylusToTrackerTransformMatrix, &toolStatus) != PLUS_SUCCESS)
  {
    std::string transformNameStr;
    stylusToTrackerTransformName.GetTransformName(transformNameStr);
    LOG_ERROR("Test failed on frame 1: unable to get transform " << transformNameStr);
  }
  if (toolStatus != TOOL_OK)
  {
    std::string transformNameStr;
    stylusToTrackerTransformName.GetTransformName(transformNameStr);
    LOG_ERROR("Test failed on frame 1: Invalid transform received, while valid transform was expected for " << transformNameStr);
  }

  // Frame 0013
  aChannel->GetTrackedFrame(2573.921586 - timeOffset, trackedFrame);
  transformRepository->SetTransforms(trackedFrame);

  if (CompareTransform(referenceToTrackerTransformName, transformRepository, 338.658, -68.523, -24.9476) != PLUS_SUCCESS)
  {
    LOG_ERROR("Test failed on frame 13");
    compareStatus = PLUS_FAIL;
  }
  if (CompareTransform(probeToTrackerTransformName, transformRepository, 284.863, -34.9189, -13.0288) != PLUS_SUCCESS)
  {
    LOG_ERROR("Test failed on frame 13");
    compareStatus = PLUS_FAIL;
  }
  toolStatus = TOOL_INVALID;
  if (transformRepository->GetTransform(stylusToTrackerTransformName, stylusToTrackerTransformMatrix, &toolStatus) != PLUS_SUCCESS)
  {
    std::string transformNameStr;
    stylusToTrackerTransformName.GetTransformName(transformNameStr);
    LOG_ERROR("Test failed on frame 13: unable to get transform " << transformNameStr);
  }
  if (toolStatus != TOOL_OK)
  {
    std::string transformNameStr;
    stylusToTrackerTransformName.GetTransformName(transformNameStr);
    LOG_ERROR("Test failed on frame 13: Invalid transform received, while valid transform was expected for " << transformNameStr);
  }

  dataCollector->Stop();
  dataCollector->Disconnect();

  if (compareStatus != PLUS_SUCCESS)
  {
    return 1;
  }
  return 0;
}
