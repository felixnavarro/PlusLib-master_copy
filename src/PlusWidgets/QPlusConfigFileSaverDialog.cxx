/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "igsioCommon.h"
#include "QPlusConfigFileSaverDialog.h"

// VTK includes
#include <vtkXMLUtilities.h>
#include <vtkXMLDataElement.h>

// Qt includes
#include <QFileDialog>
#include <QSettings>
#include <QString>

//-----------------------------------------------------------------------------
QPlusConfigFileSaverDialog::QPlusConfigFileSaverDialog(QWidget* aParent)
  : QDialog(aParent)
{
  ui.setupUi(this);

  connect(ui.pushButton_OpenDestinationDirectory, SIGNAL(clicked()), this, SLOT(OpenDestinationDirectoryClicked()));
  connect(ui.pushButton_Save, SIGNAL(clicked()), this, SLOT(SaveClicked()));

  SetDestinationDirectory(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory());

  ReadConfiguration();
}

//-----------------------------------------------------------------------------
QPlusConfigFileSaverDialog::~QPlusConfigFileSaverDialog()
{
}

//-----------------------------------------------------------------------------
void QPlusConfigFileSaverDialog::OpenDestinationDirectoryClicked()
{
  LOG_TRACE("QPlusConfigFileSaverDialog::OpenDestinationDirectoryClicked");

  // Directory open dialog for selecting configuration directory
  QString dirName = QFileDialog::getExistingDirectory(NULL, QString(tr("Select destination directory")), m_DestinationDirectory);
  if (dirName.isNull())
  {
    return;
  }

  this->SetDestinationDirectory(dirName.toStdString());

  ui.lineEdit_DestinationDirectory->setText(dirName);
  ui.lineEdit_DestinationDirectory->setToolTip(dirName);
}

//-----------------------------------------------------------------------------
void QPlusConfigFileSaverDialog::SetDestinationDirectory(const std::string& aDirectory)
{
  LOG_TRACE("QPlusConfigFileSaverDialog::SetDestinationDirectory(" << aDirectory << ")");

  m_DestinationDirectory = QString::fromStdString(aDirectory);

  ui.lineEdit_DestinationDirectory->setText(m_DestinationDirectory);
  ui.lineEdit_DestinationDirectory->setToolTip(m_DestinationDirectory);
}

//-----------------------------------------------------------------------------
PlusStatus QPlusConfigFileSaverDialog::ReadConfiguration()
{
  LOG_TRACE("QPlusConfigFileSaverDialog::ReadConfiguration");

  // Find Device set element
  vtkXMLDataElement* dataCollection = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationData()->FindNestedElementWithName("DataCollection");
  if (dataCollection == NULL)
  {
    LOG_ERROR("No DataCollection element is found in the XML tree!");
    return PLUS_FAIL;
  }

  vtkXMLDataElement* deviceSet = dataCollection->FindNestedElementWithName("DeviceSet");
  if (deviceSet == NULL)
  {
    LOG_ERROR("No DeviceSet element is found in the XML tree!");
    return PLUS_FAIL;
  }

  // Get name and description
  bool isEqual(true);
  if (igsioCommon::XML::SafeCheckAttributeValueInsensitive(*deviceSet, "Name", "", isEqual) == PLUS_FAIL || isEqual)
  {
    LOG_WARNING("Name attribute cannot be found in DeviceSet element!");
    return PLUS_FAIL;
  }

  if (igsioCommon::XML::SafeCheckAttributeValueInsensitive(*deviceSet, "Description", "", isEqual) == PLUS_FAIL || isEqual)
  {
    LOG_WARNING("Description attribute cannot be found in DeviceSet element!");
    return PLUS_FAIL;
  }

  // Set text field values
  ui.lineEdit_DeviceSetName->setText(deviceSet->GetAttribute("Name"));
  ui.textEdit_Description->setText(deviceSet->GetAttribute("Description"));

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void QPlusConfigFileSaverDialog::SaveClicked()
{
  LOG_TRACE("QPlusConfigFileSaverDialog::SaveClicked");

  // Get root element
  vtkXMLDataElement* configRootElement = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationData();
  if (configRootElement == NULL)
  {
    LOG_ERROR("No configuration XML found!");
    return;
  }

  // Find Device set element
  vtkXMLDataElement* dataCollection = configRootElement->FindNestedElementWithName("DataCollection");
  if (dataCollection == NULL)
  {
    LOG_ERROR("No DataCollection element is found in the XML tree!");
    return;
  }

  vtkXMLDataElement* deviceSet = dataCollection->FindNestedElementWithName("DeviceSet");
  if (deviceSet == NULL)
  {
    LOG_ERROR("No DeviceSet element is found in the XML tree!");
    return;
  }
  // Set name and description to XML
  deviceSet->SetAttribute("Name", ui.lineEdit_DeviceSetName->text().toStdString().c_str());
  deviceSet->SetAttribute("Description", ui.textEdit_Description->toPlainText().toStdString().c_str());

  // Display file save dialog and save XML
  QString filter = QString(tr("XML files ( *.xml );;"));
  QString destinationFile = QString("%1/%2").arg(m_DestinationDirectory).arg(vtkPlusConfig::GetInstance()->GetNewDeviceSetConfigurationFileName().c_str());
  QString fileName = QFileDialog::getSaveFileName(NULL, tr("Save result configuration XML"), destinationFile, filter);

  if (!fileName.isNull())
  {
    igsioCommon::XML::PrintXML(fileName.toStdString().c_str(), vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationData());
    LOG_INFO("Device set configuration saved as '" << fileName.toStdString() << "'");
  }

  accept();
}