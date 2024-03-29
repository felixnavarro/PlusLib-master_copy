/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlusSavedDataSource_h
#define __vtkPlusSavedDataSource_h

#include "vtkPlusDataCollectionExport.h"

#include "vtkPlusDevice.h"

class vtkPlusBuffer;

class vtkPlusDataCollectionExport vtkPlusSavedDataSource;

/*!
\class vtkPlusSavedDataSource
\brief Class for providing VTK video input interface from sequence file
\ingroup PlusLibDataCollection

Attributes:
\li SequenceFile: source file to read the image/transform data from
\li UseData: specifies what data to be use from the source file (IMAGE|IMAGE_AND_TRANSFORM|TRANSFORM)
\li RepeatEnabled: if true then the sequence is played repeateadly (TRUE|FALSE)
\li UseOriginalTimestamps: if true then the original timestamps (recorded originally in the source file)
  will be replayed exactly, otherwise only the timestamp difference will be replayed exactly,
  starting from the current time (TRUE|FALSE)

*/
class vtkPlusDataCollectionExport vtkPlusSavedDataSource : public vtkPlusDevice
{
public:
  vtkTypeMacro( vtkPlusSavedDataSource,vtkPlusDevice );
  void PrintSelf( ostream& os, vtkIndent indent );
  static vtkPlusSavedDataSource* New();

  /*! Read configuration from xml data */
  virtual PlusStatus ReadConfiguration( vtkXMLDataElement* config );
  /*! Write configuration to xml data */
  virtual PlusStatus WriteConfiguration( vtkXMLDataElement* config );

  /*! Set SequenceMetafile name with path with tracking buffer data  */
  vtkSetStringMacro( SequenceFile );
  /*! Get SequenceMetafile name with path with tracking buffer data  */
  vtkGetStringMacro( SequenceFile );

  /*! Set the time range of the loaded buffer that will be replayed */
  void SetLoopTimeRange( double loopStartTime, double loopStopTime );

  /*! Get the time range of the loaded buffer that will be replayed. It is initialized to the full range of the loaded data set on Connect(). */
  void GetLoopTimeRange( double& loopStartTime, double& loopStopTime );

  /*! Set flag to to enable saved dataset looping /sa RepeatEnabled */
  vtkGetMacro( RepeatEnabled, bool );
  /*! Get flag to to enable saved dataset looping /sa RepeatEnabled */
  vtkSetMacro( RepeatEnabled, bool );
  /*! Get flag to to enable saved dataset looping /sa RepeatEnabled */
  vtkBooleanMacro( RepeatEnabled, bool );

  /*! Read all the frame fields from the file and provide them in the output */
  vtkGetMacro( UseAllFrameFields, bool );
  /*! Read all the frame fields from the file and provide them in the output */
  vtkSetMacro( UseAllFrameFields, bool );
  /*! Read all the frame fields from the file and provide them in the output */
  vtkBooleanMacro( UseAllFrameFields, bool );

  /*! Read the timestamps from the file and use provide them in the output (instead of the current time) */
  vtkGetMacro( UseOriginalTimestamps, bool );
  /*! Read the timestamps from the file and use provide them in the output (instead of the current time) */
  vtkSetMacro( UseOriginalTimestamps, bool );
  /*! Read the timestamps from the file and use provide them in the output (instead of the current time) */
  vtkBooleanMacro( UseOriginalTimestamps, bool );

  /*! Get local video buffer */
  vtkGetObjectMacro( LocalVideoBuffer, vtkPlusBuffer );

  virtual bool IsTracker() const;

  /*!
    Perform any completion tasks once configured
  */
  virtual PlusStatus NotifyConfigured();

  /*! Probe to see if the tracking system is present on the specified serial port. */
  PlusStatus Probe();

protected:
  /*! Constructor */
  vtkPlusSavedDataSource();
  /*! Destructor */
  virtual ~vtkPlusSavedDataSource();

  /*! Connect to device */
  virtual PlusStatus InternalConnect();

  /*! Connect to device, in case the output is a video stream */
  virtual PlusStatus InternalConnectVideo( vtkIGSIOTrackedFrameList* savedDataBuffer );

  /*! Connect to device, in case the output is a tracker stream */
  virtual PlusStatus InternalConnectTracker( vtkIGSIOTrackedFrameList* savedDataBuffer );

  /*! Disconnect from device */
  virtual PlusStatus InternalDisconnect();

  /*! The internal function which actually does the grab.  */
  PlusStatus InternalUpdate();

  /*! Internal update, called when NOT the original timestamps are used */
  PlusStatus InternalUpdateCurrentTimestamp( BufferItemUidType frameToBeAddedUid, int frameToBeAddedLoopIndex );

  /*! Internal update, called when the original timestamps are used */
  PlusStatus InternalUpdateOriginalTimestamp( BufferItemUidType frameToBeAddedUid, int frameToBeAddedLoopIndex );

  BufferItemUidType GetClosestFrameUidWithinTimeRange( double time_Local, double startTime_Local, double stopTime_Local );

  /*! Get local tracker buffer */
  vtkPlusBuffer* GetLocalTrackerBuffer();

  /*! Get the output data source (the video buffer or the first active tool buffer of the current stream) */
  vtkPlusDataSource* GetOutputDataSource();

  /*!
    Get local tracker buffer, it returns tracker buffer if the output is a tracker stream, and
    returns the video buffer if the output is a video stream
  */
  vtkPlusBuffer* GetLocalBuffer();

  void DeleteLocalBuffers();

protected:
  /*! Byte alignment of each row in the framebuffer */
  int FrameBufferRowAlignment;

  /*! Name of input sequence file */
  char* SequenceFile;

  /*! Flag to to enable saved dataset looping. If it's enabled, the video source will continuously play saved data (starts playing from the beginning when the end is reached). */
  bool RepeatEnabled;

  /*!
    Loop start time (in local buffer time). The first acquired frame (at system time = 0) will be the frame
    that has the closest timestamp to the start time.
    ItemTimestamp_Local = loopStartTime_Local + (actualTimestamp_Sys - startTimestamp_Sys) % (loopStopTime-LooStartTime)
  */
  double LoopStartTime_Local;

  /*! Loop stop time (in local buffer time) */
  double LoopStopTime_Local;

  /*! Local video buffer */
  vtkPlusBuffer* LocalVideoBuffer;

  /*! Local buffer for each tracker tool, used for storing data read from sequence metafile */
  std::map<std::string, vtkPlusBuffer*> LocalTrackerBuffers;

  /*! Read all the frame fields from the file and provide them in the output */
  bool UseAllFrameFields;

  /*! Read the timestamps from the file and use provide them in the output (instead of the current time) */
  bool UseOriginalTimestamps;

  /*! Buffer item UID of the last added frame in the local buffer */
  BufferItemUidType LastAddedFrameUid;

  /*! Index of the loop when the last frame was added. Used for making sure we add each frame only once in one loop period. */
  int LastAddedLoopIndex;

  /*! Frames before this item (identified by the buffer item UID) in the local buffer are ignored, not replayed */
  BufferItemUidType LoopFirstFrameUid;

  /*! Frames after this item (identified by the buffer item UID) in the local buffer are ignored, not replayed */
  BufferItemUidType LoopLastFrameUid;

  enum SimulatedStreamType
  {
    TRACKER_STREAM, /*< The device provides a tracker stream */
    VIDEO_STREAM   /*!< The device provides a video stream (with optional tracking data added as fields) */
  };

  SimulatedStreamType SimulatedStream;

private:
  static vtkPlusSavedDataSource* Instance;
  vtkPlusSavedDataSource( const vtkPlusSavedDataSource& ); // Not implemented.
  void operator=( const vtkPlusSavedDataSource& ); // Not implemented.
};

#endif

