/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include "PlusBkProFocusCameraLinkReceiver.h"
#include "vtkPlusBkProFocusCameraLinkVideoSource.h"
#include "vtkImageData.h"
#include "vtkImageImport.h"
#include "vtkPlusChannel.h"
#include "vtkPlusDataSource.h"
#include "vtkPlusRfProcessor.h"
#include "vtkPlusUsScanConvertCurvilinear.h"
#include "vtkPlusUsScanConvertLinear.h"

// BK Includes
#include "AcquisitionGrabberSapera.h"
#include "AcquisitionInjector.h"
#include "AcquisitionSettings.h"
#include "BmodeViewDataReceiver.h"
#include "CommandAndControl.h"
#include "ParamConnectionSettings.h"
#include "SaperaViewDataReceiver.h"
#include "TcpClient.h"

#include "vtkMath.h"

//----------------------------------------------------------------------------

vtkStandardNewMacro(vtkPlusBkProFocusCameraLinkVideoSource);

//----------------------------------------------------------------------------
std::string ParseResponse(std::string str, int item)
{
  int nItems = 0;
  std::string rest = str;
  while (rest.size())
  {
    int pos = rest.find(",");
    std::string curItem;
    if (pos == -1)
    {
      curItem = rest;
      rest = "";
    }
    else
    {
      curItem = rest.substr(0, pos);
      rest = rest.substr(pos + 1, rest.length());
    }
    if (nItems++ == item)
    {
      return curItem;
    }
  }
  return "";
}

//----------------------------------------------------------------------------
std::string ParseResponseQuoted(std::string str, int item)
{
  std::string result = ParseResponse(str, item);
  if (result.length() > 2)
  { return result.substr(1, result.length() - 2); }
  else
  { return ""; }
}

class vtkPlusBkProFocusCameraLinkVideoSource::vtkInternal
{
public:
  vtkPlusBkProFocusCameraLinkVideoSource* External;
  vtkPlusChannel* Channel;

  vtkPlusBkProFocusCameraLinkVideoSource::ScanPlaneType CurrentPlane;
  bool SubscribeScanPlane;

  ParamConnectionSettings BKparamSettings; // parConnectSettings, for read/write settings from ini file

  AcquisitionInjector BKAcqInjector; // injector
  AcquisitionSettings BKAcqSettings; // settings
  AcquisitionGrabberSapera BKAcqSapera; // sapera
  BmodeViewDataReceiver BKBModeView; // bmodeView;
  SaperaViewDataReceiver* pBKSaperaView; // saperaView
  PlusBkProFocusCameraLinkReceiver PlusReceiver;

  CmdCtrlSettings BKcmdCtrlSettings; // cmdCtrlSet
  CommandAndControl* pBKcmdCtrl; // cmdctrl

  vtkPlusBkProFocusCameraLinkVideoSource::vtkInternal::vtkInternal(vtkPlusBkProFocusCameraLinkVideoSource* external)
    : External(external)
    , pBKSaperaView(NULL)
    , pBKcmdCtrl(NULL)
    , CurrentPlane(vtkPlusBkProFocusCameraLinkVideoSource::Transverse)
    , SubscribeScanPlane(false)
  {
    this->PlusReceiver.SetPlusVideoSource(this->External);
  }

  virtual vtkPlusBkProFocusCameraLinkVideoSource::vtkInternal::~vtkInternal()
  {
    this->PlusReceiver.SetPlusVideoSource(NULL);
    this->Channel = NULL;
    delete this->pBKSaperaView;
    this->pBKSaperaView = NULL;
    delete this->pBKcmdCtrl;
    this->pBKcmdCtrl = NULL;
    this->External = NULL;
  }

  PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::vtkInternal::InitializeParametersFromOEM()
  {
    //WSAIF wsaif;
    TcpClient* oemClient = (this->pBKcmdCtrl->GetOEMClient());
    std::string value;

    // Explanation of the queries/responses is from the BK document
    //  "Product Specification for Pro Focus OEM Interface"
    // DATA:TRANSDUCER:A "A","8848";
    // the first value is the connector used, the second is the transducer type
    value = QueryParameter(oemClient, "TRANSDUCER");
    if (value.empty())
    {
      return PLUS_FAIL;
    }
    std::string transducer = ParseResponseQuoted(value, 1);
    LOG_INFO("Transducer: " << transducer);
    // DATA:SCAN_PLANE:A "S";
    // reply depends on the transducer type; for 8848, it is either "T" (transverse) or "S"
    // (sagittal). For the abdominal 8820, the response apparently is "" (!)
    value = QueryParameter(oemClient, "SCAN_PLANE");
    if (value.empty())
    {
      return PLUS_FAIL;
    }
    std::string scanPlane = ParseResponseQuoted(value, 0);
    LOG_INFO("Scan plane: " << scanPlane);
    this->CurrentPlane = (scanPlane.find("S") == std::string::npos ? vtkPlusBkProFocusCameraLinkVideoSource::Transverse : vtkPlusBkProFocusCameraLinkVideoSource::Sagittal);

    value = QueryParameter(oemClient, "B_FRAMERATE");   // DATA:B_FRAMERATE:A 17.8271;
    if (value.empty())
    {
      return PLUS_FAIL;
    }
    float frameRate = atof(ParseResponse(value, 0).c_str());
    LOG_INFO("Frame rate: " << frameRate);

    LOG_INFO("Queried value: " << value);
    // DATA:B_GEOMETRY_SCANAREA:A
    //    StartLineX,StartLineY,StartLineAngle,StartDepth,StopLineX,StopLineY,StopLineAngle,StopDepth
    // StartLineX/Y: coordinate of the start line origin in mm
    // StartLineAngle: angle of the start line in radians
    // StartDepth: start depth of the scanning area in m
    // StopLineX/Y: coordinate of the stop line origin in mm
    // StopDepth: stop depth of the scanning area in mm
    value = QueryParameter(oemClient, "B_GEOMETRY_SCANAREA");
    if (value.empty())
    {
      return PLUS_FAIL;
    }
    float startLineXMm = fabs(atof(ParseResponse(value, 0).c_str())) * 1000.;
    float stopLineXMm = fabs(atof(ParseResponse(value, 4).c_str())) * 1000.;
    float startAngleDeg =
      vtkMath::DegreesFromRadians(atof(ParseResponse(value, 2).c_str()));
    // start depth is defined at the distance from the outer surface of the transducer
    // to the surface of the crystal. stop depth is from the outer surface to the scan depth.
    // start depth has negative depth in this coordinate system (the transducer surface pixels are inside the image)
    float startDepthMm = atof(ParseResponse(value, 3).c_str()) * 1000.;
    float stopAngleDeg =
      vtkMath::DegreesFromRadians(atof(ParseResponse(value, 6).c_str()));
    float stopDepthMm = atof(ParseResponse(value, 7).c_str()) * 1000.;

    // DATA:B_SCANLINES_COUNT:A 517;
    // Number of scanning lines in specified view
    value = QueryParameter(oemClient, "B_SCANLINES_COUNT");
    if (value.empty())
    {
      return PLUS_FAIL;
    }
    float scanlinesCount = atof(ParseResponse(value, 0).c_str());
    value = QueryParameter(oemClient, "B_RF_LINE_LENGTH");
    if (value.empty())
    {
      return PLUS_FAIL;
    }
    float rfLineLength = atof(ParseResponse(value, 0).c_str());

    // BK defines angles start at 9:00, not at 12:00
    startAngleDeg = -(startAngleDeg - stopAngleDeg) / 2.;
    stopAngleDeg = -startAngleDeg;

    // Update the RfProcessor with scan conversion parameters

    if (transducer == "8848")
    {
      if (scanPlane == "S")
      {
        LOG_DEBUG("Linear transducer");
        if (vtkPlusUsScanConvertLinear::SafeDownCast(this->Channel->GetRfProcessor()->GetScanConverter()) == NULL)
        {
          // The current scan converter is not for a linear transducer, so change it now
          vtkSmartPointer<vtkPlusUsScanConvertLinear> scanConverter = vtkSmartPointer<vtkPlusUsScanConvertLinear>::New();
          this->Channel->GetRfProcessor()->SetScanConverter(scanConverter);
        }
      }
      else if (scanPlane == "T")
      {
        LOG_DEBUG("Curvilinear transducer");
        if (vtkPlusUsScanConvertCurvilinear::SafeDownCast(this->Channel->GetRfProcessor()->GetScanConverter()) == NULL)
        {
          // The current scan converter is not for a curvilinear transducer, so change it now
          vtkSmartPointer<vtkPlusUsScanConvertCurvilinear> scanConverter = vtkSmartPointer<vtkPlusUsScanConvertCurvilinear>::New();
          this->Channel->GetRfProcessor()->SetScanConverter(scanConverter);
        }
      }
      else
      {
        LOG_WARNING("Unknown transducer scan plane (" << scanPlane << "). Cannot determine transducer geometry.");
      }
    }
    else
    {
      LOG_WARNING("Unknown transducer model (" << transducer << "). Cannot determine transducer geometry.");
    }

    vtkPlusUsScanConvert* scanConverter = this->Channel->GetRfProcessor()->GetScanConverter();
    if (scanConverter != NULL)
    {
      vtkPlusUsScanConvertLinear* scanConverterLinear = vtkPlusUsScanConvertLinear::SafeDownCast(scanConverter);
      vtkPlusUsScanConvertCurvilinear* scanConverterCurvilinear = vtkPlusUsScanConvertCurvilinear::SafeDownCast(scanConverter);
      if (scanConverterLinear != NULL)
      {
        scanConverterLinear->SetTransducerWidthMm(startLineXMm + stopLineXMm);
        scanConverterLinear->SetImagingDepthMm(stopDepthMm);
      }
      else if (scanConverterCurvilinear != NULL)
      {
        // this is a predefined value for 8848 transverse array, which
        // apparently cannot be queried from OEM. It is not clear if ROC is the distance to
        // crystal surface or to the outer surface of the transducer (waiting for the response from BK).
        scanConverterCurvilinear->SetOutputImageStartDepthMm(startDepthMm);
        scanConverterCurvilinear->SetRadiusStartMm(9.74);
        scanConverterCurvilinear->SetRadiusStopMm(stopDepthMm);
        scanConverterCurvilinear->SetThetaStartDeg(startAngleDeg);
        scanConverterCurvilinear->SetThetaStopDeg(stopAngleDeg);
      }
      else
      {
        LOG_WARNING("Unknown scan converter type: " << scanConverter->GetTransducerGeometry());
      }

      scanConverter->SetTransducerName((std::string("BK-") + transducer + scanPlane).c_str());
    }
    else
    {
      LOG_WARNING("Scan converter is not defined in either manually or through the OEM interface");
    }

    /* Not used in reconstruction
    std::cout << "Queried value: " << value << std::endl;
    // DATA:3D_SPACING:A 0.25;
    //  Returns the spacing between the frames;
    //  Fan-type movers return spacing in degrees
    //  Linear-type movers returm spacing in mm
    value = QueryParameter(oemClient, "3D_SPACING");
    std::cout << "Queried value: " << value << std::endl;
    // DATA:3D_CAPTURE_AREA:A Left,Top,Right,Bottom;
    //  Returns the capture area for the acquisition in screen pixel coordinates
    value = QueryParameter(oemClient, "3D_CAPTURE_AREA");
    std::cout << "Queried value: " << value << std::endl;
    */

    return PLUS_SUCCESS;
  }

  std::string vtkPlusBkProFocusCameraLinkVideoSource::vtkInternal::QueryParameter(TcpClient* oemClient, const char* parameter)
  {
    std::string query;
    char buffer[1024];

    query = std::string("QUERY:") + parameter + ":A;";
    oemClient->Write(query.c_str(), strlen(query.c_str()));
    try
    {
      oemClient->Read(&buffer[0], 1024);
    }
    catch (TcpClientWaitException exc)
    {
      LOG_WARNING("QueryParameter::" << exc.Message);
      return "";
    }
    std::string value = std::string(&buffer[0]);
    std::string prefix = std::string("DATA:") + parameter + ":A ";
    return value.substr(prefix.length(), value.length() - prefix.length() - 1);
  }

  void vtkPlusBkProFocusCameraLinkVideoSource::vtkInternal::RegisterEventCallback(void* owner, void (*func)(void*, char*, size_t))
  {
    if (this->pBKcmdCtrl != NULL)
    {
      this->pBKcmdCtrl->RegisterEventCallback(owner, func);
    }
  }
};

//----------------------------------------------------------------------------
vtkPlusBkProFocusCameraLinkVideoSource::vtkPlusBkProFocusCameraLinkVideoSource()
{
  this->Internal = new vtkInternal(this);

  this->IniFileName = NULL;
  this->ShowSaperaWindow = false;
  this->ShowBModeWindow = false;

  this->ImagingMode = RfMode;
  SetLogFunc(LogInfoMessageCallback);
  SetDbgFunc(LogDebugMessageCallback);

  this->RequireImageOrientationInConfiguration = true;

  // No need for StartThreadForInternalUpdates, as we are notified about each new frame through a callback function
}

//----------------------------------------------------------------------------
vtkPlusBkProFocusCameraLinkVideoSource::~vtkPlusBkProFocusCameraLinkVideoSource()
{
  SetIniFileName(NULL);

  delete this->Internal;
  this->Internal = NULL;
}

//----------------------------------------------------------------------------
void vtkPlusBkProFocusCameraLinkVideoSource::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
void vtkPlusBkProFocusCameraLinkVideoSource::LogInfoMessageCallback(char* msg)
{
  LOG_INFO(msg);
}

//----------------------------------------------------------------------------
void vtkPlusBkProFocusCameraLinkVideoSource::LogDebugMessageCallback(char* msg)
{
  LOG_INFO(msg);
}

//----------------------------------------------------------------------------
void vtkPlusBkProFocusCameraLinkVideoSource::EventCallback(void* owner, char* eventText, size_t eventTextLength)
{
  vtkPlusBkProFocusCameraLinkVideoSource* self = static_cast<vtkPlusBkProFocusCameraLinkVideoSource*>(owner);

  igsioLockGuard<vtkIGSIORecursiveCriticalSection> critSectionGuard(self->UpdateMutex);

  if (self->Internal->SubscribeScanPlane && !_strnicmp("SCAN_PLANE", &eventText[strlen("SDATA:")], strlen("SCAN_PLANE")))
  {
    char* probeId = &eventText[strlen("SDATA:SCAN_PLANE:")];
    std::string eventStr(probeId);
    std::string details = eventStr.substr(eventStr.find(' '));
    if (details.find('S') != std::string::npos)
    {
      self->Internal->CurrentPlane = Sagittal;
    }
    if (details.find('T') != std::string::npos)
    {
      self->Internal->CurrentPlane = Transverse;
    }
    self->Internal->Channel = self->FindChannelByPlane();
    if (self->Internal->Channel == NULL)
    {
      LOG_ERROR("Unable to find a channel by plane. Check configuration.");
      return;
    }
    if (self->ChannelConfiguredMap.find(self->Internal->Channel) == self->ChannelConfiguredMap.end()
        || self->ChannelConfiguredMap[self->Internal->Channel] == false)
    {
      self->Internal->BKAcqSapera.StopGrabbing();
      self->Internal->InitializeParametersFromOEM();
      self->Internal->BKAcqSapera.StartGrabbing(&self->Internal->BKAcqInjector);
      self->ChannelConfiguredMap[self->Internal->Channel] = true;
    }
  }
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::InternalConnect()
{
  std::string iniFilePath;
  GetFullIniFilePath(iniFilePath);
  if (!this->Internal->BKparamSettings.LoadSettingsFromIniFile(iniFilePath.c_str()))
  {
    LOG_ERROR("Could not load BK parameter settings from file: " << iniFilePath.c_str());
    return PLUS_FAIL;
  }

  LOG_DEBUG("BK scanner address: " << this->Internal->BKparamSettings.GetScannerAddress());
  LOG_DEBUG("BK scanner OEM port: " << this->Internal->BKparamSettings.GetOemPort());
  LOG_DEBUG("BK scanner toolbox port: " << this->Internal->BKparamSettings.GetToolboxPort());

  this->Internal->BKcmdCtrlSettings.LoadFromIniFile(iniFilePath.c_str());

  if (!this->Internal->BKAcqSettings.LoadIni(iniFilePath.c_str()))
  {
    LOG_ERROR("Failed to load acquisition settings from file: " << iniFilePath.c_str());
    return PLUS_FAIL;
  }

  this->Internal->BKcmdCtrlSettings.autoUpdate = true;
  this->Internal->pBKcmdCtrl = new CommandAndControl(&this->Internal->BKparamSettings, &this->Internal->BKcmdCtrlSettings);
  this->Internal->BKcmdCtrlSettings = this->Internal->pBKcmdCtrl->GetCmdCtrlSettings();    // Get what has not failed !!!

  if (this->Internal->InitializeParametersFromOEM() != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to initialize BK parameters.");
    return PLUS_FAIL;
  }

  int numSamples = 0;
  int numLines = 0;
  if (!this->Internal->pBKcmdCtrl->CalcSaperaBufSize(&numSamples, &numLines))
  {
    LOG_ERROR("Failed to get Sapera framegrabber buffer size for RF data");
    delete this->Internal->pBKcmdCtrl;
    this->Internal->pBKcmdCtrl = NULL;
    return PLUS_FAIL;
  }

  LOG_DEBUG("Sapera buffer size: numSamples=" << numSamples << ", numLines=" << numLines);

  this->Internal->BKAcqSettings.SetLinesPerFrame(numLines);
  this->Internal->BKAcqSettings.SetRFLineLength(numSamples);
  this->Internal->BKAcqSettings.SetFramesToGrab(0); // continuous

  if (this->Internal->SubscribeScanPlane)
  {
    LOG_INFO("Subscribing to scan plane events.");
    this->Internal->pBKcmdCtrl->SubscribeScanPlaneEvents();
    this->Internal->RegisterEventCallback((void*)this, EventCallback);
  }

  if (!this->Internal->BKAcqSapera.Init(this->Internal->BKAcqSettings))
  {
    LOG_ERROR("Failed to initialize framegrabber");
    delete this->Internal->pBKcmdCtrl;
    this->Internal->pBKcmdCtrl = NULL;
    return PLUS_FAIL;
  }

  this->Internal->pBKSaperaView = new SaperaViewDataReceiver(this->Internal->BKAcqSapera.GetBuffer());
  if (this->ShowSaperaWindow)
  {
    // show Sapera viewer
    this->Internal->BKAcqInjector.AddDataReceiver(this->Internal->pBKSaperaView);
  }

  if (this->ShowBModeWindow)
  {
    // show B-mode image
    this->Internal->BKAcqInjector.AddDataReceiver(&this->Internal->BKBModeView);
  }

  // send frames to this video source
  this->Internal->BKAcqInjector.AddDataReceiver(&this->Internal->PlusReceiver);

  // Clear buffer on connect because the new frames that we will acquire might have a different size
  for (ChannelContainerIterator it = this->OutputChannels.begin(); it != this->OutputChannels.end(); ++it)
  {
    (*it)->Clear();
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::InternalDisconnect()
{
  this->Internal->BKAcqSapera.Destroy();

  this->Internal->BKAcqInjector.RemoveDataReceiver(&this->Internal->PlusReceiver);

  if (this->ShowBModeWindow)
  {
    this->Internal->BKAcqInjector.RemoveDataReceiver(&this->Internal->BKBModeView);
  }

  if (this->ShowSaperaWindow)
  {
    this->Internal->BKAcqInjector.RemoveDataReceiver(this->Internal->pBKSaperaView);
  }

  delete this->Internal->pBKSaperaView;
  this->Internal->pBKSaperaView = NULL;
  delete this->Internal->pBKcmdCtrl;
  this->Internal->pBKcmdCtrl = NULL;
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::InternalStartRecording()
{
  if (!this->Internal->BKAcqSapera.StartGrabbing(&this->Internal->BKAcqInjector))
  {
    LOG_ERROR("Failed to start grabbing");
    return PLUS_FAIL;
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::InternalStopRecording()
{
  /*
  Sleep(500);
  if (!this->Internal->BKAcqSapera.StopGrabbing())
  {
  LOG_ERROR("Failed to start grabbing");
  return PLUS_FAIL;
  }
  */
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
void vtkPlusBkProFocusCameraLinkVideoSource::NewFrameCallback(void* pixelDataPtr, const FrameSizeType& inputFrameSizeInPix, igsioCommon::VTKScalarPixelType pixelType, US_IMAGE_TYPE imageType)
{
  igsioLockGuard<vtkIGSIORecursiveCriticalSection> critSectionGuard(this->UpdateMutex);

  // we may need to overwrite these, so create a copy that will be used internally
  FrameSizeType frameSizeInPix =
  {
    inputFrameSizeInPix[0],
    inputFrameSizeInPix[1],
    inputFrameSizeInPix[2]
  };

  LOG_TRACE("New frame received: " << frameSizeInPix[0] << "x" << frameSizeInPix[1]
            << ", pixel type: " << vtkImageScalarTypeNameMacro(pixelType)
            << ", image type: " << igsioVideoFrame::GetStringFromUsImageType(imageType));

  vtkPlusChannel* channel = this->FindChannelByPlane();

  if (channel == NULL)
  {
    std::string type("Unknown");
    if (this->Internal->CurrentPlane == Sagittal)
    {
      type = "Sagittal";
    }
    if (this->Internal->CurrentPlane == Transverse)
    {
      type = "Transverse";
    }
    LOG_ERROR("No channel returned. Verify configuration. Requested plane: " << type);
    return;
  }

  switch (this->ImagingMode)
  {
    case RfMode:
    {
      if (imageType == US_IMG_RF_REAL || imageType == US_IMG_RF_IQ_LINE || imageType == US_IMG_RF_I_LINE_Q_LINE)
      {
        // RF image is received and RF image is needed => no need for conversion
        break;
      }
      LOG_ERROR("The received frame is discarded, as it cannot be convert from " << igsioVideoFrame::GetStringFromUsImageType(imageType) << " to RF");
      return;
    }
    case BMode:
    {
      if (imageType == US_IMG_BRIGHTNESS)
      {
        // B-mode image is received and B-mode image is needed => no need for conversion
        break;
      }
      else if (imageType == US_IMG_RF_REAL || imageType == US_IMG_RF_IQ_LINE || imageType == US_IMG_RF_I_LINE_Q_LINE)
      {
        // convert from RF to Brightness

        // Create a VTK image input for the RF to Brightness converter
        vtkSmartPointer<vtkImageImport> bufferToVtkImage = vtkSmartPointer<vtkImageImport>::New();
        bufferToVtkImage->SetDataScalarType(pixelType);
        bufferToVtkImage->SetImportVoidPointer((unsigned char*)pixelDataPtr);
        bufferToVtkImage->SetDataExtent(0, frameSizeInPix[0] - 1, 0, frameSizeInPix[1] - 1, 0, 0);
        bufferToVtkImage->SetWholeExtent(0, frameSizeInPix[0] - 1, 0, frameSizeInPix[1] - 1, 0, 0);
        bufferToVtkImage->Update();

        channel->GetRfProcessor()->SetRfFrame(bufferToVtkImage->GetOutput(), imageType);
        channel->SetSaveRfProcessingParameters(true); // RF processing parameters were used, make sure they will be saved into the config file

        // Overwrite the input parameters with the converted image; it will look as if we received a B-mode image
        vtkImageData* convertedBmodeImage = channel->GetRfProcessor()->GetBrightnessScanConvertedImage();
        pixelDataPtr = convertedBmodeImage->GetScalarPointer();
        int* resultExtent = convertedBmodeImage->GetExtent();
        frameSizeInPix[0] = resultExtent[1] - resultExtent[0] + 1;
        frameSizeInPix[1] = resultExtent[3] - resultExtent[2] + 1;
        pixelType = convertedBmodeImage->GetScalarType();
        imageType = US_IMG_BRIGHTNESS;
        break;
      }
      LOG_ERROR("The received frame is discarded, as it cannot be convert from " << igsioVideoFrame::GetStringFromUsImageType(imageType) << " to Brightness");
      return;
    }
    default:
      LOG_ERROR("The received frame is discarded, as the requested imaging mode (" << igsioVideoFrame::GetStringFromUsImageType(imageType) << ") is not supported");
      return;
  }

  vtkPlusDataSource* aSource(NULL);
  if (channel->GetVideoSource(aSource) != PLUS_SUCCESS)
  {
    LOG_ERROR("Output channel does not have video source. Unable to record a new frame.");
    return;
  }
  // If the buffer is empty, set the pixel type and frame size to the first received properties
  if (aSource->GetNumberOfItems() == 0)
  {
    LOG_DEBUG("Set up BK ProFocus image buffer");
    aSource->SetPixelType(pixelType);
    aSource->SetImageType(imageType);
    aSource->SetInputFrameSize(frameSizeInPix[0], frameSizeInPix[1], 1);
    if (imageType == US_IMG_BRIGHTNESS)
    {
      // Store B-mode images in MF orientation
      aSource->SetOutputImageOrientation(US_IMG_ORIENT_MF);
    }
    else
    {
      // RF data is stored line-by-line, therefore set the temporary storage buffer to FM orientation
      aSource->SetOutputImageOrientation(US_IMG_ORIENT_FM);
    }
    LOG_INFO("Frame size: " << frameSizeInPix[0] << "x" << frameSizeInPix[1]
             << ", pixel type: " << vtkImageScalarTypeNameMacro(pixelType)
             << ", image type: " << igsioVideoFrame::GetStringFromUsImageType(imageType)
             << ", device image orientation: " << igsioVideoFrame::GetStringFromUsImageOrientation(aSource->GetInputImageOrientation())
             << ", buffer image orientation: " << igsioVideoFrame::GetStringFromUsImageOrientation(aSource->GetOutputImageOrientation()));

  }

  aSource->AddItem(pixelDataPtr, aSource->GetInputImageOrientation(), frameSizeInPix, pixelType, 1, imageType, 0, this->FrameNumber);
  this->Modified();
  this->FrameNumber++;

  // just for testing: igsioVideoFrame::SaveImageToFile( (unsigned char*)pixelDataPtr, frameSizeInPix, numberOfBitsPerPixel, (char *)"test.jpg");
}


//-----------------------------------------------------------------------------
PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  this->ChannelConfiguredMap.clear();

  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  XML_READ_CSTRING_ATTRIBUTE_REQUIRED(IniFileName, deviceConfig);
  XML_READ_ENUM2_ATTRIBUTE_OPTIONAL(ImagingMode, deviceConfig, "BMode", BMode, "RfMode", RfMode);

  const char* subscribe = deviceConfig->GetAttribute("SubscribeScanPlane");
  if (subscribe != NULL)
  {
    this->Internal->SubscribeScanPlane = (STRCASECMP(subscribe, "TRUE") == 0);
  }

  if (this->Internal->SubscribeScanPlane && this->OutputChannels.size() != 2)
  {
    LOG_ERROR("Scan plane switching requested but there are not exactly two output channels.");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceElement, rootConfigElement);
  deviceElement->SetAttribute("IniFileName", this->IniFileName);
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::GetFullIniFilePath(std::string& fullPath)
{
  if (this->IniFileName == NULL)
  {
    LOG_ERROR("Ini file name has not been set");
    return PLUS_FAIL;
  }
  fullPath = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory() + std::string("/") + this->IniFileName;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void vtkPlusBkProFocusCameraLinkVideoSource::SetImagingMode(ImagingModeType imagingMode)
{
  this->ImagingMode = imagingMode;
  // always keep the receiver in RF mode and if B-mode image is requested then do the B-mode conversion in this class
  // this->Internal->PlusReceiver.SetImagingMode(imagingMode);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusBkProFocusCameraLinkVideoSource::NotifyConfigured()
{
  if (!this->Internal->SubscribeScanPlane && this->OutputChannels.size() > 1)
  {
    LOG_WARNING("vtkPlusBkProFocusCameraLinkVideoSource is expecting one output channel and there are " << this->OutputChannels.size() << " channels. First output channel will be used.");
  }

  if (this->OutputChannels.empty())
  {
    LOG_ERROR("No output channels defined for vtkPlusBkProFocusCameraLinkVideoSource. Cannot proceed.");
    this->CorrectlyConfigured = false;
    return PLUS_FAIL;
  }

  if (!this->Internal->SubscribeScanPlane)
  {
    this->Internal->Channel = this->OutputChannels[0];
  }
  else
  {
    this->Internal->Channel = this->FindChannelByPlane();
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
vtkPlusChannel* vtkPlusBkProFocusCameraLinkVideoSource::FindChannelByPlane()
{
  if (this->OutputChannels.size() != 2)
  {
    LOG_ERROR("vtkPlusBkProFocusCameraLinkVideoSource::FindChannelByPlane failed: expected two output channels");
    return NULL;
  }

  std::string desiredPlane;
  if (this->Internal->CurrentPlane == Transverse)
  {
    desiredPlane = "Transverse";
  }
  else
  {
    desiredPlane = "Sagittal";
  }

  for (ChannelContainerIterator it = this->OutputChannels.begin(); it != this->OutputChannels.end(); ++it)
  {
    std::string plane;
    if ((*it)->GetCustomAttribute("Plane", plane) != PLUS_SUCCESS)
    {
      LOG_ERROR("Plane switching requested but channel \"" << (*it)->GetChannelId() << "\" doesn't have attribute \"Plane\" defined. Please fix configuration.");
      continue;
    }
    if (plane.compare(desiredPlane) == 0)
    {
      return *it;
    }
  }
  LOG_ERROR(desiredPlane << " requested but no channel with custom attribute Plane::" << desiredPlane << " detected.");
  return NULL;
}
