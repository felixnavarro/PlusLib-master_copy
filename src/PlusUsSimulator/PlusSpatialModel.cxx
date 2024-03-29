/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include "PlusSpatialModel.h"

#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkModifiedBSPTree.h"
#include "vtkObjectFactory.h"
#include "vtkSTLReader.h"
#include "vtkXMLPolyDataReader.h"
#include "vtkPolyDataNormals.h"
#include "vtkProbeFilter.h"
#include "vtkPointData.h"
#include "vtkIdList.h"
#include "vtkTriangle.h"

// If fraction of the transmitted beam intensity is smaller then this value then we consider the beam to be completely absorbed
const double MINIMUM_BEAM_INTENSITY = 1e-9;

// Characterizes the specular reflection BRDF. If the value is smaller then reflection is limited to a smaller angle range (closer to 90deg incidence angle).
double SPECULAR_REFLECTION_BRDF_STDEV = 30.0;

//-----------------------------------------------------------------------------
PlusSpatialModel::PlusSpatialModel()
  : Name("")
  , ModelFile("")
  , ModelFileNeedsUpdate(false)
  , ModelToObjectTransform(vtkMatrix4x4::New())
  , ReferenceToObjectTransform(vtkMatrix4x4::New())
  , ObjectCoordinateFrame("")
  , ImagingFrequencyMhz(5.0)
  , DensityKgPerM3(910)
  , SoundVelocityMPerSec(1540)
  , AttenuationCoefficientDbPerCmMhz(0.65)
  , SurfaceReflectionIntensityDecayDbPerMm(20)
  , BackscatterDiffuseReflectionCoefficient(0.1)
  , TransducerSpatialModelMaxOverlapMm(10.0)
  , SurfaceSpecularReflectionCoefficient(0.0)
  , SurfaceDiffuseReflectionCoefficient(0.1)
  , ModelLocalizer(vtkModifiedBSPTree::New())
  , PolyData(NULL)
{
}

//-----------------------------------------------------------------------------
PlusSpatialModel::~PlusSpatialModel()
{
  SetModelToObjectTransform(static_cast<vtkMatrix4x4*>(NULL));
  SetReferenceToObjectTransform(NULL);
  SetModelLocalizer(NULL);
  SetPolyData(NULL);
}

//-----------------------------------------------------------------------------
PlusSpatialModel::PlusSpatialModel(const PlusSpatialModel& model)
{
  this->Name = model.Name;
  this->ObjectCoordinateFrame = model.ObjectCoordinateFrame;
  this->ModelFile = model.ModelFile;
  this->ImagingFrequencyMhz = model.ImagingFrequencyMhz;
  this->DensityKgPerM3 = model.DensityKgPerM3;
  this->SoundVelocityMPerSec = model.SoundVelocityMPerSec;
  this->AttenuationCoefficientDbPerCmMhz = model.AttenuationCoefficientDbPerCmMhz;
  this->SurfaceReflectionIntensityDecayDbPerMm = model.SurfaceReflectionIntensityDecayDbPerMm;
  this->BackscatterDiffuseReflectionCoefficient = model.BackscatterDiffuseReflectionCoefficient;
  this->SurfaceDiffuseReflectionCoefficient = model.SurfaceDiffuseReflectionCoefficient;
  this->SurfaceSpecularReflectionCoefficient = model.SurfaceSpecularReflectionCoefficient;
  this->ModelToObjectTransform = NULL;
  this->ReferenceToObjectTransform = NULL;
  this->ModelLocalizer = NULL;
  this->PolyData = NULL;
  SetModelToObjectTransform(model.ModelToObjectTransform);
  SetReferenceToObjectTransform(model.ReferenceToObjectTransform);
  SetModelLocalizer(model.ModelLocalizer);
  SetPolyData(model.PolyData);
  this->ModelFileNeedsUpdate = model.ModelFileNeedsUpdate;
  this->PrecomputedAttenuations = model.PrecomputedAttenuations;
  this->TransducerSpatialModelMaxOverlapMm = model.TransducerSpatialModelMaxOverlapMm;
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::operator=(const PlusSpatialModel& model)
{
  this->Name = model.Name;
  this->ObjectCoordinateFrame = model.ObjectCoordinateFrame;
  this->ModelFile = model.ModelFile;
  this->ImagingFrequencyMhz = model.ImagingFrequencyMhz;
  this->DensityKgPerM3 = model.DensityKgPerM3;
  this->SoundVelocityMPerSec = model.SoundVelocityMPerSec;
  this->AttenuationCoefficientDbPerCmMhz = model.AttenuationCoefficientDbPerCmMhz;
  this->SurfaceReflectionIntensityDecayDbPerMm = model.SurfaceReflectionIntensityDecayDbPerMm;
  this->BackscatterDiffuseReflectionCoefficient = model.BackscatterDiffuseReflectionCoefficient;
  this->SurfaceDiffuseReflectionCoefficient = model.SurfaceDiffuseReflectionCoefficient;
  this->SurfaceSpecularReflectionCoefficient = model.SurfaceSpecularReflectionCoefficient;
  SetModelToObjectTransform(model.ModelToObjectTransform);
  SetReferenceToObjectTransform(model.ReferenceToObjectTransform);
  SetModelLocalizer(model.ModelLocalizer);
  SetPolyData(model.PolyData);
  this->ModelFileNeedsUpdate = model.ModelFileNeedsUpdate;
  this->PrecomputedAttenuations = model.PrecomputedAttenuations;
  this->TransducerSpatialModelMaxOverlapMm = model.TransducerSpatialModelMaxOverlapMm;
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::SetModelToObjectTransform(vtkMatrix4x4* modelToObjectTransform)
{
  if (this->ModelToObjectTransform == modelToObjectTransform)
  {
    return;
  }
  if (this->ModelToObjectTransform != NULL)
  {
    this->ModelToObjectTransform->Delete();
  }
  this->ModelToObjectTransform = modelToObjectTransform;
  if (this->ModelToObjectTransform != NULL)
  {
    this->ModelToObjectTransform->Register(NULL);
  }
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::SetReferenceToObjectTransform(vtkMatrix4x4* referenceToObjectTransform)
{
  if (this->ReferenceToObjectTransform == referenceToObjectTransform)
  {
    return;
  }
  if (this->ReferenceToObjectTransform != NULL)
  {
    this->ReferenceToObjectTransform->Delete();
  }
  this->ReferenceToObjectTransform = referenceToObjectTransform;
  if (this->ReferenceToObjectTransform != NULL)
  {
    this->ReferenceToObjectTransform->Register(NULL);
  }
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::SetPolyData(vtkPolyData* polyData)
{
  if (this->PolyData == polyData)
  {
    return;
  }
  if (this->PolyData != NULL)
  {
    this->PolyData->Delete();
  }
  this->PolyData = polyData;
  if (this->PolyData != NULL)
  {
    this->PolyData->Register(NULL);
  }
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::SetModelLocalizer(vtkModifiedBSPTree* modelLocalizer)
{
  if (this->ModelLocalizer == modelLocalizer)
  {
    return;
  }
  if (this->ModelLocalizer != NULL)
  {
    this->ModelLocalizer->Delete();
  }
  this->ModelLocalizer = modelLocalizer;
  if (this->ModelLocalizer != NULL)
  {
    this->ModelLocalizer->Register(NULL);
  }
}

//-----------------------------------------------------------------------------
PlusStatus PlusSpatialModel::ReadConfiguration(vtkXMLDataElement* spatialModelElement)
{
  XML_VERIFY_ELEMENT(spatialModelElement, "SpatialModel");

  XML_READ_STRING_ATTRIBUTE_OPTIONAL(Name, spatialModelElement);
  XML_READ_STRING_ATTRIBUTE_OPTIONAL(ObjectCoordinateFrame, spatialModelElement);
  XML_READ_STRING_ATTRIBUTE_OPTIONAL(ModelFile, spatialModelElement);
  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 16, ModelToObjectTransform, spatialModelElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, DensityKgPerM3, spatialModelElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, SoundVelocityMPerSec, spatialModelElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, AttenuationCoefficientDbPerCmMhz, spatialModelElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, SurfaceReflectionIntensityDecayDbPerMm, spatialModelElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, BackscatterDiffuseReflectionCoefficient, spatialModelElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, SurfaceDiffuseReflectionCoefficient, spatialModelElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, SurfaceSpecularReflectionCoefficient, spatialModelElement);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, TransducerSpatialModelMaxOverlapMm, spatialModelElement);

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
double PlusSpatialModel::GetAcousticImpedanceMegarayls()
{
  double acousticImpedanceRayls = this->DensityKgPerM3 * this->SoundVelocityMPerSec; // kg / (s * m2)
  return acousticImpedanceRayls * 1e-6; // megarayls
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::CalculateIntensity(std::vector<double>& reflectedIntensity, unsigned int numberOfFilledPixels, double distanceBetweenScanlineSamplePointsMm, double previousModelAcousticImpedanceMegarayls, double incidentIntensity, double& transmittedIntensity, double incidenceAngleRad)
{
  UpdateModelFile();

  if (numberOfFilledPixels <= 0)
  {
    transmittedIntensity = incidentIntensity;
    return;
  }

  // Make sure there is enough space to store the results
  if (reflectedIntensity.size() < numberOfFilledPixels)
  {
    reflectedIntensity.resize(numberOfFilledPixels);
  }

  // Compute reflection from the surface of the previous and this model
  double acousticImpedanceMegarayls = GetAcousticImpedanceMegarayls();
  // intensityReflectionCoefficient: reflected beam intensity / incident beam intensity => can be computed from acoustic impedance mismatch
  double intensityReflectionCoefficient =
    (previousModelAcousticImpedanceMegarayls - acousticImpedanceMegarayls) * (previousModelAcousticImpedanceMegarayls - acousticImpedanceMegarayls)
    / (previousModelAcousticImpedanceMegarayls + acousticImpedanceMegarayls) / (previousModelAcousticImpedanceMegarayls + acousticImpedanceMegarayls);
  double surfaceReflectedBeamIntensity = incidentIntensity * intensityReflectionCoefficient;
  double surfaceTransmittedBeamIntensity = incidentIntensity - surfaceReflectedBeamIntensity;

  // backscatteredReflectedIntensity: intensity reflected from the surface
  double backscatteredReflectedIntensity = this->SurfaceDiffuseReflectionCoefficient * surfaceReflectedBeamIntensity;;
  if (this->SurfaceSpecularReflectionCoefficient > 0)
  {
    // There is specular reflection
    // Normalize incidence angle to -90..90 deg
    if (incidenceAngleRad > vtkMath::Pi() / 2)
    {
      incidenceAngleRad -= vtkMath::Pi();
    }
    else if (incidenceAngleRad < -vtkMath::Pi() / 2)
    {
      incidenceAngleRad += vtkMath::Pi();
    }
    const double stdev = vtkMath::RadiansFromDegrees(SPECULAR_REFLECTION_BRDF_STDEV);
    float reflectionDirectionFactor = exp(-(incidenceAngleRad * incidenceAngleRad) / (stdev * stdev));
    backscatteredReflectedIntensity += reflectionDirectionFactor * this->SurfaceSpecularReflectionCoefficient * surfaceReflectedBeamIntensity;
  }

  // Compute attenuation within this model
  double intensityAttenuationCoefficientdBPerPixel = this->AttenuationCoefficientDbPerCmMhz * (distanceBetweenScanlineSamplePointsMm / 10.0) * this->ImagingFrequencyMhz;
  // intensityAttenuationCoefficientPerPixel: should be close to 1, as it's the ratio of (transmitted beam intensity / incident beam intensity) after traversing through a single pixel
  double intensityAttenuationCoefficientPerPixel = pow(10.0, -intensityAttenuationCoefficientdBPerPixel / 10.0);
  // intensityAttenuatedFractionPerPixel: how big fraction of the intensity is attenuated during traversing through one voxel
  double intensityAttenuatedFractionPerPixel = (1 - intensityAttenuationCoefficientPerPixel);
  // intensityTransmittedFractionPerPixelTwoWay: how big fraction of the intensity is transmitted during traversing through one voxel; takes into account both propagation directions
  double intensityTransmittedFractionPerPixelTwoWay = intensityAttenuationCoefficientPerPixel * intensityAttenuationCoefficientPerPixel;

  transmittedIntensity = surfaceTransmittedBeamIntensity * intensityTransmittedFractionPerPixelTwoWay;

  if ((numberOfFilledPixels > 0 && this->PrecomputedAttenuations.size() < numberOfFilledPixels) || intensityTransmittedFractionPerPixelTwoWay != this->PrecomputedAttenuations[0])
  {
    UpdatePrecomputedAttenuations(intensityTransmittedFractionPerPixelTwoWay, numberOfFilledPixels);
  }

  // We iterate until transmittedIntensity * intensityTransmittedFractionPerPixelTwoWay^n > MINIMUM_BEAM_INTENSITY
  // So, n = log(MINIMUM_BEAM_INTENSITY/transmittedIntensity) / log(intensityTransmittedFractionPerPixelTwoWay)
  unsigned int numberOfIterationsToReachMinimumBeamIntensity = 0;
  if (transmittedIntensity > MINIMUM_BEAM_INTENSITY)
  {
    numberOfIterationsToReachMinimumBeamIntensity =
      std::min<unsigned int>(numberOfFilledPixels,  // value may be larger than number of pixels to fill -> clamp it to the number of pixels to fill
                             static_cast<unsigned int>(std::max<int>(0,   // value may be negative when AttenuationCoefficientDbPerCmMhz is close to 0 -> clamp it to zero
                                 floor(log(MINIMUM_BEAM_INTENSITY / transmittedIntensity) / log(intensityTransmittedFractionPerPixelTwoWay)) + 1)));
    double backScatterFactor = transmittedIntensity * intensityAttenuatedFractionPerPixel * this->BackscatterDiffuseReflectionCoefficient / intensityTransmittedFractionPerPixelTwoWay;
    for (unsigned int currentPixelInFilledPixels = 0; currentPixelInFilledPixels < numberOfIterationsToReachMinimumBeamIntensity; currentPixelInFilledPixels++)
    {
      // a fraction of the attenuation is caused by backscattering, the backscattering is sensed by the transducer
      //reflectedIntensity[currentPixelInFilledPixels] = *(attenuation++) * backScatterFactor;
      reflectedIntensity[currentPixelInFilledPixels] = this->PrecomputedAttenuations[currentPixelInFilledPixels] * backScatterFactor;
    }
    transmittedIntensity *= pow(intensityTransmittedFractionPerPixelTwoWay, static_cast<double>(numberOfIterationsToReachMinimumBeamIntensity));
  }
  else
  {
    transmittedIntensity = 0;
  }
  // The beam intensity is very close to 0, so fill the remaining values with 0 instead of computing miniscule values
  for (unsigned int currentPixelInFilledPixels = numberOfIterationsToReachMinimumBeamIntensity; currentPixelInFilledPixels < numberOfFilledPixels; currentPixelInFilledPixels++)
  {
    reflectedIntensity[currentPixelInFilledPixels] = 0.0;
  }

  // Add surface reflection
  if (backscatteredReflectedIntensity > MINIMUM_BEAM_INTENSITY)
  {
    double surfaceReflectionIntensityDecayPerPixel = pow(10.0, -this->SurfaceReflectionIntensityDecayDbPerMm * distanceBetweenScanlineSamplePointsMm / 10.0);
    // We iterate until backscatteredReflectedIntensity * surfaceReflectionIntensityDecayPerPixel^n > MINIMUM_BEAM_INTENSITY
    // So, n = log(MINIMUM_BEAM_INTENSITY/backscatteredReflectedIntensity) / log(surfaceReflectionIntensityDecayPerPixel)
    int numberOfIterationsToReachMinimumBackscatteredIntensity = std::min<int>(numberOfFilledPixels, floor(log(MINIMUM_BEAM_INTENSITY / backscatteredReflectedIntensity) / log(surfaceReflectionIntensityDecayPerPixel)) + 1);
    for (int currentPixelInFilledPixels = 0; currentPixelInFilledPixels < numberOfIterationsToReachMinimumBackscatteredIntensity; currentPixelInFilledPixels++)
    {
      // a fraction of the attenuation is caused by backscattering, the backscattering is sensed by the transducer
      reflectedIntensity[currentPixelInFilledPixels] += backscatteredReflectedIntensity;
      backscatteredReflectedIntensity *= surfaceReflectionIntensityDecayPerPixel;
    }
  }
  // TODO: to simulate beamwidth, take into account the incidence angle and disperse the reflection on a larger area if the angle is large
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::GetLineIntersections(std::deque<LineIntersectionInfo>& lineIntersections, double* scanLineStartPoint_Reference, double* scanLineEndPoint_Reference)
{
  UpdateModelFile();

  if (this->ModelFile.empty())
  {
    // no model is defined, which means that the model is everywhere
    // add an intersection point at 0 distance, which means that the whole scanline is in this model
    LineIntersectionInfo intersectionInfo;
    intersectionInfo.Model = this;
    intersectionInfo.IntersectionIncidenceAngleRad = 0;
    intersectionInfo.IntersectionDistanceFromStartPointMm = 0;
    lineIntersections.push_back(intersectionInfo);
    return;
  }

  // non-normalized direction vector of the scanline
  double scanLineDirectionVector_Reference[4] =
  {
    scanLineEndPoint_Reference[0] - scanLineStartPoint_Reference[0],
    scanLineEndPoint_Reference[1] - scanLineStartPoint_Reference[1],
    scanLineEndPoint_Reference[2] - scanLineStartPoint_Reference[2],
    0
  };
  double scanLineDirectionVectorNorm_Reference = vtkMath::Norm(scanLineDirectionVector_Reference);
  double searchLineStartPoint_Reference[4] = {0, 0, 0, 1};
  for (int i = 0; i < 3; i++)
  {
    searchLineStartPoint_Reference[i] = scanLineStartPoint_Reference[i] - this->TransducerSpatialModelMaxOverlapMm * scanLineDirectionVector_Reference[i] / scanLineDirectionVectorNorm_Reference;
  }

  vtkSmartPointer<vtkMatrix4x4> objectToModelMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  vtkMatrix4x4::Invert(this->ModelToObjectTransform, objectToModelMatrix);
  vtkSmartPointer<vtkMatrix4x4> referenceToModelMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  vtkMatrix4x4::Multiply4x4(objectToModelMatrix, this->ReferenceToObjectTransform, referenceToModelMatrix);

  double searchLineStartPoint_Model[4] = {0, 0, 0, 1};
  double scanLineEndPoint_Model[4] = {0, 0, 0, 1};
  referenceToModelMatrix->MultiplyPoint(searchLineStartPoint_Reference, searchLineStartPoint_Model);
  referenceToModelMatrix->MultiplyPoint(scanLineEndPoint_Reference, scanLineEndPoint_Model);

  vtkSmartPointer<vtkPoints> intersectionPoints_Model = vtkSmartPointer<vtkPoints>::New();
  vtkSmartPointer<vtkIdList> intersectionCellIds = vtkSmartPointer<vtkIdList>::New();
  this->ModelLocalizer->IntersectWithLine(searchLineStartPoint_Model, scanLineEndPoint_Model, 0.0, intersectionPoints_Model, intersectionCellIds);

  if (intersectionPoints_Model->GetNumberOfPoints() < 1)
  {
    // no intersections with this model
    return;
  }

  vtkSmartPointer<vtkMatrix4x4> modelToReferenceMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  vtkMatrix4x4::Invert(referenceToModelMatrix, modelToReferenceMatrix);

  // Measure the distance from the starting point in the reference coordinate system
  double intersectionPoint_Model[4] = {0, 0, 0, 1};
  double intersectionPoint_Reference[4] = {0, 0, 0, 1};
  int intersectionPointIndex = 0;
  bool scanLineStartPointInsideModel = false;
  // Search for intersection points in the search line that are not part of the scanline to detect
  // potential model/transducer overlap
  for (; intersectionPointIndex < intersectionPoints_Model->GetNumberOfPoints(); intersectionPointIndex++)
  {
    intersectionPoints_Model->GetPoint(intersectionPointIndex, intersectionPoint_Model);
    modelToReferenceMatrix->MultiplyPoint(intersectionPoint_Model, intersectionPoint_Reference);
    double intersectionDistanceFromSearchLineStartPointMm = sqrt(vtkMath::Distance2BetweenPoints(searchLineStartPoint_Reference, intersectionPoint_Reference));
    if (intersectionDistanceFromSearchLineStartPointMm <= this->TransducerSpatialModelMaxOverlapMm)
    {
      // there is an intersection point in the search line that is not part of the scanline
      scanLineStartPointInsideModel = (!scanLineStartPointInsideModel);
    }
    else
    {
      // we reached the scanline starting point
      break;
    }
  }
  LineIntersectionInfo intersectionInfo;
  intersectionInfo.Model = this;
  if (scanLineStartPointInsideModel)
  {
    // the scanline starting point is inside the model, so add an intersection point at 0 distance
    intersectionInfo.IntersectionDistanceFromStartPointMm = 0;
    lineIntersections.push_back(intersectionInfo);
  }

  // Get surface normals at intersection points
  vtkDataArray* normals_Model = NULL;
  if (this->PolyData->GetPointData())
  {
    normals_Model = this->PolyData->GetPointData()->GetNormals();
  }

  double scanLineDirectionVector_Model[4] = {0, 0, 0, 0};
  referenceToModelMatrix->MultiplyPoint(scanLineDirectionVector_Reference, scanLineDirectionVector_Model);
  vtkMath::Normalize(scanLineDirectionVector_Model);

  for (; intersectionPointIndex < intersectionPoints_Model->GetNumberOfPoints(); intersectionPointIndex++)
  {
    intersectionPoints_Model->GetPoint(intersectionPointIndex, intersectionPoint_Model);
    modelToReferenceMatrix->MultiplyPoint(intersectionPoint_Model, intersectionPoint_Reference);
    intersectionInfo.IntersectionDistanceFromStartPointMm = sqrt(vtkMath::Distance2BetweenPoints(scanLineStartPoint_Reference, intersectionPoint_Reference));
    vtkTriangle* cell = vtkTriangle::SafeDownCast(this->PolyData->GetCell(intersectionCellIds->GetId(intersectionPointIndex)));
    if (cell != NULL && normals_Model != NULL)
    {
      const int NUMBER_OF_POINTS_PER_CELL = 3; // triangle cell
      double pcoords[NUMBER_OF_POINTS_PER_CELL] = {0, 0, 0};
      double dist2 = 0;
      double weights[NUMBER_OF_POINTS_PER_CELL] = {0, 0, 0};
      double closestPoint[3] = {0, 0, 0};
      int subId = 0;
      cell->EvaluatePosition(intersectionPoint_Model, closestPoint, subId, pcoords, dist2, weights);
      double interpolatedNormal_Model[3] = {0, 0, 0};
      for (int pointIndex = 0; pointIndex < NUMBER_OF_POINTS_PER_CELL; pointIndex++)
      {
        double* normalAtCellCorner = normals_Model->GetTuple3(cell->GetPointId(pointIndex));
        if (normalAtCellCorner == NULL)
        {
          LOG_ERROR("SpatialModel::GetLineIntersections error: invalid normal");
          continue;
        }
        interpolatedNormal_Model[0] += normalAtCellCorner[0] * weights[pointIndex];
        interpolatedNormal_Model[1] += normalAtCellCorner[1] * weights[pointIndex];
        interpolatedNormal_Model[2] += normalAtCellCorner[2] * weights[pointIndex];
      }
      vtkMath::Normalize(interpolatedNormal_Model);
      intersectionInfo.IntersectionIncidenceAngleRad = acos(vtkMath::Dot(interpolatedNormal_Model, scanLineDirectionVector_Model));
    }
    else
    {
      LOG_ERROR("SpatialModel::GetLineIntersections error: surface normal is not available");
      intersectionInfo.IntersectionIncidenceAngleRad = 0;
    }
    lineIntersections.push_back(intersectionInfo);
  }
}

//-----------------------------------------------------------------------------
PlusStatus PlusSpatialModel::UpdateModelFile()
{
  if (!this->ModelFileNeedsUpdate)
  {
    return PLUS_SUCCESS;
  }

  this->ModelFileNeedsUpdate = false;

  if (this->PolyData != NULL)
  {
    this->PolyData->Delete();
    this->PolyData = NULL;
  }

  if (this->ModelFile.empty())
  {
    LOG_DEBUG("ModelFileName is not specified for SpatialModel " << (this->Name.empty() ? "(undefined)" : this->Name) << " it will be used as background media");
    return PLUS_SUCCESS;
  }

  std::string foundAbsoluteImagePath;
  // FindImagePath is used instead of FindModelPath, as the model is expected to be in the image directory
  // it might be more reasonable to move the model to the model directory and change this to FindModelPath
  if (vtkPlusConfig::GetInstance()->FindImagePath(this->ModelFile, foundAbsoluteImagePath) != PLUS_SUCCESS)
  {
    LOG_ERROR("Cannot find input model file " << this->ModelFile);
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkPolyData> polyData;

  std::string fileExt = vtksys::SystemTools::GetFilenameLastExtension(foundAbsoluteImagePath);
  if (igsioCommon::IsEqualInsensitive(fileExt, ".stl"))
  {
    vtkSmartPointer<vtkSTLReader> modelReader = vtkSmartPointer<vtkSTLReader>::New();
    modelReader->SetFileName(foundAbsoluteImagePath.c_str());
    modelReader->Update();
    polyData = modelReader->GetOutput();
  }
  else //if (igsioCommon::IsEqualInsensitive(fileExt.c_str(),".vtp"))
  {
    vtkSmartPointer<vtkXMLPolyDataReader> modelReader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
    modelReader->SetFileName(foundAbsoluteImagePath.c_str());
    modelReader->Update();
    polyData = modelReader->GetOutput();
  }

  if (polyData.GetPointer() == NULL || polyData->GetNumberOfPoints() == 0)
  {
    LOG_ERROR("Model specified cannot be found: " << foundAbsoluteImagePath);
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkPolyDataNormals> polyDataNormalsComputer = vtkSmartPointer<vtkPolyDataNormals>::New();
  polyDataNormalsComputer->SetInputData(polyData);
  polyDataNormalsComputer->Update();
  this->PolyData = polyDataNormalsComputer->GetOutput();
  this->PolyData->Register(NULL);

  this->ModelLocalizer->SetDataSet(this->PolyData);
  this->ModelLocalizer->SetMaxLevel(24);
  this->ModelLocalizer->SetNumberOfCellsPerNode(32);
  this->ModelLocalizer->BuildLocator();

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::SetModelFile(const std::string& modelFile)
{
  if (this->ModelFile.compare(modelFile) != 0)
  {
    this->ModelFileNeedsUpdate = true;
  }
  this->ModelFile = modelFile;
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::UpdatePrecomputedAttenuations(double intensityTransmittedFractionPerPixelTwoWay, int numberOfElements)
{
  this->PrecomputedAttenuations.resize(numberOfElements);
  double attenuation = intensityTransmittedFractionPerPixelTwoWay;
  for (int i = 0; i < numberOfElements; i++)
  {
    this->PrecomputedAttenuations[i] = attenuation;
    attenuation *= intensityTransmittedFractionPerPixelTwoWay;
  }
}

//-----------------------------------------------------------------------------
void PlusSpatialModel::SetModelToObjectTransform(double* matrixElements)
{
  this->ModelToObjectTransform->DeepCopy(matrixElements);
}