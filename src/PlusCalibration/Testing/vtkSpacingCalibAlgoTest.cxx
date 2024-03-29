/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/ 

/*!
  \file vtkSpacingCalibAlgoTest.cxx
  \brief This test runs a spacing calibration on a recorded data set and 
  compares the results to a baseline
*/ 

#include "PlusConfigure.h"

#include "PlusFidPatternRecognition.h"
#include "vtkPlusHTMLGenerator.h"
#include "vtkIGSIOSequenceIO.h"
#include "vtkPlusSpacingCalibAlgo.h"
#include "vtkXMLDataElement.h"
#include "vtkXMLUtilities.h"
#include "vtksys/CommandLineArguments.hxx"
#include "vtksys/SystemTools.hxx"

// define tolerance used for comparing double numbers
#ifndef _WIN32
  const double DOUBLE_DIFF = LINUXTOLERANCE;
#else
  const double DOUBLE_DIFF = 0.0001;
#endif

//----------------------------------------------------------------------------
int main(int argc, char **argv)
{
  int numberOfFailures(0); 

  bool printHelp(false);

  int verboseLevel = vtkPlusLogger::LOG_LEVEL_UNDEFINED;

  vtksys::CommandLineArguments args;
  args.Initialize(argc, argv);
  std::vector<std::string> inputSequenceMetafiles; 
  std::string inputBaselineFileName(""); 
  std::string inputConfigFileName(""); 

  args.AddArgument("--help", vtksys::CommandLineArguments::NO_ARGUMENT, &printHelp, "Print this help.");  
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");  
  args.AddArgument("--source-seq-files", vtksys::CommandLineArguments::MULTI_ARGUMENT, &inputSequenceMetafiles, "Input sequence metafile(s) name with path");  
  args.AddArgument("--baseline-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputBaselineFileName, "Input xml baseline file name with path");  
  args.AddArgument("--config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputConfigFileName, "Input xml config file name with path");  
  
  if ( !args.Parse() )
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }
  
  if ( printHelp ) 
  {
    std::cout << args.GetHelp() << std::endl;
    exit(EXIT_SUCCESS); 
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  if ( inputSequenceMetafiles.empty() || inputConfigFileName.empty() || inputBaselineFileName.empty() )
  {
    std::cerr << "input-translation-sequence-metafile, input-baseline-file-name and input-config-file-name are required arguments!" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }  

  // Read configuration
  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkSmartPointer<vtkXMLDataElement>::New();
  if (PlusXmlUtils::ReadDeviceSetConfigurationFromFile(configRootElement, inputConfigFileName.c_str())==PLUS_FAIL)
  {  
    LOG_ERROR("Unable to read configuration from file " << inputConfigFileName.c_str()); 
    return EXIT_FAILURE;
  }

  vtkPlusConfig::GetInstance()->SetDeviceSetConfigurationData(configRootElement);

  PlusFidPatternRecognition patternRecognition; 
  patternRecognition.ReadConfiguration(configRootElement);

  LOG_INFO("Reading metafiles:");

  vtkSmartPointer<vtkIGSIOTrackedFrameList> trackedFrameList = vtkSmartPointer<vtkIGSIOTrackedFrameList>::New(); 
  for ( unsigned int i = 0; i < inputSequenceMetafiles.size(); ++i )
  {
    LOG_INFO("Reading " << inputSequenceMetafiles[i] << " ..."); 
    vtkSmartPointer<vtkIGSIOTrackedFrameList> tfList = vtkSmartPointer<vtkIGSIOTrackedFrameList>::New(); 
    if( vtkIGSIOSequenceIO::Read(inputSequenceMetafiles[i], tfList) != PLUS_SUCCESS )
    {
      LOG_ERROR("Failed to read sequence metafile: " << inputSequenceMetafiles[i]); 
      return EXIT_FAILURE;
    }

    if ( trackedFrameList->AddTrackedFrameList(tfList) != PLUS_SUCCESS )
    {
      LOG_ERROR("Failed to add tracked frame list to container!"); 
      return EXIT_FAILURE; 
    }
  }

  LOG_INFO("Testing image data segmentation...");
  int numberOfSuccessfullySegmentedImages = 0;
  PlusFidPatternRecognition::PatternRecognitionError error;
  patternRecognition.RecognizePattern(trackedFrameList, error, &numberOfSuccessfullySegmentedImages);
  LOG_INFO("Segmentation success rate: " << numberOfSuccessfullySegmentedImages << " out of " << trackedFrameList->GetNumberOfTrackedFrames()
    << " (" << (100.0 * numberOfSuccessfullySegmentedImages ) / trackedFrameList->GetNumberOfTrackedFrames() << "%)");

  LOG_INFO("Testing spacing computation...");
  vtkSmartPointer<vtkPlusSpacingCalibAlgo> spacingCalibAlgo = vtkSmartPointer<vtkPlusSpacingCalibAlgo>::New(); 
  spacingCalibAlgo->SetInputs(trackedFrameList, patternRecognition.GetFidLineFinder()->GetNWires()); 

  double spacing[2]={0};
  if ( spacingCalibAlgo->GetSpacing(spacing) != PLUS_SUCCESS )
  {
    LOG_ERROR("Spacing calibration failed!"); 
    numberOfFailures++; 
  }
  else
  {
    LOG_INFO("Spacing: " << std::fixed << spacing[0] << "  " << spacing[1] << " mm/px"); 
  }

  // Get calibration error
  double errorMean(0), errorStdev(0); 
  if ( spacingCalibAlgo->GetError(errorMean, errorStdev) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to get spacing calibration error!"); 
    numberOfFailures++; 
  }
  else
  {
    LOG_INFO("Spacing calibration error - mean: " << std::fixed << errorMean << "  stdev: " << errorStdev); 
  }
  
  LOG_INFO("Testing report table generation and saving into file..."); 
  vtkTable* reportTable = spacingCalibAlgo->GetReportTable(); 
  if ( reportTable != NULL )
  {
    if ( vtkPlusLogger::Instance()->GetLogLevel() >= vtkPlusLogger::LOG_LEVEL_DEBUG )
    {
      reportTable->Dump(25); 
    }
  }
  else
  {
    LOG_ERROR("Failed to get report table!"); 
    numberOfFailures++; 
  }

  LOG_INFO("Testing HTML report generation..."); 
  vtkSmartPointer<vtkPlusHTMLGenerator> htmlGenerator = vtkSmartPointer<vtkPlusHTMLGenerator>::New(); 
  htmlGenerator->SetBaseFilename("SpacingCalibrationReport");
  htmlGenerator->SetTitle("Spacing Calibration Test Report"); 
  spacingCalibAlgo->GenerateReport(htmlGenerator);
  htmlGenerator->SaveHtmlPageAutoFilename();

  std::ostringstream spacingCalibAlgoStream; 
  spacingCalibAlgo->PrintSelf(spacingCalibAlgoStream, vtkIndent(0)); 
  LOG_DEBUG("SpacingCalibAlgo::PrintSelf: "<< spacingCalibAlgoStream.str()); 
  

  //*********************************************************************
  // Save results to file

  const char calibResultSaveFilename[]="SpacingCalibrationResults.xml";
  LOG_INFO("Save calibration results to XML file: "<<calibResultSaveFilename); 
  std::ofstream outFile; 
  outFile.open(calibResultSaveFilename);  
  outFile << "<CalibrationResults>" << std::endl;
  outFile << "  <SpacingCalibrationResult " << std::fixed << std::setprecision(8)
    << "Spacing=\""<<spacing[0]<<" "<<spacing[1]<<"\" "
    << "ErrorMean=\""<<errorMean<<"\" "
    << "ErrorStdev=\""<<errorStdev<<"\" "
    << " />" << std::endl;
  outFile << "</CalibrationResults>" << std::endl;  
  outFile.close(); 

  //*********************************************************************
  // Compare result to baseline
  
  LOG_INFO("Comparing result with baseline..."); 

  vtkSmartPointer<vtkXMLDataElement> xmlBaseline = vtkSmartPointer<vtkXMLDataElement>::Take(
    vtkXMLUtilities::ReadElementFromFile(inputBaselineFileName.c_str()));

  vtkXMLDataElement* xmlSpacingCalibrationBaseline = NULL; 
  if ( xmlBaseline != NULL )
  {
    xmlSpacingCalibrationBaseline = xmlBaseline->FindNestedElementWithName("SpacingCalibrationResult"); 
  }
  else
  {
    LOG_ERROR("Failed to read baseline file!");
    numberOfFailures++;
  }

  if ( xmlSpacingCalibrationBaseline == NULL )
  {
    LOG_ERROR("Unable to find SpacingCalibrationResult XML data element in baseline: " << inputBaselineFileName); 
    numberOfFailures++; 
  }
  else
  {
    // Compare Spacing to baseline 
    double baseSpacing[2]={0}; 
    if ( !xmlSpacingCalibrationBaseline->GetVectorAttribute( "Spacing", 2, baseSpacing) )
    {
      LOG_ERROR("Unable to find Spacing XML data element in baseline."); 
      numberOfFailures++; 
    }
    else
    {
      if ( fabs(baseSpacing[0] - spacing[0]) > DOUBLE_DIFF 
        || fabs(baseSpacing[1] - spacing[1]) > DOUBLE_DIFF )
      {
        LOG_ERROR("Spacing result in pixel differ from baseline: current(" << spacing[0] << ", " << spacing[1] 
        << ") base (" << baseSpacing[0] << ", " << baseSpacing[1] << ")."); 
        numberOfFailures++;
      }
    }

    // Compare errorMean 
    double baseErrorMean=0; 
    if ( !xmlSpacingCalibrationBaseline->GetScalarAttribute("ErrorMean", baseErrorMean) )
    {
      LOG_ERROR("Unable to find ErrorMean XML data element in baseline."); 
      numberOfFailures++; 
    }
    else
    {
      if ( fabs(baseErrorMean - errorMean) > DOUBLE_DIFF )
      {
        LOG_ERROR("Spacing mean error differ from baseline: current(" << errorMean << ") base (" << baseErrorMean << ")."); 
        numberOfFailures++;
      }
    }

    // Compare errorStdev
    double baseErrorStdev=0; 
    if ( !xmlSpacingCalibrationBaseline->GetScalarAttribute("ErrorStdev", baseErrorStdev) )
    {
      LOG_ERROR("Unable to find ErrorStdev XML data element in baseline."); 
      numberOfFailures++; 
    }
    else
    {
      if ( fabs(baseErrorStdev - errorStdev) > DOUBLE_DIFF )
      {
        LOG_ERROR("Spacing stdev of error differ from baseline: current(" << errorStdev << ") base (" << baseErrorStdev << ")."); 
        numberOfFailures++;
      }
    }
  }


  if ( numberOfFailures > 0 ) 
  {
    LOG_INFO("Test failed!"); 
    return EXIT_FAILURE; 
  }

  LOG_INFO("Test finished successfully!"); 
  return EXIT_SUCCESS; 
} 
