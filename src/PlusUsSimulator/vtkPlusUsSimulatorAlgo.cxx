/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include <algorithm>
#include <list>
#include <map>

#include "vtkPlusUsSimulatorAlgo.h"

#include "vtkImageAlgorithm.h"
#include "vtkInformation.h"
#include "vtkTransform.h"
#include "vtkPolyData.h"
#include "vtkPolyDataToImageStencil.h"
#include "vtkSmartPointer.h"
#include "vtkImageStencil.h"
#include "vtkInformationVector.h"
#include "vtkImageData.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkImageStencilData.h"
#include "vtkPolyData.h"
#include "vtksys/SystemTools.hxx"

#include "vtkPlusRfProcessor.h"
#include "vtkPlusUsScanConvert.h"

// For noise generation
#include "vtkLineSource.h"
#include "vtkPerlinNoise.h"
#include "vtkProbeFilter.h"
#include "vtkSampleFunction.h"

//-----------------------------------------------------------------------------

vtkStandardNewMacro(vtkPlusUsSimulatorAlgo);

//-----------------------------------------------------------------------------
vtkPlusUsSimulatorAlgo::vtkPlusUsSimulatorAlgo()
  : TransformRepository(NULL)
{
  SetNumberOfInputPorts(0);
  SetNumberOfOutputPorts(1);

  this->ImageCoordinateFrame = NULL;
  this->ReferenceCoordinateFrame = NULL;

  this->NumberOfScanlines = 256;
  this->NumberOfSamplesPerScanline = 1000;
  this->IncomingIntensityMwPerCm2 = 100;
  this->FrequencyMhz = 2.5;
  this->BrightnessConversionGamma = 0.333;
  this->BrightnessConversionOffset = 30;
  this->BrightnessConversionScale = 30;

  this->RfProcessor = vtkPlusRfProcessor::New();

  this->NoiseAmplitude = 0;
  this->NoiseFrequency[0] = 0;
  this->NoiseFrequency[1] = 0;
  this->NoiseFrequency[2] = 0;
  this->NoisePhase[0] = 0;
  this->NoisePhase[1] = 0;
  this->NoisePhase[2] = 0;

  // this->TransducerSpatialModel doesn't have to be initialized, as the default parameters of SpatialModel
  // are for soft tissue that should match the transducer material in acoustic impedance
}

//-----------------------------------------------------------------------------
vtkPlusUsSimulatorAlgo::~vtkPlusUsSimulatorAlgo()
{
  SetImageCoordinateFrame(NULL);
  SetReferenceCoordinateFrame(NULL);
  if (this->RfProcessor != NULL)
  {
    this->RfProcessor->Delete();
    this->RfProcessor = NULL;
  }
  this->SetTransformRepository(NULL);
}

//-----------------------------------------------------------------------------
void vtkPlusUsSimulatorAlgo::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
int vtkPlusUsSimulatorAlgo::FillOutputPortInformation(int, vtkInformation* info)
{
  info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkImageData");

  return 1;
}

inline double fastPow(double a, double b)
{
  union
  {
    double d;
    int x[2];
  } u = { a };
  u.x[1] = (int)(b * (u.x[1] - 1072632447) + 1072632447);
  u.x[0] = 0;
  return u.d;
}

//-----------------------------------------------------------------------------
int vtkPlusUsSimulatorAlgo::RequestData(vtkInformation* request, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  if (this->TransformRepository == NULL)
  {
    LOG_ERROR("No transform repository is specified ");
    return 0;
  }

  // Get input
  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  for (std::vector<PlusSpatialModel>::iterator spatialModelIt = this->SpatialModels.begin(); spatialModelIt != this->SpatialModels.end(); ++spatialModelIt)
  {
    spatialModelIt->SetImagingFrequencyMhz(this->FrequencyMhz);
  }
  this->TransducerSpatialModel.SetImagingFrequencyMhz(this->FrequencyMhz);

  vtkSmartPointer<vtkImageData> scanLines = vtkSmartPointer<vtkImageData>::New(); // image data containing the scanlines in rows (FM orientation)
  scanLines->SetExtent(0, this->NumberOfSamplesPerScanline - 1, 0, this->NumberOfScanlines - 1, 0, 0);
  scanLines->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

  // Create BSPTree for fast scanline-model intersection computation
  vtkSmartPointer<vtkPoints> scanLineIntersectionWithModel = vtkSmartPointer<vtkPoints>::New();

  vtkPlusUsScanConvert* scanConverter = this->RfProcessor->GetScanConverter();
  if (scanConverter == NULL)
  {
    LOG_ERROR("ScanConverter is not defined");
    return 0;
  }
  // The input image extent has to be set before calling the scanConverter's
  // GetScanLineEndPoints or GetDistanceBetweenScanlineSamplePointsMm methods
  scanConverter->SetInputImageExtent(scanLines->GetExtent());

  double outputImageSpacingMm[3] = {1.0, 1.0, 1.0};
  scanConverter->GetOutputImageSpacing(outputImageSpacingMm);

  double distanceBetweenScanlineSamplePointsMm = scanConverter->GetDistanceBetweenScanlineSamplePointsMm();

  // Initialize noise generator
  vtkSmartPointer<vtkLineSource> noiseSamplerLine_Reference = vtkSmartPointer<vtkLineSource>::New();
  vtkSmartPointer<vtkPerlinNoise> noiseFunction = vtkSmartPointer<vtkPerlinNoise>::New();
  double samplePointPosition_Reference[3] = {0, 0, 0};
  if (this->NoiseAmplitude > 0)
  {
    noiseSamplerLine_Reference->SetResolution(this->NumberOfSamplesPerScanline - 1);
    noiseFunction->SetAmplitude(this->NoiseAmplitude);
    noiseFunction->SetFrequency(this->NoiseFrequency);
    noiseFunction->SetPhase(this->NoisePhase);
  }

  igsioTransformName imageToReferenceTransformName(this->GetImageCoordinateFrame(), this->GetReferenceCoordinateFrame());
  vtkSmartPointer<vtkMatrix4x4> imageToReferenceMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  if (this->TransformRepository->GetTransform(imageToReferenceTransformName, imageToReferenceMatrix) != PLUS_SUCCESS)
  {
    std::string strTransformName;
    imageToReferenceTransformName.GetTransformName(strTransformName);
    LOG_ERROR("Failed to get transform from repository: " << strTransformName);

    // Prevent crash in the pipeline by initializing output image
    vtkImageData* simulatedUsImage = vtkImageData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));
    if (simulatedUsImage == NULL)
    {
      LOG_ERROR("vtkPlusUsSimulatorAlgo output type is invalid");
      return 0;
    }
    simulatedUsImage->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

    return 0;
  }
  vtkSmartPointer<vtkMatrix4x4> referenceToImageMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  vtkMatrix4x4::Invert(imageToReferenceMatrix, referenceToImageMatrix);

  // Create a few variables outside the loop to avoid reallocations and to make the code easier to read
  vtkPoints* samplePointPositions_Reference = 0;
  // Create a buffer outside the for loop to allow reusing it
  std::vector<double> intensities;
  // scanline start/end positions in Image and Reference coordinate systems
  double scanLineStartPoint_Image[4] = {0, 0, 0, 1};
  double scanLineEndPoint_Image[4] = {0, 0, 0, 1};
  double scanLineStartPoint_Reference[4] = {0, 0, 0, 1};
  double scanLineEndPoint_Reference[4] = {0, 0, 0, 1};

  for (std::vector<PlusSpatialModel>::iterator spatialModelIt = this->SpatialModels.begin(); spatialModelIt != this->SpatialModels.end(); ++spatialModelIt)
  {
    vtkSmartPointer<vtkMatrix4x4> referenceToObjectMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    if (!spatialModelIt->GetObjectCoordinateFrame().empty())
    {
      igsioTransformName referenceToObjectTransformName(this->GetReferenceCoordinateFrame(), spatialModelIt->GetObjectCoordinateFrame());
      if (this->TransformRepository->GetTransform(referenceToObjectTransformName, referenceToObjectMatrix) != PLUS_SUCCESS)
      {
        std::string strTransformName;
        imageToReferenceTransformName.GetTransformName(strTransformName);
        LOG_ERROR("Error computing spatial position of " << spatialModelIt->GetName() << " SpatialModel: failed to get transform from repository: " << strTransformName);
      }
    }
    spatialModelIt->SetReferenceToObjectTransform(referenceToObjectMatrix);
  }

  for (int scanLineIndex = 0; scanLineIndex < this->NumberOfScanlines; scanLineIndex++)
  {
    scanConverter->GetScanLineEndPoints(scanLineIndex, scanLineStartPoint_Image, scanLineEndPoint_Image);
    imageToReferenceMatrix->MultiplyPoint(scanLineStartPoint_Image, scanLineStartPoint_Reference);
    imageToReferenceMatrix->MultiplyPoint(scanLineEndPoint_Image, scanLineEndPoint_Reference);

    if (this->NoiseAmplitude > 0)
    {
      noiseSamplerLine_Reference->SetPoint1(scanLineStartPoint_Reference);
      noiseSamplerLine_Reference->SetPoint2(scanLineEndPoint_Reference);
      noiseSamplerLine_Reference->Update();
      samplePointPositions_Reference = noiseSamplerLine_Reference->GetOutput()->GetPoints();
    }

    // Get model intersection positions along the scanline for all the models
    std::deque<PlusSpatialModel::LineIntersectionInfo> lineIntersectionsWithModels;
    for (std::vector<PlusSpatialModel>::iterator spatialModelIt = this->SpatialModels.begin(); spatialModelIt != this->SpatialModels.end(); ++spatialModelIt)
    {
      // Append line intersections found with this model to lineIntersectionsWithModels
      spatialModelIt->GetLineIntersections(lineIntersectionsWithModels, scanLineStartPoint_Reference, scanLineEndPoint_Reference);
    }

    ConvertLineModelIntersectionsToSegmentDescriptor(lineIntersectionsWithModels);

    int currentPixelIndex = 0;
    int scanLineExtent[6] = {0, this->NumberOfSamplesPerScanline - 1, scanLineIndex, scanLineIndex, 0, 0};
    unsigned char* dstPixelAddress = (unsigned char*)scanLines->GetScalarPointerForExtent(scanLineExtent);
    double incomingBeamIntensity = this->IncomingIntensityMwPerCm2 * 1000;
    int numIntersectionPoints = lineIntersectionsWithModels.size();
    if (numIntersectionPoints < 1)
    {
      LOG_ERROR("No intersections with any SpatialObjects. Probably no background object is specified.");
      return 0;
    }
    PlusSpatialModel* previousModel = &this->TransducerSpatialModel;
    for (vtkIdType intersectionIndex = 0; (intersectionIndex <= numIntersectionPoints) && (currentPixelIndex < this->NumberOfSamplesPerScanline); intersectionIndex++)
    {
      // determine end of segment position and pixel color
      int endOfSegmentPixelIndex = currentPixelIndex;
      double distanceOfIntersectionPointFromScanLineStartPointMm = 0; // defined here to allow for access later on in code
      if (intersectionIndex + 1 < numIntersectionPoints)
      {
        distanceOfIntersectionPointFromScanLineStartPointMm = lineIntersectionsWithModels[intersectionIndex + 1].IntersectionDistanceFromStartPointMm;
        endOfSegmentPixelIndex = distanceOfIntersectionPointFromScanLineStartPointMm / distanceBetweenScanlineSamplePointsMm;
        if (endOfSegmentPixelIndex > this->NumberOfSamplesPerScanline)
        {
          // the next intersection point is out of the image
          endOfSegmentPixelIndex = this->NumberOfSamplesPerScanline;
        }
      }
      else
      {
        // last segment, after all the intersection points
        endOfSegmentPixelIndex = this->NumberOfSamplesPerScanline;
      }

      int numberOfFilledPixels = endOfSegmentPixelIndex - currentPixelIndex;
      if (numberOfFilledPixels < 1)
      {
        continue;
      }

      PlusSpatialModel* currentModel = NULL;
      if (intersectionIndex < numIntersectionPoints)
      {
        currentModel = lineIntersectionsWithModels[intersectionIndex].Model;
      }
      else
      {
        // the segment after the last intersection point is assumed to belong to the model of the last intersection
        currentModel = lineIntersectionsWithModels[numIntersectionPoints - 1].Model;
      }

      double outgoingBeamIntensity = 0;
      currentModel->CalculateIntensity(intensities, numberOfFilledPixels, distanceBetweenScanlineSamplePointsMm, previousModel->GetAcousticImpedanceMegarayls(), incomingBeamIntensity, outgoingBeamIntensity, lineIntersectionsWithModels[intersectionIndex].IntersectionIncidenceAngleRad);
      previousModel = currentModel;

      if (this->NoiseAmplitude > 0)
      {
        for (int pixelIndex = 0; pixelIndex < numberOfFilledPixels; pixelIndex++)
        {
          samplePointPositions_Reference->GetPoint(currentPixelIndex + pixelIndex, samplePointPosition_Reference);
          double noise = noiseFunction->EvaluateFunction(samplePointPosition_Reference);
          // Noise is multiplicative: NoisySignal = signal + noise * (signal-SignalMean) = signal*(1+noise) - noise*SignalMean;
          (*dstPixelAddress++) = std::max(std::min(this->BrightnessConversionOffset + this->BrightnessConversionScale * fastPow(intensities[pixelIndex], this->BrightnessConversionGamma) + noise, 255.0), 0.0);
        }
      }
      else
      {
        for (int pixelIndex = 0; pixelIndex < numberOfFilledPixels; pixelIndex++)
        {
          (*dstPixelAddress++) = std::max(std::min(this->BrightnessConversionOffset + this->BrightnessConversionScale * fastPow(intensities[pixelIndex], this->BrightnessConversionGamma), 255.0), 0.0);
        }
      }

      incomingBeamIntensity = outgoingBeamIntensity;

      currentPixelIndex += numberOfFilledPixels;
    }
  }

  vtkImageData* simulatedUsImage = vtkImageData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));
  if (simulatedUsImage == NULL)
  {
    LOG_ERROR("vtkPlusUsSimulatorAlgo output type is invalid");
    return 0;
  }
  this->RfProcessor->SetRfFrame(scanLines, US_IMG_BRIGHTNESS);
  simulatedUsImage->DeepCopy(this->RfProcessor->GetBrightnessScanConvertedImage());
  return 1;
}

bool lineIntersectionLessThan(PlusSpatialModel::LineIntersectionInfo a, PlusSpatialModel::LineIntersectionInfo b)
{
  return a.IntersectionDistanceFromStartPointMm < b.IntersectionDistanceFromStartPointMm;
}

// In the input the "Model" in the line model intersections refers to the model that either starts or ends
// at the given intersection position (e.g., background/spine/spine).
// We overwrite the "Model" by the model that starts from that intersection position (e.g., background/spine/background).
//-----------------------------------------------------------------------------
void vtkPlusUsSimulatorAlgo::ConvertLineModelIntersectionsToSegmentDescriptor(std::deque<PlusSpatialModel::LineIntersectionInfo>& lineIntersectionsWithModels)
{
  // sort intersections based on the intersection distance
  std::sort(lineIntersectionsWithModels.begin(), lineIntersectionsWithModels.end(), lineIntersectionLessThan);

  // Cohesiveness defines how likely that the model contains another model.
  // If cohesiveness is low then the model likely to contain other models (e.g., the background material has the lowest cohesiveness value).
  // When we are in a segment that is inside multiple models, the model with the highest cohesiveness "overwrites" all the others.
  // e.g.,
  //  Cohesiveness values:
  //   background: cohesiveness = 0
  //   spine: cohesiveness = 1
  //   needle: cohesiveness = 2
  //  Decision:
  //   if we are in background+spine segment => it's spine
  //   if we are in background+spine+needle segment => it's needle
  // It is assumed that the SpatialModels are listed in the config file in increasing cohesiveness order (background is the first).
  std::map< PlusSpatialModel*, int > cohesiveness;
  for (unsigned int i = 0; i < this->SpatialModels.size(); i++)
  {
    cohesiveness[&this->SpatialModels[i]] = i;
  }
  // insideModel contains all the SpatialModels that the current segment is in; listed in descending order based on cohesiveness
  std::list<PlusSpatialModel*> insideModel;
  for (std::deque<PlusSpatialModel::LineIntersectionInfo>::iterator intersectionIt = lineIntersectionsWithModels.begin();
       intersectionIt != lineIntersectionsWithModels.end(); ++intersectionIt)
  {
    std::list<PlusSpatialModel*>::iterator foundThisModelAt = find(insideModel.begin(), insideModel.end(), intersectionIt->Model);
    if (foundThisModelAt != insideModel.end())
    {
      // we were already inside this model, so now we are out
      insideModel.erase(foundThisModelAt);
    }
    else
    {
      // we were not inside this model, so now we are in - need to put this model into the list
      int thisModelsCohesiveness = cohesiveness[intersectionIt->Model];
      std::list<PlusSpatialModel*>::iterator insertThisModelAt = insideModel.begin();
      while (insertThisModelAt != insideModel.end() && cohesiveness[*insertThisModelAt] > thisModelsCohesiveness)
      {
        ++insertThisModelAt;
      }
      insideModel.insert(insertThisModelAt, intersectionIt->Model);
    }
    if (insideModel.empty())
    {
      LOG_ERROR("No model is defined in one segment of the scanline");
    }
    else
    {
      intersectionIt->Model = insideModel.front();
    }
  }
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusUsSimulatorAlgo::ReadConfiguration(vtkXMLDataElement* config)
{
  LOG_TRACE("vtkPlusUsSimulatorVideoSource::ReadConfiguration");

  XML_FIND_NESTED_ELEMENT_REQUIRED(usSimulatorAlgoElement, config, "vtkPlusUsSimulatorAlgo");

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, NumberOfScanlines, usSimulatorAlgoElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, NumberOfSamplesPerScanline, usSimulatorAlgoElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, FrequencyMhz, usSimulatorAlgoElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, BrightnessConversionGamma, usSimulatorAlgoElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, BrightnessConversionOffset, usSimulatorAlgoElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, BrightnessConversionScale, usSimulatorAlgoElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, IncomingIntensityMwPerCm2, usSimulatorAlgoElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, NoiseAmplitude, usSimulatorAlgoElement);
  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 3, NoiseFrequency, usSimulatorAlgoElement);
  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 3, NoisePhase, usSimulatorAlgoElement);
  XML_READ_CSTRING_ATTRIBUTE_REQUIRED(ImageCoordinateFrame, usSimulatorAlgoElement);
  XML_READ_CSTRING_ATTRIBUTE_REQUIRED(ReferenceCoordinateFrame, usSimulatorAlgoElement);

  XML_FIND_NESTED_ELEMENT_REQUIRED(rfProcesingElement, usSimulatorAlgoElement, "RfProcessing");
  this->RfProcessor->ReadConfiguration(rfProcesingElement);

  this->SpatialModels.clear();
  for (int i = 0; i < usSimulatorAlgoElement->GetNumberOfNestedElements(); ++i)
  {
    vtkXMLDataElement* spatialModelElement = usSimulatorAlgoElement->GetNestedElement(i);
    if (STRCASECMP(spatialModelElement->GetName(), "SpatialModel") != 0)
    {
      continue;
    }
    PlusSpatialModel model;
    if (model.ReadConfiguration(spatialModelElement) != PLUS_SUCCESS)
    {
      return PLUS_FAIL;
    }
    this->SpatialModels.push_back(model);
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusUsSimulatorAlgo::GetFrameSize(FrameSizeType& frameSize)
{
  vtkPlusUsScanConvert* scanConverter = this->RfProcessor->GetScanConverter();
  if (scanConverter == NULL)
  {
    LOG_ERROR("Unable to determine frame size, ScanConverter is not defined");
    return PLUS_FAIL;
  }
  frameSize = scanConverter->GetOutputImageSizePixel();
  frameSize[2] = 1; // currently the simulator always provides 2D images
  return PLUS_SUCCESS;
}
