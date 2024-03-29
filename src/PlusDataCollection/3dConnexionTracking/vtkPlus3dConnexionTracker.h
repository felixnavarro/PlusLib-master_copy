/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlus3dConnexionTracker_h
#define __vtkPlus3dConnexionTracker_h

#include "vtkPlusDataCollectionExport.h"
#include "vtkIGSIORecursiveCriticalSection.h"
#include "vtkPlusDevice.h"

class vtkPlusDataSource;
class vtkMatrix4x4;

/*!
\class vtkPlus3dConnexionTracker
\brief Interface for 3D Connexion 3D mouse devices

This class reads transforms from 3D mouse devices.

\ingroup PlusLibDataCollection
*/
class vtkPlusDataCollectionExport vtkPlus3dConnexionTracker : public vtkPlusDevice
{
public:

  static vtkPlus3dConnexionTracker* New();
  vtkTypeMacro( vtkPlus3dConnexionTracker, vtkPlusDevice );

  /*! Connect to device */
  PlusStatus InternalConnect();

  /*! Disconnect from device */
  virtual PlusStatus InternalDisconnect();

  /*!
  Probe to see if the tracking system is present on the specified serial port.
  */
  PlusStatus Probe();

  /*!
  Get an update from the tracking system and push the new transforms
  to the tools.  This should only be used within vtkTracker.cxx.
  This method is called by the tracker thread.
  */
  PlusStatus InternalUpdate();

  /*! Read configuration from xml data */
  PlusStatus ReadConfiguration( vtkXMLDataElement* config );

  /*! Write configuration to xml data */
  PlusStatus WriteConfiguration( vtkXMLDataElement* config );

  /*! Process input event received from the device. For internal use only. Public to allow calling from static function. */
  void ProcessDeviceInputEvent( LPARAM lParam );

  /*! Callback function called when capture window is destroyed. For internal use only. Public to allow calling from static function. */
  void OnCaptureWindowDestroy();

  enum OperatingModeType
  {
    MOUSE_MODE,
    JOYSTICK_MODE
  };

  virtual bool IsTracker() const { return true; }

protected:

  vtkPlus3dConnexionTracker();
  ~vtkPlus3dConnexionTracker();

  /*! Start processing data received from the device */
  PlusStatus InternalStartRecording();

  /*! Stop processing data received from the device */
  PlusStatus InternalStopRecording();

  /*! Register callback to get notifications from the device */
  PlusStatus RegisterDevice();

  /*! Unregister callback function */
  void UnregisterDevice();

  /*! Create an invisible window that will be used to receive input messages from the device  */
  PlusStatus CreateCaptureWindow();

  /*! Delete the capture window */
  void DestroyCaptureWindow();

private:  // Functions.

  vtkPlus3dConnexionTracker( const vtkPlus3dConnexionTracker& );
  void operator=( const vtkPlus3dConnexionTracker& );

private:  // Variables.

  vtkPlusDataSource* SpaceNavigatorTool;
  vtkMatrix4x4* LatestMouseTransform;
  vtkMatrix4x4* DeviceToTrackerTransform;
  double TranslationScales[3];
  double RotationScales[3];

  OperatingModeType OperatingMode;

  /*! Mutex instance simultaneous access of mouse pose transform (LatestMouseTransform) from the tracker and the main thread */
  vtkSmartPointer<vtkIGSIORecursiveCriticalSection> Mutex;

  std::string CaptureWindowClassName;
  HWND CaptureWindowHandle;

  /*! Pointer to the first element of an array of raw input devices */
  PRAWINPUTDEVICE RegisteredRawInputDevices;
  /*! Number of raw input devices in the RawInputDevices array */
  unsigned int NumberOfRegisteredRawInputDevices;

};

#endif
