/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include "PlusFidPatternRecognition.h"
#include "igsioTrackedFrame.h"
#include "vtkCommand.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkIGSIOSequenceIO.h"
#include "vtkSmartPointer.h"
#include "vtkIGSIOTrackedFrameList.h"
#include "vtkTransform.h"
#include "vtksys/CommandLineArguments.hxx" 
#include <iostream>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////

int main (int argc, char* argv[])
{ 
  std::string inputSequenceMetafile;
  std::string inputTransformName; 
  std::string outputWirePositionFile("./SegmentedWirePositions.txt");

  std::string inputBaselineFileName;
  double inputTranslationErrorThreshold(0); 
  double inputRotationErrorThreshold(0); 

  int verboseLevel=vtkPlusLogger::LOG_LEVEL_UNDEFINED;

  vtksys::CommandLineArguments cmdargs;
  cmdargs.Initialize(argc, argv);

  cmdargs.AddArgument("--image-seq-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputSequenceMetafile, "Image sequence metafile");
  cmdargs.AddArgument("--image-position-transform", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputTransformName, "Transform name used for image position display");
  cmdargs.AddArgument("--output-wire-position-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &outputWirePositionFile, "Result wire position file name (Default: ./SegmentedWirePositions.txt)");

  cmdargs.AddArgument("--baseline", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputBaselineFileName, "Name of file storing baseline calibration results");
  cmdargs.AddArgument("--translation-error-threshold", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputTranslationErrorThreshold, "Translation error threshold in mm.");  
  cmdargs.AddArgument("--rotation-error-threshold", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputRotationErrorThreshold, "Rotation error threshold in degrees.");  
  cmdargs.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");  

  if ( !cmdargs.Parse() )
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << cmdargs.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }
  
  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  if ( inputSequenceMetafile.empty() ) 
  {
    std::cerr << "image-seq-file argument required" << std::endl << std::endl;
    std::cout << "Help: " << cmdargs.GetHelp() << std::endl;
    exit(EXIT_FAILURE);

  }  

  LOG_INFO( "Reading sequence meta file");  
  vtkSmartPointer<vtkIGSIOTrackedFrameList> trackedFrameList = vtkSmartPointer<vtkIGSIOTrackedFrameList>::New(); 
  if( vtkIGSIOSequenceIO::Read(inputSequenceMetafile, trackedFrameList) != PLUS_SUCCESS )
  {
      LOG_ERROR("Failed to read sequence metafile: " << inputSequenceMetafile); 
      return EXIT_FAILURE;
  }

  std::ofstream positionInfo;
  positionInfo.open (outputWirePositionFile.c_str(), ios::out );

  LOG_INFO( "Segmenting frames..."); 

  for ( unsigned int frameIndex = 0; frameIndex < trackedFrameList->GetNumberOfTrackedFrames(); frameIndex++ )
  {
    vtkPlusLogger::PrintProgressbar( (100.0 * frameIndex) / trackedFrameList->GetNumberOfTrackedFrames() ); 

    PlusFidPatternRecognition patternRecognition;
    PlusPatternRecognitionResult segResults;
    PlusFidPatternRecognition::PatternRecognitionError error;

    try
    {
      // Send the image to the Segmentation component to segment
      if (trackedFrameList->GetTrackedFrame(frameIndex)->GetImageData()->GetVTKScalarPixelType() != VTK_UNSIGNED_CHAR)
      {
        LOG_ERROR("patternRecognition.RecognizePattern only works on unsigned char images");
      }
      else
      {
        patternRecognition.RecognizePattern( trackedFrameList->GetTrackedFrame(frameIndex), segResults, error, frameIndex );
      }
    }
    catch(...)
    {
      LOG_ERROR("SegmentImage: The segmentation has failed due to UNKNOWN exception thrown, the image is ignored"); 
      continue; 
    }

    igsioTransformName transformName; 
    if ( transformName.SetTransformName(inputTransformName.c_str()) != PLUS_SUCCESS )
    {
      LOG_ERROR("Invalid transform name: " << inputTransformName ); 
      return EXIT_FAILURE; 
    }
   
    if ( segResults.GetDotsFound() )
    {
      double defaultTransform[16]={0}; 
      if ( trackedFrameList->GetTrackedFrame(frameIndex)->GetFrameTransform(transformName, defaultTransform) != PLUS_SUCCESS )
      {
        LOG_ERROR("Failed to get default frame transform from tracked frame #" << frameIndex); 
        continue; 
      }

      vtkSmartPointer<vtkTransform> frameTransform = vtkSmartPointer<vtkTransform>::New(); 
      frameTransform->SetMatrix(defaultTransform); 

      double posZ = frameTransform->GetPosition()[2]; 
      double rotZ = frameTransform->GetOrientation()[2]; 

      int dataType = -1; 
      positionInfo << dataType << "\t\t" << posZ << "\t" << rotZ << "\t\t"; 

      for (unsigned int i=0; i < segResults.GetFoundDotsCoordinateValue().size(); i++)
      {
        positionInfo << segResults.GetFoundDotsCoordinateValue()[i][0] << "\t" << segResults.GetFoundDotsCoordinateValue()[i][1] << "\t\t"; 
      }

      positionInfo << std::endl; 

    }
  }
  
  positionInfo.close(); 
  vtkPlusLogger::PrintProgressbar(100); 
  std::cout << std::endl; 

  std::cout << "Exit success!!!" << std::endl; 
  return EXIT_SUCCESS; 
}
