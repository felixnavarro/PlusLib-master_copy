/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include "vtkObjectFactory.h"
#include "vtkTransform.h"
#include "vtkMath.h"
#include "vtkDoubleArray.h"
#include "vtkTable.h"
#include "vtkPCAStatistics.h"

#include "vtkPlusPrincipalMotionDetectionAlgo.h"
#include "vtkIGSIOTransformRepository.h"
#include "vtkIGSIOTrackedFrameList.h"
#include "igsioTrackedFrame.h"

//-----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPlusPrincipalMotionDetectionAlgo);

//-----------------------------------------------------------------------------
vtkPlusPrincipalMotionDetectionAlgo::vtkPlusPrincipalMotionDetectionAlgo()
{
  m_SignalTimeRangeMin = 0.0;
  m_SignalTimeRangeMax = -1.0;
}

//-----------------------------------------------------------------------------
vtkPlusPrincipalMotionDetectionAlgo::~vtkPlusPrincipalMotionDetectionAlgo()
{
}

//----------------------------------------------------------------------------
void vtkPlusPrincipalMotionDetectionAlgo::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
void vtkPlusPrincipalMotionDetectionAlgo::SetTrackerFrames(vtkIGSIOTrackedFrameList* trackerFrames)
{
  m_TrackerFrames = trackerFrames;
}

//-----------------------------------------------------------------------------
void vtkPlusPrincipalMotionDetectionAlgo::SetSignalTimeRange(double rangeMin, double rangeMax)
{
  m_SignalTimeRangeMin = rangeMin;
  m_SignalTimeRangeMax = rangeMax;
}

//-----------------------------------------------------------------------------
void vtkPlusPrincipalMotionDetectionAlgo::SetProbeToReferenceTransformName(const std::string& transformName)
{
  m_ProbeToReferenceTransformName = transformName;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusPrincipalMotionDetectionAlgo::VerifyInputFrames()
{
  if (m_TrackerFrames == NULL)
  {
    LOG_ERROR("Tracker input data verification failed: no tracker data is set");
    return PLUS_FAIL;
  }
  // Make sure tracker frame list is not empty
  if (m_TrackerFrames->GetNumberOfTrackedFrames() == 0)
  {
    LOG_ERROR("Tracker input data verification failed: tracker data set is empty");
    return PLUS_FAIL;
  }
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusPrincipalMotionDetectionAlgo::ComputeTrackerPositionMetric()
{
  vtkSmartPointer<vtkIGSIOTransformRepository> transformRepository = vtkSmartPointer<vtkIGSIOTransformRepository>::New();
  igsioTransformName transformName;

  if (transformName.SetTransformName(m_ProbeToReferenceTransformName.c_str()) != PLUS_SUCCESS)
  {
    LOG_ERROR("Cannot compute tracker position metric, transform name is invalid (" << m_ProbeToReferenceTransformName << ")");
    return PLUS_FAIL;
  }

  // Clear the old tracker timestamps, preparing for an update
  m_SignalTimestamps.clear();
  m_SignalValues.clear();

  // Find the mean tracker position
  itk::Point<double, 3> trackerPositionSum;
  trackerPositionSum[0] = trackerPositionSum[1] = trackerPositionSum[2] = 0.0;
  std::deque<itk::Point<double, 3> > trackerPositions;
  int numberOfValidFrames = 0;
  bool signalTimeRangeDefined = (m_SignalTimeRangeMin <= m_SignalTimeRangeMax);
  for (unsigned int frame = 0; frame < m_TrackerFrames->GetNumberOfTrackedFrames(); ++frame)
  {
    igsioTrackedFrame* trackedFrame = m_TrackerFrames->GetTrackedFrame(frame);

    if (signalTimeRangeDefined && (trackedFrame->GetTimestamp() < m_SignalTimeRangeMin || trackedFrame->GetTimestamp() > m_SignalTimeRangeMax))
    {
      // frame is out of the specified signal range
      continue;
    }

    transformRepository->SetTransforms(*trackedFrame);


    vtkSmartPointer<vtkMatrix4x4> probeToReferenceTransform = vtkSmartPointer<vtkMatrix4x4>::New();
    ToolStatus status(TOOL_INVALID);
    transformRepository->GetTransform(transformName, probeToReferenceTransform, &status);
    if (status != TOOL_OK)
    {
      // There is no available transform for this frame; skip that frame
      continue;
    }

    //  Store current tracker position
    itk::Point<double, 3> currTrackerPosition;
    currTrackerPosition[0] = probeToReferenceTransform->GetElement(0, 3);
    currTrackerPosition[1] = probeToReferenceTransform->GetElement(1, 3);
    currTrackerPosition[2] = probeToReferenceTransform->GetElement(2, 3);
    trackerPositions.push_back(currTrackerPosition);

    // Add current tracker position to the running total
    trackerPositionSum[0] = trackerPositionSum[0] + probeToReferenceTransform->GetElement(0, 3);
    trackerPositionSum[1] = trackerPositionSum[1] + probeToReferenceTransform->GetElement(1, 3);
    trackerPositionSum[2] = trackerPositionSum[2] + probeToReferenceTransform->GetElement(2, 3);
    ++numberOfValidFrames;

    m_SignalTimestamps.push_back(trackedFrame->GetTimestamp());   // These timestamps will be in the desired time range
  }

  // Calculate the principal axis of motion (using PCA)
  itk::Point<double, 3> principalAxisOfMotion;
  ComputePrincipalAxis(trackerPositions, principalAxisOfMotion, numberOfValidFrames);

  // Compute the mean tracker position
  itk::Point<double, 3> meanTrackerPosition;
  meanTrackerPosition[0] = (trackerPositionSum[0] / static_cast<double>(numberOfValidFrames));
  meanTrackerPosition[1] = (trackerPositionSum[1] / static_cast<double>(numberOfValidFrames));
  meanTrackerPosition[2] = (trackerPositionSum[2] / static_cast<double>(numberOfValidFrames));

  //  For each tracker position in the recorded tracker sequence, get its translation from reference.
  for (unsigned int frame = 0; frame < m_SignalTimestamps.size(); ++frame)
  {
    // Project the current tracker position onto the principal axis of motion
    double currTrackerPositionProjection = trackerPositions.at(frame).GetElement(0) * principalAxisOfMotion[0]
                                           + trackerPositions.at(frame).GetElement(1) * principalAxisOfMotion[1]
                                           + trackerPositions.at(frame).GetElement(2) * principalAxisOfMotion[2];

    //  Store this translation and corresponding timestamp
    m_SignalValues.push_back(currTrackerPositionProjection);
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void vtkPlusPrincipalMotionDetectionAlgo::ComputePrincipalAxis(std::deque<itk::Point<double, 3> >& trackerPositions, itk::Point<double, 3>& principalAxisOfMotion, int numValidFrames)
{
  // Set the X-values
  const char m0Name[] = "M0";
  vtkSmartPointer<vtkDoubleArray> dataset1Arr = vtkSmartPointer<vtkDoubleArray>::New();
  dataset1Arr->SetNumberOfComponents(1);
  dataset1Arr->SetName(m0Name);
  for (int i = 0; i < numValidFrames; ++i)
  {
    dataset1Arr->InsertNextValue(trackerPositions[i].GetElement(0));
  }

  // Set the Y-values
  const char m1Name[] = "M1";
  vtkSmartPointer<vtkDoubleArray> dataset2Arr = vtkSmartPointer<vtkDoubleArray>::New();
  dataset2Arr->SetNumberOfComponents(1);
  dataset2Arr->SetName(m1Name);
  for (int i = 0; i < numValidFrames; ++i)
  {
    dataset2Arr->InsertNextValue(trackerPositions[i].GetElement(1));
  }

  // Set the Z-values
  const char m2Name[] = "M2";
  vtkSmartPointer<vtkDoubleArray> dataset3Arr = vtkSmartPointer<vtkDoubleArray>::New();
  dataset3Arr->SetNumberOfComponents(1);
  dataset3Arr->SetName(m2Name);
  for (int i = 0; i < numValidFrames; ++i)
  {
    dataset3Arr->InsertNextValue(trackerPositions[i].GetElement(2));
  }

  vtkSmartPointer<vtkTable> datasetTable = vtkSmartPointer<vtkTable>::New();
  datasetTable->AddColumn(dataset1Arr);
  datasetTable->AddColumn(dataset2Arr);
  datasetTable->AddColumn(dataset3Arr);

  vtkSmartPointer<vtkPCAStatistics> pcaStatistics = vtkSmartPointer<vtkPCAStatistics>::New();

  pcaStatistics->SetInputData(vtkStatisticsAlgorithm::INPUT_DATA, datasetTable);

  pcaStatistics->SetColumnStatus("M0", 1);
  pcaStatistics->SetColumnStatus("M1", 1);
  pcaStatistics->SetColumnStatus("M2", 1);
  pcaStatistics->RequestSelectedColumns();
  pcaStatistics->SetDeriveOption(true);
  pcaStatistics->Update();

  // Get the eigenvector corresponding to the largest eigenvalue (i.e. the principal axis). The
  // eigenvectors are stored with the eigenvector corresponding to the largest eigenvalue stored
  // first (i.e. in the "zero" position) and the eigenvector corresponding to the smallest eigenvalue
  // stored last.
  vtkSmartPointer<vtkDoubleArray> eigenvector = vtkSmartPointer<vtkDoubleArray>::New();
  pcaStatistics->GetEigenvector(0, eigenvector);

  for (int i = 0; i < eigenvector->GetNumberOfComponents(); ++i)
  {
    principalAxisOfMotion[i] = eigenvector->GetComponent(0, i);
  }
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusPrincipalMotionDetectionAlgo::Update()
{
  if (VerifyInputFrames() != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }
  if (ComputeTrackerPositionMetric() != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void vtkPlusPrincipalMotionDetectionAlgo::GetDetectedTimestamps(std::deque<double>& timestamps)
{
  timestamps = m_SignalTimestamps;
}

//-----------------------------------------------------------------------------
void vtkPlusPrincipalMotionDetectionAlgo::GetDetectedPositions(std::deque<double>& positions)
{
  positions = m_SignalValues;
}
