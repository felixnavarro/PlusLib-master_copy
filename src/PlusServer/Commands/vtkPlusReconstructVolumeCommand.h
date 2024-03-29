/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __vtkPlusReconstructVolumeCommand_h
#define __vtkPlusReconstructVolumeCommand_h

#include "vtkPlusServerExport.h"

#include "vtkPlusCommand.h"

class vtkPlusVolumeReconstructor;
//class vtkIGSIOTrackedFrameList;
//class vtkIGSIOTransformRepository;
class vtkPlusVirtualVolumeReconstructor;
/*!
  \class vtkPlusReconstructVolumeCommand
  \brief This command reconstructs a volume from an image sequence and saves it to disk or sends it to the client in an IMAGE message.
  \ingroup PlusLibPlusServer
 */
class vtkPlusServerExport vtkPlusReconstructVolumeCommand : public vtkPlusCommand
{
public:

  static vtkPlusReconstructVolumeCommand* New();
  vtkTypeMacro(vtkPlusReconstructVolumeCommand, vtkPlusCommand);
  virtual void PrintSelf(ostream& os, vtkIndent indent);
  virtual vtkPlusCommand* Clone() { return New(); }

  /*! Executes the command  */
  virtual PlusStatus Execute();

  /*! Read command parameters from XML */
  virtual PlusStatus ReadConfiguration(vtkXMLDataElement* aConfig);

  /*! Write command parameters to XML */
  virtual PlusStatus WriteConfiguration(vtkXMLDataElement* aConfig);

  /*! Get all the command names that this class can execute */
  virtual void GetCommandNames(std::list<std::string>& cmdNames);

  /*! Gets the description for the specified command name. */
  virtual std::string GetDescription(const std::string& commandName);

  /*! File name of the sequence file that contains the image frames */
  vtkGetStdStringMacro(InputSeqFilename);
  vtkSetStdStringMacro(InputSeqFilename);

  /*! If specified, the reconstructed volume will be saved into this filename */
  vtkGetStdStringMacro(OutputVolFilename);
  vtkSetStdStringMacro(OutputVolFilename);

  /*! If specified, the reconstructed volume will sent to the client through OpenIGTLink, using this device name */
  vtkGetStdStringMacro(OutputVolDeviceName);
  vtkSetStdStringMacro(OutputVolDeviceName);

  /*! Id of the live reconstruction command to be stopped, suspended, or resumed at the next Execute */
  vtkGetStdStringMacro(VolumeReconstructorDeviceId);
  vtkSetStdStringMacro(VolumeReconstructorDeviceId);

  /*!
    Set spacing of the output data in Reference coordinate system.
    This is required to be set, otherwise the reconstructed volume will be empty.
  */
  vtkSetVector3Macro(OutputSpacing, double);
  /*! Get spacing of the output data in Reference coordinate system  */
  vtkGetVector3Macro(OutputSpacing, double);

  /*!
    Set origin of the output data in Reference coordinate system.
    This is required to be set, otherwise the reconstructed volume will be empty.
  */
  vtkSetVector3Macro(OutputOrigin, double);
  /*! Get origin of the output data in Reference coordinate system  */
  vtkGetVector3Macro(OutputOrigin, double);

  /*!
    Set extent of the output data in Reference coordinate system.
    This is required to be set, otherwise the reconstructed volume will be empty.
  */
  vtkSetVector6Macro(OutputExtent, int);
  /*! Get extent of the output data in Reference coordinate system  */
  vtkGetVector6Macro(OutputExtent, int);

  vtkGetMacro(ApplyHoleFilling, bool);
  vtkSetMacro(ApplyHoleFilling, bool);

  void SetNameToReconstruct();
  void SetNameToStart();
  void SetNameToStop();
  void SetNameToSuspend();
  void SetNameToResume();
  void SetNameToGetSnapshot();

protected:
  /*! Saves image to disk (if requested) and prepare sending image as a response (if requested) */
  PlusStatus ProcessImageReply(vtkImageData* volumeToSend, const std::string& outputVolFilename, const std::string& outputVolDeviceName, std::string& resultMessage);

  vtkPlusVirtualVolumeReconstructor* GetVolumeReconstructorDevice();

  vtkPlusReconstructVolumeCommand();
  virtual ~vtkPlusReconstructVolumeCommand();

protected:
  std::string InputSeqFilename;
  std::string OutputVolFilename;
  std::string OutputVolDeviceName;
  std::string VolumeReconstructorDeviceId;

  // Output image position and size
  double OutputOrigin[3];
  double OutputSpacing[3];
  int OutputExtent[6];

  bool ApplyHoleFilling;

  vtkPlusReconstructVolumeCommand(const vtkPlusReconstructVolumeCommand&);
  void operator=(const vtkPlusReconstructVolumeCommand&);

};

#endif
