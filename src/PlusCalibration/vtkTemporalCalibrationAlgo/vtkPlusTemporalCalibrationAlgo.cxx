/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "igsioTrackedFrame.h"
#include "vtkObjectFactory.h"
#include "vtkDoubleArray.h"
#include "vtkPlusLineSegmentationAlgo.h"
#include "vtkMath.h"
#include "vtkPiecewiseFunction.h"
#include "vtkPlusPrincipalMotionDetectionAlgo.h"
#include "vtkTable.h"
#include "vtkPlusTemporalCalibrationAlgo.h"
#include "vtkIGSIOTrackedFrameList.h"
#include <algorithm>
#include <fstream>
#include <iostream>

//-----------------------------------------------------------------------------

vtkStandardNewMacro(vtkPlusTemporalCalibrationAlgo);

//-----------------------------------------------------------------------------
// Default algorithm parameters
namespace
{
  // If the tracker metric "swings" less than this, abort
  const double MINIMUM_TRACKER_SIGNAL_PEAK_TO_PEAK_MM = 8.0;
  // If the video metric "swings" less than this, abort
  const double MINIMUM_VIDEO_SIGNAL_PEAK_TO_PEAK_PIXEL = 30.0;
  // Temporal resolution below which two time values are considered identical
  const double TIMESTAMP_EPSILON_SEC = 0.0001;
  // The maximum resolution that the user can request
  const double MINIMUM_SAMPLING_RESOLUTION_SEC = 0.00001;
  const double DEFAULT_SAMPLING_RESOLUTION_SEC = 0.001;
  const double DEFAULT_MAX_MOVING_LAG_SEC = 0.5;

  enum SignalAlignmentMetricType
  {
    SSD,
    CORRELATION,
    SAD,
    SIGNAL_METRIC_TYPE_COUNT
  };
  const double SIGNAL_ALIGNMENT_METRIC_THRESHOLD[SIGNAL_METRIC_TYPE_COUNT] =
  {
    -2 ^ 500,
    -2 ^ 500,
    2 ^ 500
  };
  SignalAlignmentMetricType SIGNAL_ALIGNMENT_METRIC = SSD;

  enum MetricNormalizationType
  {
    STD,
    AMPLITUDE
  };
  MetricNormalizationType METRIC_NORMALIZATION = STD;
}

//-----------------------------------------------------------------------------
vtkPlusTemporalCalibrationAlgo::vtkPlusTemporalCalibrationAlgo()
  : TrackerLagUpToDate(false)
  ,  NeverUpdated(true)
  , SaveIntermediateImages(false)
  , IntermediateFilesOutputDirectory(vtkPlusConfig::GetInstance()->GetOutputDirectory())
  , SamplingResolutionSec(DEFAULT_SAMPLING_RESOLUTION_SEC)
  , BestCorrelationValue(0.0)
  , BestCorrelationLagIndex(-1)
  , BestCorrelationTimeOffset(0.0)
  , MovingLagSec(0.0)
  , CalibrationError(0.0)
  , MaxCalibrationError(0.0)
  , MaxMovingLagSec(DEFAULT_MAX_MOVING_LAG_SEC)
  , BestCorrelationNormalizationFactor(0.0)
  , FixedSignalValuesNormalizationFactor(0.0)
{
  this->FixedSignal.frameList = NULL;
  this->MovingSignal.frameList = NULL;
  this->LineSegmentationClipRectangleOrigin[0] = 0;
  this->LineSegmentationClipRectangleOrigin[1] = 0;
  this->LineSegmentationClipRectangleSize[0] = 0;
  this->LineSegmentationClipRectangleSize[1] = 0;
  this->IntermediateFilesOutputDirectory = vtkPlusConfig::GetInstance()->GetOutputDirectory();
}

//-----------------------------------------------------------------------------
vtkPlusTemporalCalibrationAlgo::~vtkPlusTemporalCalibrationAlgo()
{
  if (this->FixedSignal.frameList != NULL)
  {
    this->FixedSignal.frameList->UnRegister(NULL);
    this->FixedSignal.frameList = NULL;
  }
  if (this->MovingSignal.frameList != NULL)
  {
    this->MovingSignal.frameList->UnRegister(NULL);
    this->MovingSignal.frameList = NULL;
  }
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::Update(TEMPORAL_CALIBRATION_ERROR& error)
{
  if (ComputeMovingSignalLagSec(error) != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }
  error = TEMPORAL_CALIBRATION_ERROR_NONE;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetSaveIntermediateImages(bool saveIntermediateImages)
{
  this->SaveIntermediateImages = saveIntermediateImages;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetFixedFrames(vtkIGSIOTrackedFrameList* frameList, FRAME_TYPE frameType)
{
  if (frameList != this->FixedSignal.frameList)
  {
    if (this->FixedSignal.frameList != NULL)
    {
      this->FixedSignal.frameList->UnRegister(NULL);
      this->FixedSignal.frameList = NULL;
    }
    this->FixedSignal.frameList = frameList;
    this->FixedSignal.frameList->Register(NULL);
  }
  this->FixedSignal.frameType = frameType;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetFixedProbeToReferenceTransformName(const std::string& probeToReferenceTransformName)
{
  this->FixedSignal.probeToReferenceTransformName = probeToReferenceTransformName;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetMovingFrames(vtkIGSIOTrackedFrameList* frameList, FRAME_TYPE frameType)
{
  if (frameList != this->MovingSignal.frameList)
  {
    if (this->MovingSignal.frameList != NULL)
    {
      this->MovingSignal.frameList->UnRegister(NULL);
      this->MovingSignal.frameList = NULL;
    }
    this->MovingSignal.frameList = frameList;
    this->MovingSignal.frameList->Register(NULL);
  }
  this->MovingSignal.frameType = frameType;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetMovingProbeToReferenceTransformName(const std::string& probeToReferenceTransformName)
{
  this->MovingSignal.probeToReferenceTransformName = probeToReferenceTransformName;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetSamplingResolutionSec(double samplingResolutionSec)
{
  if (samplingResolutionSec < MINIMUM_SAMPLING_RESOLUTION_SEC)
  {
    LOG_ERROR("Specified resampling resolution (" << samplingResolutionSec << " seconds) is too small. Sampling resolution must be greater than: " << MINIMUM_SAMPLING_RESOLUTION_SEC << " seconds");
    return;
  }
  this->SamplingResolutionSec = samplingResolutionSec;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetMaximumMovingLagSec(double maxLagSec)
{
  this->MaxMovingLagSec = maxLagSec;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetIntermediateFilesOutputDirectory(const std::string& outputDirectory)
{
  this->IntermediateFilesOutputDirectory = outputDirectory;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetMovingLagSec(double& lag)
{
  if (this->NeverUpdated)
  {
    LOG_ERROR("You must first call the \"Update()\" to compute the tracker lag.");
    return PLUS_FAIL;
  }
  lag = this->MovingLagSec;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetBestCorrelation(double& videoCorrelation)
{
  if (this->NeverUpdated)
  {
    LOG_ERROR("You must first call the \"Update()\" to compute the best correlation metric.");
    return PLUS_FAIL;
  }
  videoCorrelation = this->BestCorrelationValue;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetCalibrationError(double& error)
{
  if (this->NeverUpdated)
  {
    LOG_ERROR("You must first call the \"Update()\" to compute the calibration error.");
    return PLUS_FAIL;
  }
  error = this->CalibrationError;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetMaxCalibrationError(double& maxCalibrationError)
{
  if (this->NeverUpdated)
  {
    LOG_ERROR("You must first call the \"Update()\" to compute the calibration error.");
    return PLUS_FAIL;
  }
  maxCalibrationError = this->MaxCalibrationError;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetUncalibratedMovingPositionSignal(vtkTable* unCalibratedMovingPositionSignal)
{
  ConstructTableSignal(this->MovingSignal.normalizedSignalTimestamps, this->MovingSignal.normalizedSignalValues, unCalibratedMovingPositionSignal, 0);
  if (unCalibratedMovingPositionSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold moving signal. Table has " <<
              unCalibratedMovingPositionSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }
  unCalibratedMovingPositionSignal->GetColumn(0)->SetName("Time [s]");
  unCalibratedMovingPositionSignal->GetColumn(1)->SetName("Uncalibrated Moving Signal");
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetCalibratedMovingPositionSignal(vtkTable* calibratedMovingPositionSignal)
{
  ConstructTableSignal(this->MovingSignal.normalizedSignalTimestamps, this->MovingSignal.normalizedSignalValues, calibratedMovingPositionSignal, -this->MovingLagSec);
  if (calibratedMovingPositionSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold moving signal. Table has " <<
              calibratedMovingPositionSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }
  calibratedMovingPositionSignal->GetColumn(0)->SetName("Time [s]");
  calibratedMovingPositionSignal->GetColumn(1)->SetName("Calibrated Moving Signal");
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetFixedPositionSignal(vtkTable* fixedPositionSignal)
{
  ConstructTableSignal(this->FixedSignal.signalTimestamps, this->FixedSignal.signalValues, fixedPositionSignal, 0);
  if (fixedPositionSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold fixed signal. Table has " <<
              fixedPositionSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }
  fixedPositionSignal->GetColumn(0)->SetName("Time [s]");
  fixedPositionSignal->GetColumn(1)->SetName("Fixed Signal");
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetCorrelationSignal(vtkTable* correlationSignal)
{
  ConstructTableSignal(this->CorrelationTimeOffsets, this->CorrelationValues, correlationSignal, 0);
  if (correlationSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold correlated signal. Table has " <<
              correlationSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }
  correlationSignal->GetColumn(0)->SetName("Time Offset [s]");
  correlationSignal->GetColumn(1)->SetName("Computed Correlation");
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetCorrelationSignalFine(vtkTable* correlationSignal)
{
  ConstructTableSignal(this->CorrelationTimeOffsetsFine, this->CorrelationValuesFine, correlationSignal, 0);
  if (correlationSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold fine correlated signal. Table has " <<
              correlationSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }
  correlationSignal->GetColumn(0)->SetName("Time Offset [s]");
  correlationSignal->GetColumn(1)->SetName("Computed Correlation (with fine step size)");
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::GetSignalRange(const std::deque<double>& signal, int startIndex, int stopIndex,  double& minValue, double& maxValue)
{
  if (signal.empty())
  {
    LOG_ERROR("Cannot get signal range, the signal is empty");
    return PLUS_FAIL;
  }
  //  Calculate maximum and minimum metric values
  maxValue = *(std::max_element(signal.begin() + startIndex, signal.begin() + stopIndex));
  minValue = *(std::min_element(signal.begin() + startIndex, signal.begin() + stopIndex));

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::NormalizeMetricValues(std::deque<double>& signal, double& normalizationFactor, int startIndex/*=0*/, int stopIndex/*=-1*/)
{
  if (signal.size() == 0)
  {
    LOG_ERROR("NormalizeMetricValues failed because the metric vector is empty");
    return PLUS_FAIL;
  }

  if (stopIndex < 0)
  {
    stopIndex = signal.size() - 1;
  }

  // Calculate the signal mean
  double mu = 0;
  for (int i = startIndex; i <= stopIndex; ++i)
  {
    mu += signal.at(i);
  }
  mu /= (stopIndex - startIndex + 1);

  // Calculate the signal "amplitude" and use its inverse as normalizationFactor
  normalizationFactor = 1.0;
  switch (METRIC_NORMALIZATION)
  {
    case AMPLITUDE:
      {
        // Divide by the maximum signal amplitude
        double minValue = 0;
        double maxValue = 0;
        GetSignalRange(signal, startIndex, stopIndex, minValue, maxValue);
        double maxPeakToPeak = fabs(maxValue - minValue);
        if (maxPeakToPeak < 1e-10)
        {
          LOG_ERROR("Cannot normalize data, peak to peak difference is too small");
        }
        else
        {
          normalizationFactor = 1.0 / maxPeakToPeak;
        }
        break;
      }
    case STD:
      {
        // Calculate standard deviation
        double stdev = 0;
        for (int i = startIndex; i <= stopIndex; ++i)
        {
          stdev += (signal.at(i) - mu) * (signal.at(i) - mu);
        }
        stdev = std::sqrt(stdev);
        stdev /= std::sqrt(static_cast<double>(stopIndex - startIndex + 1) - 1);

        if (stdev < 1e-10)
        {
          LOG_ERROR("Cannot normalize data, stdev is too small");
        }
        else
        {
          normalizationFactor = 1.0 / stdev;
        }
        break;
      }
  }

  // Normalize the signal values
  for (unsigned int i = 0; i < signal.size(); ++i)
  {
    signal.at(i) = (signal.at(i) - mu) * normalizationFactor;
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::NormalizeMetricValues(std::deque<double>& signal, double& normalizationFactor, double startTime, double stopTime, const std::deque<double>& timestamps)
{
  if (timestamps.size() == 0)
  {
    LOG_ERROR("NormalizeMetricValues failed because the metric vector is empty");
    return PLUS_FAIL;
  }

  int startIndex = 0;
  for (unsigned int i = 0; i < timestamps.size(); ++i)
  {
    double t = timestamps.at(i);
    if (t >= startTime)
    {
      startIndex = i;
      break;
    }
  }

  int stopIndex = timestamps.size() - 1;
  for (unsigned int i = timestamps.size() - 1; i != 0; --i)
  {
    double t = timestamps.at(i);
    if (t <= stopTime)
    {
      stopIndex = i;
      break;
    }
  }

  return NormalizeMetricValues(signal, normalizationFactor, startIndex, stopIndex);
}


//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::ResampleSignalLinearly(const std::deque<double>& templateSignalTimestamps,
    const vtkSmartPointer<vtkPiecewiseFunction>& signalFunction, std::deque<double>& resampledSignalValues)
{
  resampledSignalValues.resize(templateSignalTimestamps.size());
  for (unsigned int i = 0; i < templateSignalTimestamps.size(); ++i)
  {
    resampledSignalValues[i] = signalFunction->GetValue(templateSignalTimestamps[i]);
  }
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::ComputeCorrelationBetweenFixedAndMovingSignal(double minTrackerLagSec, double maxTrackerLagSec, double stepSizeSec, double& bestCorrelationValue, double& bestCorrelationTimeOffset, double& bestCorrelationNormalizationFactor, std::deque<double>& corrTimeOffsets, std::deque<double>& corrValues)
{
  // We will let the tracker metric be the "sliding" metric and let the video metric be the "fixed" metric. Since we are assuming a maximum offset between the two streams.

  // Construct piecewise function for tracker signal
  vtkSmartPointer<vtkPiecewiseFunction> trackerPositionPiecewiseSignal = vtkSmartPointer<vtkPiecewiseFunction>::New();
  double midpoint = 0.5;
  double sharpness = 0;
  for (unsigned int i = 0; i < this->MovingSignal.signalTimestamps.size(); ++i)
  {
    trackerPositionPiecewiseSignal->AddPoint(this->MovingSignal.signalTimestamps.at(i), this->MovingSignal.signalValues.at(i), midpoint, sharpness);
  }

  // Compute alignment metric for each offset
  std::deque<double> normalizationFactors;
  if (stepSizeSec < TIMESTAMP_EPSILON_SEC)
  {
    LOG_ERROR("Sampling resolution is too small: " << stepSizeSec << " sec");
    return;
  }
  std::deque<double> slidingSignalTimestamps(this->FixedSignal.signalTimestamps.size());
  std::deque<double> resampledTrackerPositionMetric;
  corrValues.clear();
  corrTimeOffsets.clear();
  for (double offsetValueSec = minTrackerLagSec; offsetValueSec <= maxTrackerLagSec; offsetValueSec += stepSizeSec)
  {
    //LOG_DEBUG("offsetValueSec = " << offsetValueSec);
    corrTimeOffsets.push_back(offsetValueSec);
    for (unsigned int i = 0; i < slidingSignalTimestamps.size(); ++i)
    {
      slidingSignalTimestamps.at(i) =  this->FixedSignal.signalTimestamps.at(i) + offsetValueSec;
    }

    NormalizeMetricValues(this->FixedSignal.signalValues, this->FixedSignalValuesNormalizationFactor, slidingSignalTimestamps.front(), slidingSignalTimestamps.back(), this->FixedSignal.signalTimestamps);

    ResampleSignalLinearly(slidingSignalTimestamps, trackerPositionPiecewiseSignal, resampledTrackerPositionMetric);
    double normalizationFactor = 1.0;
    NormalizeMetricValues(resampledTrackerPositionMetric, normalizationFactor);
    normalizationFactors.push_back(normalizationFactor);

    corrValues.push_back(ComputeAlignmentMetric(this->FixedSignal.signalValues, resampledTrackerPositionMetric));
  }

  // Find the time offset that has the best alignment metric value
  bestCorrelationValue = corrValues.at(0);
  bestCorrelationTimeOffset = corrTimeOffsets.at(0);
  bestCorrelationNormalizationFactor = normalizationFactors.at(0);
  for (unsigned int i = 1; i < corrValues.size(); ++i)
  {
    if (corrValues.at(i) > bestCorrelationValue)
    {
      bestCorrelationValue = corrValues.at(i);
      bestCorrelationTimeOffset = corrTimeOffsets.at(i);
      bestCorrelationNormalizationFactor = normalizationFactors.at(i);
    }
  }
  LOG_DEBUG("bestCorrelationValue=" << bestCorrelationValue);
  LOG_DEBUG("bestCorrelationTimeOffset=" << bestCorrelationTimeOffset);
  LOG_DEBUG("bestCorrelationNormalizationFactor=" << bestCorrelationNormalizationFactor);
  LOG_DEBUG("numberOfSamples=" << corrValues.size());
}

double vtkPlusTemporalCalibrationAlgo::ComputeAlignmentMetric(const std::deque<double>& signalA, const std::deque<double>& signalB)
{
  if (signalA.size() != signalB.size())
  {
    LOG_ERROR("Cannot compute alignment metric: input signals size mismatch");
    return 0;
  }
  switch (SIGNAL_ALIGNMENT_METRIC)
  {
    case SSD:
      {
        // Use sum of squared differences as signal alignment metric
        double ssdSum = 0;
        for (unsigned int i = 0; i < signalA.size(); ++i)
        {
          double diff = signalA.at(i) - signalB.at(i);     //SSD
          ssdSum -= diff * diff;
        }
        return ssdSum;
      }
    case CORRELATION:
      {
        // Use correlation as signal alignment metric
        double xCorrSum = 0;
        for (unsigned int i = 0; i < signalA.size(); ++i)
        {
          xCorrSum += signalA.at(i) * signalB.at(i);     // XCORR
        }
        return xCorrSum;
      }
    case SAD:
      {
        // Use sum of absolute differences as signal alignment metric
        double sadSum = 0;
        for (unsigned int i = 0; i < signalA.size(); ++i)
        {
          sadSum -= fabs(signalA.at(i) - signalB.at(i));       //SAD
        }
        return sadSum;
      }
    default:
      LOG_ERROR("Unknown metric: " << SIGNAL_ALIGNMENT_METRIC);
  }
  return 0;
}


//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::ComputePositionSignalValues(SignalType& signal)
{
  switch (signal.frameType)
  {
    case FRAME_TYPE_TRACKER:
      {
        vtkSmartPointer<vtkPlusPrincipalMotionDetectionAlgo> trackerDataMetricExtractor = vtkSmartPointer<vtkPlusPrincipalMotionDetectionAlgo>::New();

        trackerDataMetricExtractor->SetTrackerFrames(signal.frameList);
        trackerDataMetricExtractor->SetSignalTimeRange(signal.signalTimeRangeMin, signal.signalTimeRangeMax);
        trackerDataMetricExtractor->SetProbeToReferenceTransformName(signal.probeToReferenceTransformName);

        if (trackerDataMetricExtractor->Update() != PLUS_SUCCESS)
        {
          LOG_ERROR("Failed to get line positions from video frames");
          return PLUS_FAIL;
        }
        trackerDataMetricExtractor->GetDetectedTimestamps(signal.signalTimestamps);
        trackerDataMetricExtractor->GetDetectedPositions(signal.signalValues);

        // If the metric values do not "swing" sufficiently, the signal is considered constant--i.e. infinite period--and will
        // not work for our purposes
        double minValue = 0;
        double maxValue = 0;
        GetSignalRange(signal.signalValues, 0, signal.signalValues.size() - 1, minValue, maxValue);
        double maxPeakToPeak = std::abs(maxValue - minValue);
        if (maxPeakToPeak < MINIMUM_TRACKER_SIGNAL_PEAK_TO_PEAK_MM)
        {
          LOG_ERROR("Detected metric values do not vary sufficiently (i.e. tracking signal is constant). Actual peak-to-peak variation: " << maxPeakToPeak << ", expected minimum: " << MINIMUM_TRACKER_SIGNAL_PEAK_TO_PEAK_MM);
          return PLUS_FAIL;
        }
        return PLUS_SUCCESS;
      }
    case FRAME_TYPE_VIDEO:
      {
        vtkSmartPointer<vtkPlusLineSegmentationAlgo> lineSegmenter = vtkSmartPointer<vtkPlusLineSegmentationAlgo>::New();
        lineSegmenter->SetTrackedFrameList(*signal.frameList);
        lineSegmenter->SetClipRectangle(this->LineSegmentationClipRectangleOrigin, this->LineSegmentationClipRectangleSize);
        lineSegmenter->SetSignalTimeRange(signal.signalTimeRangeMin, signal.signalTimeRangeMax);
        lineSegmenter->SetSaveIntermediateImages(this->SaveIntermediateImages);
        lineSegmenter->SetIntermediateFilesOutputDirectory(this->IntermediateFilesOutputDirectory);
        if (lineSegmenter->Update() != PLUS_SUCCESS)
        {
          LOG_ERROR("Failed to get line positions from video frames");
          return PLUS_FAIL;
        }
        lineSegmenter->GetDetectedTimestamps(signal.signalTimestamps);
        lineSegmenter->GetDetectedPositions(signal.signalValues);

        // If the metric values do not "swing" sufficiently, the signal is considered constant--i.e. infinite period--and will
        // not work for our purposes
        double minValue = 0;
        double maxValue = 0;
        this->GetSignalRange(signal.signalValues, 0, signal.signalValues.size() - 1, minValue, maxValue);
        double maxPeakToPeak = std::abs(maxValue - minValue);
        if (maxPeakToPeak < MINIMUM_VIDEO_SIGNAL_PEAK_TO_PEAK_PIXEL)
        {
          LOG_ERROR("Detected metric values do not vary sufficiently (i.e. video signal is constant)");
          return PLUS_FAIL;
        }
        return PLUS_SUCCESS;
      }
    default:
      LOG_ERROR("Compute position signal value failed. Unknown frame type: " << signal.frameType);
      return PLUS_FAIL;
  }
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::ComputeCommonTimeRange()
{
  if (this->FixedSignal.frameList->GetNumberOfTrackedFrames() < 1)
  {
    LOG_ERROR("Fixed signal frame list are empty");
    return PLUS_FAIL;
  }
  if (this->MovingSignal.frameList->GetNumberOfTrackedFrames() < 1)
  {
    LOG_ERROR("Moving signal frame list are empty");
    return PLUS_FAIL;
  }

  double fixedTimestampMin = this->FixedSignal.frameList->GetTrackedFrame(0)->GetTimestamp();
  double fixedTimestampMax = this->FixedSignal.frameList->GetTrackedFrame(this->FixedSignal.frameList->GetNumberOfTrackedFrames() - 1)->GetTimestamp();;
  double movingTimestampMin = this->MovingSignal.frameList->GetTrackedFrame(0)->GetTimestamp();
  double movingTimestampMax = this->MovingSignal.frameList->GetTrackedFrame(this->MovingSignal.frameList->GetNumberOfTrackedFrames() - 1)->GetTimestamp();;

  double commonRangeMin = std::max(fixedTimestampMin, movingTimestampMin);
  double commonRangeMax = std::min(fixedTimestampMax, movingTimestampMax);
  if (commonRangeMin + this->MaxMovingLagSec >= commonRangeMax - this->MaxMovingLagSec)
  {
    LOG_ERROR("Insufficient overlap between fixed and moving frames timestamps to compute time offset (fixed: " << fixedTimestampMin << "-" << fixedTimestampMax << " sec, moving: " << movingTimestampMin << "-" << movingTimestampMax << " sec)");
    return PLUS_FAIL;
  }

  this->FixedSignal.signalTimeRangeMin = commonRangeMin;
  this->FixedSignal.signalTimeRangeMax = commonRangeMax;
  this->MovingSignal.signalTimeRangeMin = commonRangeMin + this->MaxMovingLagSec;
  this->MovingSignal.signalTimeRangeMax = commonRangeMax - this->MaxMovingLagSec;

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::ComputeMovingSignalLagSec(TEMPORAL_CALIBRATION_ERROR& error)
{
  // Need to determine the common signal range before extracting signals from the frames,
  // because normalization, PCA, etc. must be performed only by taking into account
  // the frames in the common range.
  if (ComputeCommonTimeRange() != PLUS_SUCCESS)
  {
    error = TEMPORAL_CALIBRATION_ERROR_NO_COMMON_TIME_RANGE;
    return PLUS_FAIL;
  }

  // Compute the position signal values from the input frames
  if (ComputePositionSignalValues(this->FixedSignal) != PLUS_SUCCESS)
  {
    error = TEMPORAL_CALIBRATION_ERROR_FAILED_COMPUTE_FIXED;
    LOG_ERROR("Failed to compute position signal from fixed frames");
    return PLUS_FAIL;
  }
  if (ComputePositionSignalValues(this->MovingSignal) != PLUS_SUCCESS)
  {
    error = TEMPORAL_CALIBRATION_ERROR_FAILED_COMPUTE_MOVING;
    LOG_ERROR("Failed to compute position signal from moving frames");
    return PLUS_FAIL;
  }

  // Compute approx image image frame period. We will use this frame period as a step size in the coarse optimum search phase.
  double fixedTimestampMin = this->FixedSignal.signalTimestamps.at(0);
  double fixedTimestampMax = this->FixedSignal.signalTimestamps.at(this->FixedSignal.signalTimestamps.size() - 1);
  if (this->FixedSignal.signalTimestamps.size() < 2)
  {
    error = TEMPORAL_CALIBRATION_ERROR_NOT_ENOUGH_FIXED_FRAMES;
    LOG_ERROR("Not enough fixed frames are available");
    return PLUS_FAIL;
  }
  double imageFramePeriodSec = (fixedTimestampMax - fixedTimestampMin) / (this->FixedSignal.signalTimestamps.size() - 1);

  double searchRangeFineStep = imageFramePeriodSec * 3;

  //  Compute cross correlation with sign convention #1
  LOG_DEBUG("ComputeCorrelationBetweenFixedAndMovingSignal(sign convention #1)");
  double bestCorrelationValue = 0;
  double bestCorrelationTimeOffset = 0;
  double bestCorrelationNormalizationFactor = 1.0;
  std::deque<double> corrTimeOffsets;
  std::deque<double> corrValues;
  ComputeCorrelationBetweenFixedAndMovingSignal(-this->MaxMovingLagSec, this->MaxMovingLagSec, imageFramePeriodSec, bestCorrelationValue, bestCorrelationTimeOffset, bestCorrelationNormalizationFactor, corrTimeOffsets, corrValues);
  std::deque<double> corrTimeOffsetsFine;
  std::deque<double> corrValuesFine;
  ComputeCorrelationBetweenFixedAndMovingSignal(bestCorrelationTimeOffset - searchRangeFineStep, bestCorrelationTimeOffset + searchRangeFineStep, this->SamplingResolutionSec, bestCorrelationValue, bestCorrelationTimeOffset, bestCorrelationNormalizationFactor, corrTimeOffsetsFine, corrValuesFine);
  LOG_DEBUG("Time offset with sign convention #1: " << bestCorrelationTimeOffset);

  //  Compute cross correlation with sign convention #2
  LOG_DEBUG("ComputeCorrelationBetweenFixedAndMovingSignal(sign convention #2)");
  // Mirror tracker metric signal about x-axis
  for (unsigned int i = 0; i < this->MovingSignal.signalValues.size(); ++i)
  {
    this->MovingSignal.signalValues.at(i) *= -1;
  }
  double bestCorrelationValueInvertedTracker(0);
  double bestCorrelationTimeOffsetInvertedTracker(0);
  double bestCorrelationNormalizationFactorInvertedTracker(1.0);
  std::deque<double> corrTimeOffsetsInvertedTracker;
  std::deque<double> corrValuesInvertedTracker;
  ComputeCorrelationBetweenFixedAndMovingSignal(
    -this->MaxMovingLagSec,
    this->MaxMovingLagSec,
    imageFramePeriodSec,
    bestCorrelationValueInvertedTracker,
    bestCorrelationTimeOffsetInvertedTracker,
    bestCorrelationNormalizationFactorInvertedTracker,
    corrTimeOffsetsInvertedTracker,
    corrValuesInvertedTracker
  );
  std::deque<double> corrTimeOffsetsInvertedTrackerFine;
  std::deque<double> corrValuesInvertedTrackerFine;
  ComputeCorrelationBetweenFixedAndMovingSignal(
    bestCorrelationTimeOffsetInvertedTracker - searchRangeFineStep,
    bestCorrelationTimeOffsetInvertedTracker + searchRangeFineStep,
    this->SamplingResolutionSec, bestCorrelationValueInvertedTracker,
    bestCorrelationTimeOffsetInvertedTracker,
    bestCorrelationNormalizationFactorInvertedTracker,
    corrTimeOffsetsInvertedTrackerFine,
    corrValuesInvertedTrackerFine
  );
  LOG_DEBUG("Time offset with sign convention #2: " << bestCorrelationTimeOffsetInvertedTracker);

  // Adopt the smallest tracker lag
  if (std::abs(bestCorrelationTimeOffset) < std::abs(bestCorrelationTimeOffsetInvertedTracker))
  {
    this->MovingLagSec = bestCorrelationTimeOffset;
    this->BestCorrelationTimeOffset = bestCorrelationTimeOffset;
    this->BestCorrelationValue = bestCorrelationValue;
    this->BestCorrelationNormalizationFactor = bestCorrelationNormalizationFactor;
    this->CorrelationTimeOffsets = corrTimeOffsets;
    this->CorrelationValues = corrValues;
    this->CorrelationTimeOffsetsFine = corrTimeOffsetsFine;
    this->CorrelationValuesFine = corrValuesFine;

    // Flip tracker metric signal back to correspond to sign convention #1
    for (unsigned int i = 0; i < this->MovingSignal.signalValues.size(); ++i)
    {
      this->MovingSignal.signalValues.at(i) *= -1;
    }
  }
  else
  {
    this->MovingLagSec = bestCorrelationTimeOffsetInvertedTracker;
    this->BestCorrelationTimeOffset = bestCorrelationTimeOffsetInvertedTracker;
    this->BestCorrelationValue = bestCorrelationValueInvertedTracker;
    this->BestCorrelationNormalizationFactor = bestCorrelationNormalizationFactorInvertedTracker;
    this->CorrelationTimeOffsets = corrTimeOffsetsInvertedTracker;
    this->CorrelationValues = corrValuesInvertedTracker;
    this->CorrelationTimeOffsetsFine = corrTimeOffsetsInvertedTrackerFine;
    this->CorrelationValuesFine = corrValuesInvertedTrackerFine;
  }

  // Normalize the tracker metric based on the best index offset (only considering the overlap "window"
  this->MovingSignal.normalizedSignalValues.clear();
  this->MovingSignal.normalizedSignalTimestamps.clear();
  for (unsigned int i = 0; i < this->MovingSignal.signalTimestamps.size(); ++i)
  {
    if (this->MovingSignal.signalTimestamps.at(i) > this->FixedSignal.signalTimestamps.at(0) + this->MovingLagSec && this->MovingSignal.signalTimestamps.at(i) < this->FixedSignal.signalTimestamps.at(this->FixedSignal.signalTimestamps.size() - 1) + this->MovingLagSec)
    {
      this->MovingSignal.normalizedSignalValues.push_back(this->MovingSignal.signalValues.at(i));
      this->MovingSignal.normalizedSignalTimestamps.push_back(this->MovingSignal.signalTimestamps.at(i));
    }
  }

  // Get a normalized tracker position metric that can be displayed
  double unusedNormFactor = 1.0;
  NormalizeMetricValues(this->MovingSignal.normalizedSignalValues, unusedNormFactor);

  this->CalibrationError = sqrt(-this->BestCorrelationValue) / this->BestCorrelationNormalizationFactor;   // RMSE in mm

  LOG_DEBUG("Moving signal lags fixed signal by: " << this->MovingLagSec << " [s]");


  // Get maximum calibration error

  // Get the timestamps of the sliding signal (i.e. cropped video signal) shifted by the best-found offset
  std::deque<double> shiftedSlidingSignalTimestamps;
  for (unsigned int i = 0; i < this->FixedSignal.signalTimestamps.size(); ++i)
  {
    shiftedSlidingSignalTimestamps.push_back(this->FixedSignal.signalTimestamps.at(i) + this->MovingLagSec);     // TODO: check this
  }

  // Get the values of the tracker metric at the offset sliding signal values

  // Construct piecewise function for tracker signal
  vtkSmartPointer<vtkPiecewiseFunction> trackerPositionPiecewiseSignal = vtkSmartPointer<vtkPiecewiseFunction>::New();
  double midpoint = 0.5;
  double sharpness = 0;
  for (unsigned int i = 0; i < this->MovingSignal.normalizedSignalTimestamps.size(); ++i)
  {
    trackerPositionPiecewiseSignal->AddPoint(this->MovingSignal.normalizedSignalTimestamps.at(i), this->MovingSignal.normalizedSignalValues.at(i), midpoint, sharpness);
  }

  std::deque<double> resampledNormalizedTrackerPositionMetric;
  ResampleSignalLinearly(shiftedSlidingSignalTimestamps, trackerPositionPiecewiseSignal, resampledNormalizedTrackerPositionMetric);

  this->CalibrationErrorVector.clear();
  for (unsigned int i = 0; i < resampledNormalizedTrackerPositionMetric.size(); ++i)
  {
    double diff = resampledNormalizedTrackerPositionMetric.at(i) - this->FixedSignal.signalValues.at(i);     //SSD
    this->CalibrationErrorVector.push_back(diff * diff);
  }

  this->MaxCalibrationError = 0;
  for (unsigned int i = 0; i < this->CalibrationErrorVector.size(); ++i)
  {
    if (this->CalibrationErrorVector.at(i) > this->MaxCalibrationError)
    {
      this->MaxCalibrationError = this->CalibrationErrorVector.at(i);
    }
  }

  this->MaxCalibrationError = std::sqrt(this->MaxCalibrationError) / this->BestCorrelationNormalizationFactor;

  this->NeverUpdated = false;

  if (this->BestCorrelationValue <= SIGNAL_ALIGNMENT_METRIC_THRESHOLD[SIGNAL_ALIGNMENT_METRIC])
  {
    error = TEMPORAL_CALIBRATION_ERROR_RESULT_ABOVE_THRESHOLD;
    LOG_ERROR("Calculated correlation exceeds threshold value. This may be an indicator of a poor calibration.");
    return PLUS_FAIL;
  }

  LOG_DEBUG("Temporal calibration BestCorrelationValue = " << this->BestCorrelationValue << " (threshold=" << SIGNAL_ALIGNMENT_METRIC_THRESHOLD[SIGNAL_ALIGNMENT_METRIC] << ")");
  LOG_DEBUG("MaxCalibrationError=" << this->MaxCalibrationError);
  LOG_DEBUG("CalibrationError=" << this->CalibrationError);
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::ConstructTableSignal(std::deque<double>& x, std::deque<double>& y, vtkTable* table,
    double timeCorrection)
{
  // Clear table
  while (table->GetNumberOfColumns() > 0)
  {
    table->RemoveColumn(0);
  }

  //  Create array corresponding to the time values of the tracker plot
  vtkSmartPointer<vtkDoubleArray> arrX = vtkSmartPointer<vtkDoubleArray>::New();
  table->AddColumn(arrX);

  //  Create array corresponding to the metric values of the tracker plot
  vtkSmartPointer<vtkDoubleArray> arrY = vtkSmartPointer<vtkDoubleArray>::New();
  table->AddColumn(arrY);

  // Set the tracker data
  table->SetNumberOfRows(x.size());
  for (unsigned int i = 0; i < x.size(); ++i)
  {
    table->SetValue(i, 0, x.at(i) + timeCorrection);
    table->SetValue(i, 1, y.at(i));
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusTemporalCalibrationAlgo::ReadConfiguration(vtkXMLDataElement* aConfig)
{
  XML_FIND_NESTED_ELEMENT_OPTIONAL(calibrationParameters, aConfig, "vtkPlusTemporalCalibrationAlgo");
  if (calibrationParameters == NULL)
  {
    LOG_DEBUG("vtkPlusTemporalCalibrationAlgo element is not defined");
    return PLUS_SUCCESS;
  }
  XML_READ_BOOL_ATTRIBUTE_OPTIONAL(SaveIntermediateImages, calibrationParameters);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, MaximumMovingLagSec, calibrationParameters);

  if (calibrationParameters != NULL)
  {
    int clipOrigin[2] = {0};
    int clipSize[2] = {0};
    if (calibrationParameters->GetVectorAttribute("ClipRectangleOrigin", 2, clipOrigin) &&
        calibrationParameters->GetVectorAttribute("ClipRectangleSize", 2, clipSize))
    {
      this->LineSegmentationClipRectangleOrigin[0] = clipOrigin[0];
      this->LineSegmentationClipRectangleOrigin[1] = clipOrigin[1];
      this->LineSegmentationClipRectangleSize[0] = clipSize[0];
      this->LineSegmentationClipRectangleSize[1] = clipSize[1];
    }
    else
    {
      LOG_WARNING("Cannot find ClipRectangleOrigin or ClipRectangleSize attributes in the \'vtkPlusTemporalCalibrationAlgo\' configuration.");
    }
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void vtkPlusTemporalCalibrationAlgo::SetVideoClipRectangle(int* clipRectOriginIntVec, int* clipRectSizeIntVec)
{
  this->LineSegmentationClipRectangleOrigin[0] = clipRectOriginIntVec[0];
  this->LineSegmentationClipRectangleOrigin[1] = clipRectOriginIntVec[1];
  this->LineSegmentationClipRectangleSize[0] = clipRectSizeIntVec[0];
  this->LineSegmentationClipRectangleSize[1] = clipRectSizeIntVec[1];
}

//----------------------------------------------------------------------------
std::vector<int> vtkPlusTemporalCalibrationAlgo::GetVideoClipRectangle() const
{
  std::vector<int> result;
  result.push_back(this->LineSegmentationClipRectangleOrigin[0]);
  result.push_back(this->LineSegmentationClipRectangleOrigin[1]);
  result.push_back(this->LineSegmentationClipRectangleSize[0]);
  result.push_back(this->LineSegmentationClipRectangleSize[1]);
  return result;
}
