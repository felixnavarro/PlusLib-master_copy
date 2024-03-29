/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "igsioMath.h"
#include "igsioTrackedFrame.h"
#include "vtkAppendPolyData.h"
#include "vtkCubeSource.h"
#include "vtkImageData.h"
#include "vtkMatrix4x4.h"
#include "vtkPointData.h"
#include "vtkSTLWriter.h"
#include "vtkPlusSequenceIO.h"
#include "vtkSmartPointer.h"
#include "vtkTimerLog.h"
#include "vtkIGSIOTrackedFrameList.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkIGSIOTransformRepository.h"
#include "vtkPlusUsSimulatorAlgo.h"
#include "vtkXMLImageDataWriter.h"
#include "vtkXMLUtilities.h"
#include "vtksys/CommandLineArguments.hxx"
#include <iomanip>
#include <iostream>

//display
#include "vtkImageActor.h"
#include "vtkActor.h"
#include "vtkRenderWindow.h"
#include "vtkRenderer.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkInteractorStyleImage.h"

//-----------------------------------------------------------------------------
void CreateSliceModels(vtkIGSIOTrackedFrameList* trackedFrameList, vtkIGSIOTransformRepository* transformRepository, igsioTransformName& imageToReferenceTransformName, vtkPolyData* outputPolyData)
{
  // Prepare the output polydata.
  vtkSmartPointer< vtkAppendPolyData > appender = vtkSmartPointer<vtkAppendPolyData>::New();

  // Loop over each tracked image slice.
  for (unsigned int frameIndex = 0; frameIndex < trackedFrameList->GetNumberOfTrackedFrames(); ++ frameIndex)
  {
    igsioTrackedFrame* frame = trackedFrameList->GetTrackedFrame(frameIndex);

    // Update transform repository
    if (transformRepository->SetTransforms(*frame) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to set repository transforms from tracked frame!");
      continue;
    }

    vtkSmartPointer<vtkMatrix4x4> tUserDefinedMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    if (transformRepository->GetTransform(imageToReferenceTransformName, tUserDefinedMatrix) != PLUS_SUCCESS)
    {
      std::string strTransformName;
      imageToReferenceTransformName.GetTransformName(strTransformName);
      LOG_ERROR("Failed to get transform from repository: " << strTransformName);
      continue;
    }

    vtkSmartPointer< vtkTransform> tUserDefinedTransform = vtkSmartPointer< vtkTransform >::New();
    tUserDefinedTransform->SetMatrix(tUserDefinedMatrix);

    FrameSizeType frameSize = frame->GetFrameSize();

    vtkSmartPointer<vtkTransform> tCubeToImage = vtkSmartPointer<vtkTransform>::New();
    tCubeToImage->Scale(frameSize[ 0 ], frameSize[ 1 ], 1);
    tCubeToImage->Translate(0.5, 0.5, 0.5);    // Moving the corner to the origin.

    vtkSmartPointer<vtkTransform> tCubeToTracker = vtkSmartPointer< vtkTransform >::New();
    tCubeToTracker->Identity();
    tCubeToTracker->Concatenate(tUserDefinedTransform);
    tCubeToTracker->Concatenate(tCubeToImage);

    vtkSmartPointer<vtkTransformPolyDataFilter > cubeToTracker = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    cubeToTracker->SetTransform(tCubeToTracker);
    vtkSmartPointer<vtkCubeSource> source = vtkSmartPointer<vtkCubeSource>::New();
    cubeToTracker->SetInputConnection(source->GetOutputPort());
    cubeToTracker->Update();

    appender->AddInputConnection(cubeToTracker->GetOutputPort());
  }

  appender->Update();
  outputPolyData->DeepCopy(appender->GetOutput());
}

//-----------------------------------------------------------------------------
void ShowResults(vtkIGSIOTrackedFrameList* trackedFrameList, vtkIGSIOTransformRepository* transformRepository, igsioTransformName imageToReferenceTransformName, std::string intersectionFile)
{
  // Setup Renderer to visualize surface model and ultrasound planes
  vtkSmartPointer<vtkRenderer> rendererPoly = vtkSmartPointer<vtkRenderer>::New();
  vtkSmartPointer<vtkRenderWindow> renderWindowPoly = vtkSmartPointer<vtkRenderWindow>::New();
  renderWindowPoly->AddRenderer(rendererPoly);
  vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractorPoly = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  renderWindowInteractorPoly->SetRenderWindow(renderWindowPoly);
  vtkSmartPointer<vtkInteractorStyleTrackballCamera> style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
  renderWindowInteractorPoly->SetInteractorStyle(style);

  // Visualization of the surface model

  /*
  TODO: add surface model of each SpatialModel in the simulator
  vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper->SetInputData((vtkPolyData*)usSimulator->GetInput());
  vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);
  rendererPoly->AddActor(actor);
  */

  // Visualization of the image planes
  vtkSmartPointer< vtkPolyData > slicesPolyData = vtkSmartPointer< vtkPolyData >::New();
  CreateSliceModels(trackedFrameList, transformRepository, imageToReferenceTransformName, slicesPolyData);

  if (!intersectionFile.empty())
  {
    vtkSmartPointer<vtkSTLWriter> surfaceModelWriter = vtkSmartPointer<vtkSTLWriter>::New();
    surfaceModelWriter->SetFileName(intersectionFile.c_str());
    surfaceModelWriter->SetInputData(slicesPolyData);
    surfaceModelWriter->Write();
  }

  vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper->SetInputData(slicesPolyData);
  vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);
  rendererPoly->AddActor(actor);

  renderWindowPoly->Render();
  renderWindowInteractorPoly->Start();
}

//-----------------------------------------------------------------------------
void ShowImage(vtkImageData* simOutput)
{
  vtkSmartPointer<vtkImageData> usImage = vtkSmartPointer<vtkImageData>::New();
  usImage->DeepCopy(simOutput);

  // Display output of filter
  vtkSmartPointer<vtkImageActor> redImageActor = vtkSmartPointer<vtkImageActor>::New();
  redImageActor->SetInputData(simOutput);

  // Visualize
  vtkSmartPointer<vtkRenderer> renderer = vtkSmartPointer<vtkRenderer>::New();

  // Red image is displayed
  renderer->AddActor(redImageActor);
  renderer->ResetCamera();

  vtkSmartPointer<vtkRenderWindow> renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
  renderWindow->AddRenderer(renderer);
  renderer->SetBackground(0, 72, 0);

  vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  vtkSmartPointer<vtkInteractorStyleImage> style = vtkSmartPointer<vtkInteractorStyleImage>::New();

  renderWindowInteractor->SetInteractorStyle(style);
  renderWindowInteractor->SetRenderWindow(renderWindow);
}

//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
  bool printHelp = false;
  std::string inputTransformsFile;
  std::string inputConfigFileName;
  std::string outputUsImageFile;
  std::string intersectionFile;
  bool showResults = false;
  bool useCompression(true);

  int verboseLevel = vtkPlusLogger::LOG_LEVEL_UNDEFINED;

  vtksys::CommandLineArguments args;
  args.Initialize(argc, argv);

  args.AddArgument("--help", vtksys::CommandLineArguments::NO_ARGUMENT, &printHelp, "Print this help.");
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");
  args.AddArgument("--config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputConfigFileName, "Config file containing the image to probe and phantom to reference transformations");
  args.AddArgument("--transforms-seq-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputTransformsFile, "Input file containing coordinate frames and the associated model to image transformations");
  args.AddArgument("--use-compression", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &useCompression, "Use compression when outputting data");
  args.AddArgument("--output-us-img-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &outputUsImageFile, "File name of the generated output ultrasound image");
  args.AddArgument("--output-slice-model-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &intersectionFile, "Name of STL output file containing the model of all the frames (optional)");
  args.AddArgument("--show-results", vtksys::CommandLineArguments::NO_ARGUMENT, &showResults, "Show the simulated image on the screen");

  // Input arguments error checking
  if (!args.Parse())
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  if (printHelp)
  {
    std::cout << args.GetHelp() << std::endl;
    exit(EXIT_SUCCESS);
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  if (inputConfigFileName.empty())
  {
    std::cerr << "--config-file required " << std::endl;
    exit(EXIT_FAILURE);
  }
  if (inputTransformsFile.empty())
  {
    std::cerr << "--transforms-seq-file required" << std::endl;
    exit(EXIT_FAILURE);
  }
  if (outputUsImageFile.empty())
  {
    std::cerr << "--output-us-img-file required" << std::endl;
    exit(EXIT_FAILURE);
  }

  // Read transformations data
  LOG_DEBUG("Reading input meta file...");
  vtkSmartPointer< vtkIGSIOTrackedFrameList > trackedFrameList = vtkSmartPointer< vtkIGSIOTrackedFrameList >::New();
  if (vtkPlusSequenceIO::Read(inputTransformsFile, trackedFrameList) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to load input sequences file.");
    exit(EXIT_FAILURE);
  }
  LOG_DEBUG("Reading input meta file completed");

  // Create repository for ultrasound images correlated to the iput tracked frames
  vtkSmartPointer<vtkIGSIOTrackedFrameList> simulatedUltrasoundFrameList = vtkSmartPointer<vtkIGSIOTrackedFrameList>::New();

  // Read config file
  LOG_DEBUG("Reading config file...")
  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkSmartPointer<vtkXMLDataElement>::New();
  if (PlusXmlUtils::ReadDeviceSetConfigurationFromFile(configRootElement, inputConfigFileName.c_str()) == PLUS_FAIL)
  {
    LOG_ERROR("Unable to read configuration from file " << inputConfigFileName.c_str());
    return EXIT_FAILURE;
  }
  LOG_DEBUG("Reading config file finished.");

  // Create transform repository
  vtkSmartPointer<vtkIGSIOTransformRepository> transformRepository = vtkSmartPointer<vtkIGSIOTransformRepository>::New();
  if (transformRepository->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to read transforms for transform repository!");
    exit(EXIT_FAILURE);
  }

  // Create simulator
  vtkSmartPointer<vtkPlusUsSimulatorAlgo> usSimulator = vtkSmartPointer<vtkPlusUsSimulatorAlgo>::New();
  if (usSimulator->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to read US simulator configuration!");
    exit(EXIT_FAILURE);
  }
  usSimulator->SetTransformRepository(transformRepository);
  igsioTransformName imageToReferenceTransformName(usSimulator->GetImageCoordinateFrame(), usSimulator->GetReferenceCoordinateFrame());

  // Write slice model file
  if (!intersectionFile.empty())
  {
    vtkSmartPointer< vtkPolyData > slicesPolyData = vtkSmartPointer< vtkPolyData >::New();
    CreateSliceModels(trackedFrameList, transformRepository, imageToReferenceTransformName, slicesPolyData);
    vtkSmartPointer<vtkSTLWriter> surfaceModelWriter = vtkSmartPointer<vtkSTLWriter>::New();
    surfaceModelWriter->SetFileName(intersectionFile.c_str());
    surfaceModelWriter->SetInputData(slicesPolyData);
    surfaceModelWriter->Write();
  }

  if (showResults)
  {
    ShowResults(trackedFrameList, transformRepository, imageToReferenceTransformName, intersectionFile);
  }

  std::vector<double> timeElapsedPerFrameSec;
  double startTimeSec = 0;
  double endTimeSec = 0;

  // TODO: remove this, it's just for testing
  trackedFrameList->RemoveTrackedFrameRange(15, trackedFrameList->GetNumberOfTrackedFrames() - 1);

  for (unsigned int i = 0; i < trackedFrameList->GetNumberOfTrackedFrames(); i++)
  {
    startTimeSec = vtkTimerLog::GetUniversalTime();

    LOG_DEBUG("Processing frame " << i);
    igsioTrackedFrame* frame = trackedFrameList->GetTrackedFrame(i);

    // Update transform repository
    if (transformRepository->SetTransforms(*frame) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to set repository transforms from tracked frame!");
      return EXIT_FAILURE;
    }

    // TODO: would be nice to implement an automatic mechanism to find out if the transforms have been changed
    usSimulator->Modified(); // Signal that the transforms have changed so we need to recompute
    usSimulator->Update();
    vtkImageData* simOutput = usSimulator->GetOutput();

    igsioVideoFrame* simulatorOutputigsioVideoFrame = new igsioVideoFrame();
    simulatorOutputigsioVideoFrame->DeepCopyFrom(simOutput);

    frame->SetImageData(*simulatorOutputigsioVideoFrame);

    /*
    double origin[3]=
    {
    imageToReferenceMatrix->Element[0][3],
    imageToReferenceMatrix->Element[1][3],
    imageToReferenceMatrix->Element[2][3]
    };
    simulatedUsImage->SetOrigin(origin);

    double spacing[3]=
    {
    sqrt(imageToReferenceMatrix->Element[0][0]*imageToReferenceMatrix->Element[0][0]+
    imageToReferenceMatrix->Element[1][0]*imageToReferenceMatrix->Element[1][0]+
    imageToReferenceMatrix->Element[2][0]*imageToReferenceMatrix->Element[2][0]),
    sqrt(imageToReferenceMatrix->Element[0][1]*imageToReferenceMatrix->Element[0][1]+
    imageToReferenceMatrix->Element[1][1]*imageToReferenceMatrix->Element[1][1]+
    imageToReferenceMatrix->Element[2][1]*imageToReferenceMatrix->Element[2][1]),
    1.0
    };
    simulatedUsImage->SetSpacing(spacing);
    */

    if (showResults)
    {
      ShowImage(simOutput);
    }

    endTimeSec = vtkTimerLog::GetUniversalTime();
    timeElapsedPerFrameSec.push_back(endTimeSec - startTimeSec);
  }

  if (vtkPlusSequenceIO::Write(outputUsImageFile, trackedFrameList, trackedFrameList->GetImageOrientation(), useCompression) != PLUS_SUCCESS)
  {
    // Error has already been logged
    return EXIT_FAILURE;
  }

  LOG_INFO("Computation time for the fits frame (not included in the statistics because of BSP Tree Building, in sec): " << timeElapsedPerFrameSec.at(0));

  // Remove the first frame from the statistics computation because extra processing is done for the first frame
  timeElapsedPerFrameSec.erase(timeElapsedPerFrameSec.begin());

  double meanTimeElapsedPerFrameSec = 0;
  double stdevTimeElapsedPerFrameSec = 0;
  igsioMath::ComputeMeanAndStdev(timeElapsedPerFrameSec, meanTimeElapsedPerFrameSec, stdevTimeElapsedPerFrameSec);
  LOG_INFO(" Average computation time per frame (sec): " << meanTimeElapsedPerFrameSec) ;
  LOG_INFO(" Standard dev computation time per frame (sec): " << stdevTimeElapsedPerFrameSec) ;
  LOG_INFO(" Average fps:  " << 1 / meanTimeElapsedPerFrameSec) ;

  return EXIT_SUCCESS;
}
