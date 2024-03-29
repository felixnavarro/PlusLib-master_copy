/*!=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlusLineSegmentationAlgo_h
#define __vtkPlusLineSegmentationAlgo_h

#include "itkImage.h"
#include "vtkPlusCalibrationExport.h"
#include "vtkObject.h"
#include <deque>

//class igsioTrackedFrame; 
//class vtkIGSIOTrackedFrameList;

/*!
  \class vtkPlusLineSegmentationAlgo
  \brief Detect the position of a line (image of a plane) in an US image sequence.
  \ingroup PlusLibCalibrationAlgorithm
*/
class vtkPlusCalibrationExport vtkPlusLineSegmentationAlgo : public vtkObject
{
public:
  struct LineParameters /*!< Line parameters is defined in the Image coordinate system (orientation is MF, origin is in the image corner, unit is pixel) */
  {
    bool lineDetected;
    double lineOriginPoint_Image[2];
    double lineDirectionVector_Image[2];

    double Slope() const { return lineDirectionVector_Image[1] / lineDirectionVector_Image[0]; }
  };

  typedef unsigned char CharPixelType;
  typedef itk::Image<CharPixelType, 2> CharImageType;

  static vtkPlusLineSegmentationAlgo* New();
  vtkTypeMacro(vtkPlusLineSegmentationAlgo, vtkObject);
  virtual void PrintSelf(ostream& os, vtkIndent indent) VTK_OVERRIDE;

  /*!
    Read XML based configuration for the segmentation
    \param aConfig Root element of device set configuration data
  */
  PlusStatus ReadConfiguration(vtkXMLDataElement* aConfig);

  /*! Sets the input US video frames */
  void SetTrackedFrameList(vtkIGSIOTrackedFrameList& aTrackedFrameList);
  void SetTrackedFrame(igsioTrackedFrame& aTrackedFrame);

  /*! Sets the time range where the signal will be extracted from. If rangeMax<rangeMin then all the input frames will be used to genereate the signal. */
  void SetSignalTimeRange(double rangeMin, double rangeMax);

  /*! Sets a rectangular region of interest. The algorithm will ignore everything outside the specified image region. If the rectangle size is (0,0) then no clipping is performed. */
  void SetClipRectangle(int clipRectangleOriginPix[2], int clipRectangleSizePix[2]);

  /*!
    Run the line detection algorithm on the input video frames
    \param errorDetail if the algorithm fails then the details of the problem are returned in this string
  */
  PlusStatus Update();

  /*! Get the timestamps of the frames where a line was successfully detected. Frames where the line detection was failed are skipped. */
  void GetDetectedTimestamps(std::deque<double>& timestamps);

  /*! Get the line positions on the frames where a line was successfully detected. Frames where the line detection was failed are skipped. */
  void GetDetectedPositions(std::deque<double>& positions);

  /*! Get the parameters of the plane where a line was successfully detected. No frames are skipped, the size of the vector matches the number of input tracked frames. If line detection failed on an image then the lineDetected parameter of the item is set to false. */
  void GetDetectedLineParameters(std::vector<LineParameters>& parameters);

  /*! Enable/disable saving of intermediate images for debugging */
  void SetSaveIntermediateImages(bool saveIntermediateImages);

  void SetIntermediateFilesOutputDirectory(const std::string& outputDirectory);

  PlusStatus Reset();

  /*! Plot intensity profile for each scanline. Enable for debugging. */
  vtkGetMacro(PlotIntensityProfile, bool);
  vtkSetMacro(PlotIntensityProfile, bool);

protected:
  vtkPlusLineSegmentationAlgo();
  virtual ~vtkPlusLineSegmentationAlgo();

  PlusStatus VerifyVideoInput();

  PlusStatus ComputeVideoPositionMetric();

  PlusStatus FindPeakStart(std::deque<int>& intensityProfile, int maxFromLargestArea, int startOfMaxArea, double& startOfPeak);

  PlusStatus FindLargestPeak(std::deque<int>& intensityProfile, int& maxFromLargestArea, int& maxFromLargestAreaIndex, int& startOfMaxArea);

  PlusStatus ComputeCenterOfGravity(std::deque<int>& intensityProfile, int startOfMaxArea, double& centerOfGravity);

  void ComputeLineParameters(std::vector<itk::Point<double, 2> >& data, LineParameters& outputParameters);

  void PlotIntArray(const std::deque<int>& intensityValues);

  void PlotDoubleArray(const std::deque<double>& intensityValues);

  void SaveIntermediateImage(int frameNumber, CharImageType::Pointer scanlineImage, double x_0, double y_0, double r_x, double r_y, int numOfValidScanlines, const std::vector<itk::Point<double, 2> >& intensityPeakPositions);

  /*! Update passed region to fit within the frame size. */
  void LimitToClipRegion(CharImageType::RegionType& region);

protected:
  vtkSmartPointer<vtkIGSIOTrackedFrameList> m_TrackedFrameList;

  std::deque<double> m_SignalValues;
  std::deque<double> m_SignalTimestamps;
  std::vector<LineParameters> m_LineParameters;

  /*! If "true" then images of intermediate steps (i.e. scanlines used, detected lines) are saved in local directory */
  bool m_SaveIntermediateImages;

  /*! Directory where the intermediate files are written to */
  std::string IntermediateFilesOutputDirectory;

  /*! Plot intensity profile for each scanline. Enable for debugging. */
  bool PlotIntensityProfile;

  double m_SignalTimeRangeMin;
  double m_SignalTimeRangeMax;

  /*! Clip rectangle origin for the processing (in pixels). Everything outside the rectangle is ignored. */
  CharImageType::IndexValueType m_ClipRectangleOrigin[2];

  /*! Clip rectangle origin for the processing (in pixels). Everything outside the rectangle is ignored. */
  CharImageType::SizeValueType m_ClipRectangleSize[2];

private:
  vtkPlusLineSegmentationAlgo(const vtkPlusLineSegmentationAlgo&);
  void operator=(const vtkPlusLineSegmentationAlgo&);
};

#endif //  __vtkPlusLineSegmentationAlgo_h 
