/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

/*=========================================================================
The following copyright notice is applicable to parts of this file:
Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
All rights reserved.
See Copyright.txt or http://www.kitware.com/Copyright.htm for details.
=========================================================================*/

#ifndef __vtkPlusDataBuffer_h
#define __vtkPlusDataBuffer_h

// Local includes
#include "igsioCommon.h"
#include "PlusConfigure.h"
#include "vtkPlusDataCollectionExport.h"
#include "PlusStreamBufferItem.h"
#include "vtkPlusTimestampedCircularBuffer.h"

//#include "igsioTrackedFrame.h"

// VTK includes
#include <vtkObject.h>

class vtkPlusDevice;
enum ToolStatus;

//class vtkIGSIOTrackedFrameList;

class vtkPlusDataCollectionExport vtkPlusBuffer : public vtkObject
{
public:
  enum TIMESTAMP_FILTERING_OPTION
  {
    READ_FILTERED_AND_UNFILTERED_TIMESTAMPS = 0,
    READ_UNFILTERED_COMPUTE_FILTERED_TIMESTAMPS,
    READ_FILTERED_IGNORE_UNFILTERED_TIMESTAMPS
  };

  /*! Tracker item temporal interpolation type */
  enum DataItemTemporalInterpolationType
  {
    EXACT_TIME, /*!< only returns the item if the requested timestamp exactly matches the timestamp of an existing element */
    INTERPOLATED, /*!< returns interpolated transform (requires valid transform at the requested timestamp) */
    CLOSEST_TIME /*!< returns the closest item  */
  };

  static vtkPlusBuffer* New();
  vtkTypeMacro(vtkPlusBuffer, vtkObject);
  virtual void PrintSelf(ostream& os, vtkIndent indent) VTK_OVERRIDE;

  /*!
    Set the size of the buffer, i.e. the maximum number of
    video frames that it will hold.  The default is 30.
  */
  virtual PlusStatus SetBufferSize(int n);
  /*! Get the size of the buffer */
  virtual int GetBufferSize();

  /*!
    Add a frame plus a timestamp to the buffer with frame index.
    If the timestamp is  less than or equal to the previous timestamp,
    or if the frame's format doesn't match the buffer's frame format,
    then the frame is not added to the buffer. If a clip rectangle is defined
    then only that portion of the frame is extracted.
  */
  virtual PlusStatus AddItem(vtkImageData* frame,
                             US_IMAGE_ORIENTATION usImageOrientation,
                             US_IMAGE_TYPE imageType,
                             long frameNumber,
                             const std::array<int, 3>& clipRectangleOrigin,
                             const std::array<int, 3>& clipRectangleSize,
                             double unfilteredTimestamp = UNDEFINED_TIMESTAMP,
                             double filteredTimestamp = UNDEFINED_TIMESTAMP,
                             const igsioFieldMapType* customFields = NULL);
  /*!
    Add a frame plus a timestamp to the buffer with frame index.
    If the timestamp is  less than or equal to the previous timestamp,
    or if the frame's format doesn't match the buffer's frame format,
    then the frame is not added to the buffer. If a clip rectangle is defined
    then only that portion of the frame is extracted.
  */
  virtual PlusStatus AddItem(const igsioVideoFrame* frame,
                             long frameNumber,
                             const std::array<int, 3>& clipRectangleOrigin,
                             const std::array<int, 3>& clipRectangleSize,
                             double unfilteredTimestamp = UNDEFINED_TIMESTAMP,
                             double filteredTimestamp = UNDEFINED_TIMESTAMP,
                             const igsioFieldMapType* customFields = NULL);
  /*!
    Add a frame plus a timestamp to the buffer with frame index.
    Additionally an optional field name&value can be added,
    which will be saved as a custom field of the added item.
    If the timestamp is less than or equal to the previous timestamp,
    or if the frame's format doesn't match the buffer's frame format,
    then the frame is not added to the buffer. If a clip rectangle is defined
    then only that portion of the image is extracted.
  */
  virtual PlusStatus AddItem(void* imageDataPtr,
                             US_IMAGE_ORIENTATION usImageOrientation,
                             const FrameSizeType& inputFrameSizeInPx,
                             igsioCommon::VTKScalarPixelType pixelType,
                             unsigned int numberOfScalarComponents,
                             US_IMAGE_TYPE imageType,
                             int numberOfBytesToSkip,
                             long frameNumber,
                             const std::array<int, 3>& clipRectangleOrigin,
                             const std::array<int, 3>& clipRectangleSize,
                             double unfilteredTimestamp = UNDEFINED_TIMESTAMP,
                             double filteredTimestamp = UNDEFINED_TIMESTAMP,
                             const igsioFieldMapType* customFields = NULL,
                             vtkStreamingVolumeFrame* encodedFrame = NULL);

  /*!
    Add a frame plus a timestamp to the buffer with frame index.
    Additionally an optional field name&value can be added,
    which will be saved as a custom field of the added item.
    If the timestamp is less than or equal to the previous timestamp,
    or if the frame's size doesn't match the frame size of the buffer,
    then the frame is not added. This overload is only used for storing
    compressed or variable frame size entries
  */
  virtual PlusStatus AddItem(void* imageDataPtr,
                             const FrameSizeType& frameSize,
                             unsigned int frameSizeInBytes,
                             US_IMAGE_TYPE imageType,
                             long frameNumber,
                             double unfilteredTimestamp = UNDEFINED_TIMESTAMP,
                             double filteredTimestamp = UNDEFINED_TIMESTAMP,
                             const igsioFieldMapType* customFields = NULL);

  /*!
    Add custom fields to the new item
    If the timestamp is less than or equal to the previous timestamp,
    or if the frame's format doesn't match the buffer's frame format,
    then the frame is not added to the buffer.
  */
  virtual PlusStatus AddItem(const igsioFieldMapType& fields,
                             long frameNumber,
                             double unfilteredTimestamp = UNDEFINED_TIMESTAMP,
                             double filteredTimestamp = UNDEFINED_TIMESTAMP);

  /*!
    Add a matrix plus status to the list, with an exactly known timestamp value (e.g., provided by a high-precision hardware timer).
    If the timestamp is less than or equal to the previous timestamp, then nothing  will be done.
    If filteredTimestamp argument is undefined then the filtered timestamp will be computed from the input unfiltered timestamp.
  */
  PlusStatus AddTimeStampedItem(vtkMatrix4x4* matrix, ToolStatus status, unsigned long frameNumber, double unfilteredTimestamp, double filteredTimestamp = UNDEFINED_TIMESTAMP, const igsioFieldMapType* customFields = NULL);

  /*! Get a frame with the specified frame uid from the buffer */
  virtual ItemStatus GetStreamBufferItem(BufferItemUidType uid, StreamBufferItem* bufferItem);
  /*! Get the most recent frame from the buffer */
  virtual ItemStatus GetLatestStreamBufferItem(StreamBufferItem* bufferItem)
  {
    return this->GetStreamBufferItem(this->GetLatestItemUidInBuffer(), bufferItem);
  };
  /*! Get the oldest frame from buffer */
  virtual ItemStatus GetOldestStreamBufferItem(StreamBufferItem* bufferItem)
  {
    return this->GetStreamBufferItem(this->GetOldestItemUidInBuffer(), bufferItem);
  };
  /*! Get a frame that was acquired at the specified time from buffer */
  virtual ItemStatus GetStreamBufferItemFromTime(double time, StreamBufferItem* bufferItem, DataItemTemporalInterpolationType interpolation);
  virtual PlusStatus ModifyBufferItemFrameField(BufferItemUidType uid, const std::string& key, const std::string& value);

  /*! Get latest timestamp in the buffer */
  virtual ItemStatus GetLatestTimeStamp(double& latestTimestamp);

  /*! Get oldest timestamp in the buffer */
  virtual ItemStatus GetOldestTimeStamp(double& oldestTimestamp);

  /*! Get buffer item timestamp */
  virtual ItemStatus GetTimeStamp(BufferItemUidType uid, double& timestamp);

  /*! Returns true if the latest item contains valid video data */
  virtual bool GetLatestItemHasValidVideoData();

  /*! Returns true if the latest item contains valid transform data */
  virtual bool GetLatestItemHasValidTransformData();

  /*! Returns true if the latest item contains valid field data */
  virtual bool GetLatestItemHasValidFieldData();

  /*! Get the index assigned by the data acquisition system (usually a counter) from the buffer by frame UID. */
  virtual ItemStatus GetIndex(const BufferItemUidType uid, unsigned long& index);

  /*!
    Given a timestamp, compute the nearest buffer index
    This assumes that the times monotonically increase
  */
  ItemStatus GetBufferIndexFromTime(const double time, int& bufferIndex);

  /*! Get buffer item unique ID */
  virtual BufferItemUidType GetOldestItemUidInBuffer()
  {
    return this->StreamBuffer->GetOldestItemUidInBuffer();
  }
  virtual BufferItemUidType GetLatestItemUidInBuffer()
  {
    return this->StreamBuffer->GetLatestItemUidInBuffer();
  }
  virtual ItemStatus GetItemUidFromTime(double time, BufferItemUidType& uid)
  {
    return this->StreamBuffer->GetItemUidFromTime(time, uid);
  }

  /*! Set the local time offset in seconds (global = local + offset) */
  virtual void SetLocalTimeOffsetSec(double offsetSec);
  /*! Get the local time offset in seconds (global = local + offset) */
  virtual double GetLocalTimeOffsetSec();

  /*! Get the number of items in the buffer */
  virtual int GetNumberOfItems()
  {
    return this->StreamBuffer->GetNumberOfItems();
  }

  /*!
    Get the frame rate from the buffer based on the number of frames in the buffer and the elapsed time.
    Ideal frame rate shows the mean of the frame periods in the buffer based on the frame
    number difference (aka the device frame rate).
    If framePeriodStdevSecPtr is not null, then the standard deviation of the frame period is computed as well (in seconds) and
    stored at the specified address.
  */
  virtual double GetFrameRate(bool ideal = false, double* framePeriodStdevSecPtr = NULL)
  {
    return this->StreamBuffer->GetFrameRate(ideal, framePeriodStdevSecPtr);
  }

  /*! Set maximum allowed time difference in seconds between the desired and the closest valid timestamp */
  vtkSetMacro(MaxAllowedTimeDifference, double);
  /*! Get maximum allowed time difference in seconds between the desired and the closest valid timestamp */
  vtkGetMacro(MaxAllowedTimeDifference, double);

  /*!
  Copy a specified transform to a tracker buffer. It is useful when tracking-only data is stored in a
  metafile (with dummy image data), which is read by a sequence metafile reader, and the
  result is needed as a vtkPlusDataBuffer.
  If useFilteredTimestamps is true, then the filtered timestamps that are stored in the buffer
  will be copied to the tracker buffer. If useFilteredTimestamps is false, then only unfiltered timestamps
  will be copied to the tracker buffer and the tracker buffer will compute the filtered timestamps.
  */
  PlusStatus CopyTransformFromTrackedFrameList(vtkIGSIOTrackedFrameList* sourceTrackedFrameList, TIMESTAMP_FILTERING_OPTION timestampFiltering, igsioTransformName& transformName);


  /*! Make this buffer into a copy of another buffer.  You should Lock both of the buffers before doing this. */
  virtual void DeepCopy(vtkPlusBuffer* buffer);

  /*! Clear buffer (set the buffer pointer to the first element) */
  virtual void Clear();

  /*! Set number of items used for timestamp filtering (with LSQR mimimizer) */
  virtual void SetAveragedItemsForFiltering(int averagedItemsForFiltering);

  virtual int GetAveragedItemsForFiltering();

  /*! Set recording start time */
  virtual void SetStartTime(double startTime);
  /*! Get recording start time */
  virtual double GetStartTime();

  /*! Get the table report of the timestamped buffer  */
  virtual PlusStatus GetTimeStampReportTable(vtkTable* timeStampReportTable);

  /*! If TimeStampReporting is enabled then all filtered and unfiltered timestamp values will be saved in a table for diagnostic purposes. */
  void SetTimeStampReporting(bool enable);
  /*! If TimeStampReporting is enabled then all filtered and unfiltered timestamp values will be saved in a table for diagnostic purposes. */
  bool GetTimeStampReporting();

  /*! Set the frame size in pixel  */
  PlusStatus SetFrameSize(unsigned int x, unsigned int y, unsigned int z, bool allocateFrames = true);
  /*! Set the frame size in pixel  */
  PlusStatus SetFrameSize(const FrameSizeType& frameSize, bool allocateFrames = true);
  /*! Get the frame size in pixel  */
  virtual FrameSizeType GetFrameSize() const;
  virtual PlusStatus GetFrameSize(unsigned int& _arg1, unsigned int& _arg2, unsigned int& _arg3) const;

  /*! Set the pixel type */
  PlusStatus SetPixelType(igsioCommon::VTKScalarPixelType pixelType);
  /*! Get the pixel type */
  vtkGetMacro(PixelType, igsioCommon::VTKScalarPixelType);

  /*! Set the number of scalar components */
  PlusStatus SetNumberOfScalarComponents(unsigned int numberOfScalarComponents);
  /*! Get the number of scalar components*/
  vtkGetMacro(NumberOfScalarComponents, unsigned int);

  /*! Set the image type. Does not convert the pixel values. */
  PlusStatus SetImageType(US_IMAGE_TYPE imageType);
  /*! Get the image type (B-mode, RF, ...) */
  vtkGetMacro(ImageType, US_IMAGE_TYPE);

  /*! Set the image orientation (MF, MN, ...). Does not reorder the pixels. */
  PlusStatus SetImageOrientation(US_IMAGE_ORIENTATION imageOrientation);
  /*! Get the image orientation (MF, MN, ...) */
  vtkGetMacro(ImageOrientation, US_IMAGE_ORIENTATION);

  /*! Get the number of bytes per scalar component */
  int GetNumberOfBytesPerScalar();

  /*!
    Get the number of bytes per pixel
    It is the number of bytes per scalar multiplied by the number of scalar components.
  */
  int GetNumberOfBytesPerPixel();

  /*! Copy images from a tracked frame buffer. It is useful when data is stored in a metafile and the data is needed as a vtkPlusDataBuffer. */
  PlusStatus CopyImagesFromTrackedFrameList(vtkIGSIOTrackedFrameList* sourceTrackedFrameList, TIMESTAMP_FILTERING_OPTION timestampFiltering, bool copyFrameFields);

  /*! Dump the current state of the video buffer to metafile */
  virtual PlusStatus WriteToSequenceFile(const char* filename, bool useCompression = false);

  vtkGetStringMacro(DescriptiveName);
  vtkSetStringMacro(DescriptiveName);

protected:
  vtkPlusBuffer();
  ~vtkPlusBuffer();

  /*! Update video buffer by setting the frame format for each frame  */
  virtual PlusStatus AllocateMemoryForFrames();

  /*!
    Compares frame format with new frame imaging parameters.
    \return true if current buffer frame format matches the method arguments, otherwise false
  */
  virtual bool CheckFrameFormat(const FrameSizeType& frameSizeInPx, igsioCommon::VTKScalarPixelType pixelType, US_IMAGE_TYPE imgType, int numberOfScalarComponents);

  /*! Returns the two buffer items that are closest previous and next buffer items relative to the specified time. itemA is the closest item */
  PlusStatus GetPrevNextBufferItemFromTime(double time, StreamBufferItem& itemA, StreamBufferItem& itemB);

  /*!
  Interpolate the matrix for the given timestamp from the two nearest transforms in the buffer.
  The rotation is interpolated with SLERP interpolation, and the position is interpolated with linear interpolation.
  The flags correspond to the closest element.
  */
  virtual ItemStatus GetInterpolatedStreamBufferItemFromTime(double time, StreamBufferItem* bufferItem);

  /*! Get tracker buffer item from an exact timestamp */
  virtual ItemStatus GetStreamBufferItemFromExactTime(double time, StreamBufferItem* bufferItem);

  /*! Get tracker buffer item from the closest timestamp */
  virtual ItemStatus GetStreamBufferItemFromClosestTime(double time, StreamBufferItem* bufferItem);

protected:
  /*! Image frame size in pixel */
  FrameSizeType FrameSize;

  /*! Image pixel type */
  igsioCommon::VTKScalarPixelType PixelType;

  /*! Number of scalar components */
  unsigned int NumberOfScalarComponents;

  /*! Image type (B-Mode, RF, ...) */
  US_IMAGE_TYPE ImageType;

  /*! Image orientation (MF, MN, ...) */
  US_IMAGE_ORIENTATION ImageOrientation;

  typedef vtkPlusTimestampedCircularBuffer StreamItemCircularBuffer;
  /*! Timestamped circular buffer that stores the last N frames */
  StreamItemCircularBuffer* StreamBuffer;

  /*! Maximum allowed time difference in seconds between the desired and the closest valid timestamp */
  double MaxAllowedTimeDifference;

  char* DescriptiveName;

private:
  vtkPlusBuffer(const vtkPlusBuffer&);
  void operator=(const vtkPlusBuffer&);
};

#endif