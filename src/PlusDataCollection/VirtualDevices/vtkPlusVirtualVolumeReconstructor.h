/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlusVirtualVolumeReconstructor_h
#define __vtkPlusVirtualVolumeReconstructor_h

#include "vtkPlusDataCollectionExport.h"

#include "vtkPlusDevice.h"
#include <string>

class vtkPlusVolumeReconstructor;

/*!
\class vtkPlusVirtualVolumeReconstructor
\brief

\ingroup PlusLibDataCollection
*/
class vtkPlusDataCollectionExport vtkPlusVirtualVolumeReconstructor : public vtkPlusDevice
{
public:
  static vtkPlusVirtualVolumeReconstructor* New();
  vtkTypeMacro(vtkPlusVirtualVolumeReconstructor, vtkPlusDevice);
  void PrintSelf(ostream& os, vtkIndent indent);

  /*!
    This method is safe to be called from any thread.
  */
  virtual PlusStatus GetReconstructedVolumeFromFile(const std::string& inputSeqFilename, vtkImageData* reconstructedVolume, std::string& errorMessage);

  /*!
    This method is safe to be called from any thread.
    \param applyHoleFilling If true (default) then hole filling will be applied (if enabled and fully specified), otherwise hole filling will be skipped
  */
  PlusStatus GetReconstructedVolume(vtkImageData* reconstructedVolume, std::string& outErrorMessage, bool applyHoleFilling = true);

  /*!
    Updated the transform repository contents within the volume reconstructor.
    It is advisable to call this before each volume reconstruction starting.
    This method is safe to be called from any thread.
  */
  PlusStatus UpdateTransformRepository(vtkIGSIOTransformRepository* sharedTransformRepository);

  /*!
    Enables adding frames to the volume. It can be used for pausing the recording.
    This method is safe to be called from any thread.
  */
  vtkGetMacro(EnableReconstruction, bool);
  /*!
    Enables adding frames to the volume. It can be used for pausing the recording.
    This method is safe to be called from any thread.
  */
  void SetEnableReconstruction(bool aValue);

  /*!
    Clear the volume.
    This method is safe to be called from any thread.
  */
  virtual PlusStatus Reset();

  /*! If specified, the reconstructed volume will be saved into this filename */
  vtkSetStdStringMacro(OutputVolFilename);
  vtkGetStdStringMacro(OutputVolFilename);

  /*! If specified, the reconstructed volume will sent to the client through OpenIGTLink, using this device name */
  vtkSetStdStringMacro(OutputVolDeviceName);
  vtkGetStdStringMacro(OutputVolDeviceName);

  /*! Set the output volume's origin in the Reference coordinate system*/
  void SetOutputOrigin(double* origin);

  /*! Set the output volume's spacing in the Reference coordinate system's unit (usually mm)*/
  void SetOutputSpacing(double* spacing);

  /*! Set the output volume's extent (xStart, xEnd, yStart, yEnd, zStart, zEnd) in voxels */
  void SetOutputExtent(int* extent);

  vtkGetMacro(TotalFramesRecorded, long int);

protected:

  /*! Read main configuration from xml data */
  virtual PlusStatus ReadConfiguration(vtkXMLDataElement*);

  /*! write main configuration to xml data */
  virtual PlusStatus WriteConfiguration(vtkXMLDataElement*);

  virtual PlusStatus InternalUpdate();

  virtual PlusStatus NotifyConfigured();

  virtual double GetAcquisitionRate() const;

  virtual int OutputChannelCount() const;

  /*!
    Method that writes output streams to XML
  */
  virtual void InternalWriteOutputChannels(vtkXMLDataElement* rootXMLElement);

  void SetRequestedFrameRate(double aValue);
  vtkGetMacro(RequestedFrameRate, double);

  virtual vtkPlusDataCollector* GetDataCollector() { return this->DataCollector; }

  virtual bool IsTracker() const { return false; }
  virtual bool IsVirtual() const { return true; }

  virtual PlusStatus InternalConnect();
  virtual PlusStatus InternalDisconnect();

  PlusStatus AddFrames(vtkIGSIOTrackedFrameList* trackedFrameList);

  /*! Get the sampling period length (in seconds). Frames are copied from the devices to the data collection buffer once in every sampling period. */
  double GetSamplingPeriodSec();

  vtkPlusVirtualVolumeReconstructor();
  virtual ~vtkPlusVirtualVolumeReconstructor();

protected:
  /*! Timestamp of last recorded frame (only frames that have more recent timestamp will be added) */
  double m_LastAlreadyRecordedFrameTimestamp;

  /*! Desired timestamp of the next frame to be recorded */
  double m_NextFrameToBeRecordedTimestamp;

  /*! Frame rate of the sampling */
  const int m_SamplingFrameRate;

  /*! Requested frame rate (frames per second) */
  double RequestedFrameRate;

  /* Time waited in update */
  double m_TimeWaited;
  double m_LastUpdateTime;

  /*! Timestamp of last added frame (the tracked frames acquired since this timestamp will be added to the volume on the next Execute) */
  double LastRecordedFrameTimestamp;

  /*! Record the number of frames captured */
  long int TotalFramesRecorded;  // hard drive will probably fill up before a regular int is hit, but still...

  vtkSmartPointer<vtkPlusVolumeReconstructor> VolumeReconstructor;
  vtkSmartPointer<vtkIGSIOTransformRepository> TransformRepository;

  bool EnableReconstruction;

  std::string OutputVolFilename;
  std::string OutputVolDeviceName;

  /*! Mutex instance simultaneous access of writer (writer may be accessed from command processing thread and also the internal update thread) */
  vtkSmartPointer<vtkIGSIORecursiveCriticalSection> VolumeReconstructorAccessMutex;

private:
  vtkPlusVirtualVolumeReconstructor(const vtkPlusVirtualVolumeReconstructor&);   // Not implemented.
  void operator=(const vtkPlusVirtualVolumeReconstructor&);   // Not implemented.
};

#endif
