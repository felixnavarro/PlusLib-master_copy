/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

/*!
  \file vtkPhantomRegistrationTest.cxx
  \brief This test runs a phantom registration on a recorded data set and
  compares the results to a baseline
*/

#include "PlusConfigure.h"
#include "PlusMath.h"
#include "igsioTrackedFrame.h"
#include "vtkPlusDataCollector.h"
#include "vtkPlusFakeTracker.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkPlusPhantomLandmarkRegistrationAlgo.h"
#include "vtkPlusChannel.h"
#include "vtkSmartPointer.h"
#include "vtkIGSIOTrackedFrameList.h"
#include "vtkTransform.h"
#include "vtkIGSIOTransformRepository.h"
#include "vtkXMLDataElement.h"
#include "vtkXMLUtilities.h"
#include "vtksys/CommandLineArguments.hxx"
#include "vtksys/SystemTools.hxx"
#include <iostream>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////
const double ERROR_THRESHOLD = 0.001; // error threshold

PlusStatus CompareRegistrationResultsWithBaseline(const char* baselineFileName, const char* currentResultFileName, const char* phantomCoordinateFrame, const char* referenceCoordinateFrame);

int main(int argc, char* argv[])
{
  std::string inputConfigFileName;
  std::string inputBaselineFileName;

  int verboseLevel = vtkPlusLogger::LOG_LEVEL_UNDEFINED;

  vtksys::CommandLineArguments cmdargs;
  cmdargs.Initialize(argc, argv);

  cmdargs.AddArgument("--config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputConfigFileName, "Configuration file name");
  cmdargs.AddArgument("--baseline-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputBaselineFileName, "Name of file storing baseline calibration results");
  cmdargs.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");

  if (!cmdargs.Parse())
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << cmdargs.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  LOG_INFO("Initialize");

  // Read configuration
  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkSmartPointer<vtkXMLDataElement>::New();
  if (PlusXmlUtils::ReadDeviceSetConfigurationFromFile(configRootElement, inputConfigFileName.c_str()) == PLUS_FAIL)
  {
    LOG_ERROR("Unable to read configuration from file " << inputConfigFileName.c_str());
    return EXIT_FAILURE;
  }

  vtkPlusConfig::GetInstance()->SetDeviceSetConfigurationData(configRootElement);

  // Initialize data collection
  vtkSmartPointer<vtkPlusDataCollector> dataCollector = vtkSmartPointer<vtkPlusDataCollector>::New();
  if (dataCollector->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to parse configuration from file " << inputConfigFileName.c_str());
    exit(EXIT_FAILURE);
  }

  if (dataCollector->Connect() != PLUS_SUCCESS)
  {
    LOG_ERROR("Data collector was unable to connect to devices!");
    exit(EXIT_FAILURE);
  }
  if (dataCollector->Start() != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to start data collection!");
    exit(EXIT_FAILURE);
  }
  vtkPlusChannel* aChannel(NULL);
  vtkPlusDevice* aDevice(NULL);
  if (dataCollector->GetDevice(aDevice, std::string("TrackerDevice")) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to locate device by ID: \'TrackerDevice\'");
    exit(EXIT_FAILURE);
  }
  if (aDevice->GetOutputChannelByName(aChannel, "TrackerStream") != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to locate channel by ID: \'TrackerStream\'");
    exit(EXIT_FAILURE);
  }
  if (aChannel->GetTrackingDataAvailable() == false)
  {
    LOG_ERROR("Channel \'" << aChannel->GetChannelId() << "\' is not tracking!");
    exit(EXIT_FAILURE);
  }
  if (aChannel->GetTrackingDataAvailable() == false)
  {
    LOG_ERROR("Data collector is not tracking!");
    exit(EXIT_FAILURE);
  }

  // Read coordinate definitions
  vtkSmartPointer<vtkIGSIOTransformRepository> transformRepository = vtkSmartPointer<vtkIGSIOTransformRepository>::New();
  if (transformRepository->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to read CoordinateDefinitions!");
    exit(EXIT_FAILURE);
  }

  // Initialize phantom registration
  vtkSmartPointer<vtkPlusPhantomLandmarkRegistrationAlgo> phantomRegistration = vtkSmartPointer<vtkPlusPhantomLandmarkRegistrationAlgo>::New();
  if (phantomRegistration == NULL)
  {
    LOG_ERROR("Unable to instantiate phantom registration algorithm class!");
    exit(EXIT_FAILURE);
  }
  if (phantomRegistration->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to read phantom definition!");
    exit(EXIT_FAILURE);
  }

  int numberOfLandmarks = phantomRegistration->GetDefinedLandmarks_Phantom()->GetNumberOfPoints();
  if (numberOfLandmarks != 8)
  {
    LOG_ERROR("Number of defined landmarks should be 8 instead of " << numberOfLandmarks << "!");
    exit(EXIT_FAILURE);
  }

  // Acquire landmarks
  vtkPlusFakeTracker* fakeTracker = dynamic_cast<vtkPlusFakeTracker*>(aDevice);
  if (fakeTracker == NULL)
  {
    LOG_ERROR("Invalid tracker object!");
    exit(EXIT_FAILURE);
  }
  fakeTracker->SetTransformRepository(transformRepository);

  igsioTrackedFrame trackedFrame;
  igsioTransformName stylusTipToReferenceTransformName(phantomRegistration->GetStylusTipCoordinateFrame(), phantomRegistration->GetReferenceCoordinateFrame());

  for (int landmarkCounter = 0; landmarkCounter < numberOfLandmarks; ++landmarkCounter)
  {
    fakeTracker->SetCounter(landmarkCounter);
    vtkIGSIOAccurateTimer::Delay(2.1 / fakeTracker->GetAcquisitionRate());

    vtkSmartPointer<vtkMatrix4x4> stylusTipToReferenceMatrix = vtkSmartPointer<vtkMatrix4x4>::New();

    aChannel->GetTrackedFrame(trackedFrame);
    transformRepository->SetTransforms(trackedFrame);

    ToolStatus status(TOOL_INVALID);
    if (transformRepository->GetTransform(stylusTipToReferenceTransformName, stylusTipToReferenceMatrix, &status) != PLUS_SUCCESS || status != TOOL_OK)
    {
      LOG_ERROR("No valid transform found between stylus tip to reference!");
      continue;
    }

    // Compute point position from matrix
    double stylusTipPosition[3] = {stylusTipToReferenceMatrix->GetElement(0, 3), stylusTipToReferenceMatrix->GetElement(1, 3), stylusTipToReferenceMatrix->GetElement(2, 3) };

    // Add recorded point to algorithm
    phantomRegistration->GetRecordedLandmarks_Reference()->InsertPoint(landmarkCounter, stylusTipPosition);
    phantomRegistration->GetRecordedLandmarks_Reference()->Modified();

    vtkPlusLogger::PrintProgressbar((100.0 * landmarkCounter) / numberOfLandmarks);
  }

  if (phantomRegistration->LandmarkRegister(transformRepository) != PLUS_SUCCESS)
  {
    LOG_ERROR("Phantom registration failed!");
    exit(EXIT_FAILURE);
  }

  vtkPlusLogger::PrintProgressbar(100);

  LOG_INFO("Registration error = " << phantomRegistration->GetRegistrationErrorMm());

  // Save result
  if (transformRepository->WriteConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to write phantom registration result to configuration element!");
    exit(EXIT_FAILURE);
  }

  std::string registrationResultFileName = "PhantomRegistrationTest.xml";
  vtksys::SystemTools::RemoveFile(registrationResultFileName.c_str());
  igsioCommon::XML::PrintXML(registrationResultFileName.c_str(), configRootElement);

  if (CompareRegistrationResultsWithBaseline(inputBaselineFileName.c_str(), registrationResultFileName.c_str(), phantomRegistration->GetPhantomCoordinateFrame(), phantomRegistration->GetReferenceCoordinateFrame()) != PLUS_SUCCESS)
  {
    LOG_ERROR("Comparison of calibration data to baseline failed");
    std::cout << "Exit failure!!!" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "Exit success!!!" << std::endl;
  return EXIT_SUCCESS;
}

//-----------------------------------------------------------------------------

// return the number of differences
PlusStatus CompareRegistrationResultsWithBaseline(const char* baselineFileName, const char* currentResultFileName, const char* phantomCoordinateFrame, const char* referenceCoordinateFrame)
{
  if (baselineFileName == NULL)
  {
    LOG_ERROR("Unable to read the baseline configuration file - filename is NULL");
    return PLUS_FAIL;
  }

  if (currentResultFileName == NULL)
  {
    LOG_ERROR("Unable to read the current configuration file - filename is NULL");
    return PLUS_FAIL;
  }

  igsioTransformName tnPhantomToPhantomReference(phantomCoordinateFrame, referenceCoordinateFrame);

  // Load current phantom registration
  vtkSmartPointer<vtkXMLDataElement> currentRootElem = vtkSmartPointer<vtkXMLDataElement>::Take(
        vtkXMLUtilities::ReadElementFromFile(currentResultFileName));
  if (currentRootElem == NULL)
  {
    LOG_ERROR("Unable to read the current configuration file: " << currentResultFileName);
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkIGSIOTransformRepository> currentTransformRepository = vtkSmartPointer<vtkIGSIOTransformRepository>::New();
  if (currentTransformRepository->ReadConfiguration(currentRootElem) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to read the current CoordinateDefinitions from configuration file: " << currentResultFileName);
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkMatrix4x4> currentMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  if (currentTransformRepository->GetTransform(tnPhantomToPhantomReference, currentMatrix) != PLUS_SUCCESS)
  {
    std::string strTransformName;
    tnPhantomToPhantomReference.GetTransformName(strTransformName);
    LOG_ERROR("Unable to get '" << strTransformName << "' coordinate definition from configuration file: " << currentResultFileName);
    return PLUS_FAIL;
  }

  // Load baseline phantom registration
  vtkSmartPointer<vtkXMLDataElement> baselineRootElem = vtkSmartPointer<vtkXMLDataElement>::Take(
        vtkXMLUtilities::ReadElementFromFile(baselineFileName));
  if (baselineFileName == NULL)
  {
    LOG_ERROR("Unable to read the baseline configuration file: " << baselineFileName);
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkIGSIOTransformRepository> baselineTransformRepository = vtkSmartPointer<vtkIGSIOTransformRepository>::New();
  if (baselineTransformRepository->ReadConfiguration(baselineRootElem) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to read the baseline CoordinateDefinitions from configuration file: " << baselineFileName);
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkMatrix4x4> baselineMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  if (baselineTransformRepository->GetTransform(tnPhantomToPhantomReference, baselineMatrix) != PLUS_SUCCESS)
  {
    std::string strTransformName;
    tnPhantomToPhantomReference.GetTransformName(strTransformName);
    LOG_ERROR("Unable to get '" << strTransformName << "' coordinate definition from configuration file: " << baselineFileName);
    return PLUS_FAIL;
  }

  // Compare the transforms
  double posDiff = igsioMath::GetPositionDifference(currentMatrix, baselineMatrix);
  double orientDiff = igsioMath::GetOrientationDifference(currentMatrix, baselineMatrix);

  if (fabs(posDiff) > ERROR_THRESHOLD || fabs(orientDiff) > ERROR_THRESHOLD)
  {
    LOG_ERROR("Transform mismatch (position difference: " << posDiff << "  orientation difference: " << orientDiff);
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}
