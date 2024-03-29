/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "QPlusToolStateDisplayWidget.h"

// PlusLib includes
//#include <igsioTrackedFrame.h>
#include <vtkPlusChannel.h>
//#include <vtkIGSIOTrackedFrameList.h>

// Qt includes
#include <QGridLayout>
#include <QLabel>
#include <QTextEdit>

//-----------------------------------------------------------------------------
QPlusToolStateDisplayWidget::QPlusToolStateDisplayWidget(QWidget* aParent, Qt::WindowFlags aFlags)
  : QWidget(aParent, aFlags)
  , m_SelectedChannel(NULL)
  , m_Initialized(false)
{
  m_ToolNameLabels.clear();
  m_ToolStateLabels.clear();

  // Create default appearance
  QGridLayout* grid = new QGridLayout(this);
  grid->setMargin(0);
  grid->setSpacing(0);
  QLabel* uninitializedLabel = new QLabel(tr("Tool state display is unavailable until connected to a device set."), this);
  uninitializedLabel->setWordWrap(true);
  grid->addWidget(uninitializedLabel);
  m_ToolNameLabels.push_back(uninitializedLabel);
  this->setLayout(grid);
}

//-----------------------------------------------------------------------------
QPlusToolStateDisplayWidget::~QPlusToolStateDisplayWidget()
{
  m_ToolNameLabels.clear();
  m_ToolStateLabels.clear();

  m_SelectedChannel = NULL;
}

//-----------------------------------------------------------------------------
PlusStatus QPlusToolStateDisplayWidget::InitializeTools(vtkPlusChannel* aChannel, bool aConnectionSuccessful)
{
  LOG_TRACE("QPlusToolStateDisplayWidget::InitializeTools");

  // Clear former content
  if (this->layout())
  {
    delete this->layout();
  }
  for (std::vector<QLabel*>::iterator it = m_ToolNameLabels.begin(); it != m_ToolNameLabels.end(); ++it)
  {
    delete (*it);
  }
  m_ToolNameLabels.clear();
  for (std::vector<QTextEdit*>::iterator it = m_ToolStateLabels.begin(); it != m_ToolStateLabels.end(); ++it)
  {
    delete (*it);
  }
  m_ToolStateLabels.clear();

  // If connection was unsuccessful, create default appearance
  if (!aConnectionSuccessful)
  {
    QGridLayout* grid = new QGridLayout(this);
    grid->setMargin(0);
    grid->setSpacing(0);
    QLabel* uninitializedLabel = new QLabel(tr("Tool state display is unavailable until connected to a device set."), this);
    uninitializedLabel->setWordWrap(true);
    grid->addWidget(uninitializedLabel);
    m_ToolNameLabels.push_back(uninitializedLabel);
    this->setLayout(grid);

    m_Initialized = false;

    return PLUS_SUCCESS;
  }

  m_SelectedChannel = aChannel;

  // Fail if data collector or tracker is not initialized (once the content was deleted)
  if (m_SelectedChannel == NULL)
  {
    LOG_ERROR("Data source is missing!");
    return PLUS_FAIL;
  }

  // Get transforms
  std::vector<igsioTransformName> transformNames;
  igsioTrackedFrame trackedFrame;
  m_SelectedChannel->GetTrackedFrame(trackedFrame);
  trackedFrame.GetFrameTransformNameList(transformNames);

  // Set up layout
  QGridLayout* grid = new QGridLayout(this);
  grid->setColumnStretch(transformNames.size(), 1);
  grid->setSpacing(2);
  grid->setVerticalSpacing(4);
  grid->setContentsMargins(4, 4, 4, 4);

  m_ToolStateLabels.resize(transformNames.size(), NULL);

  int i;
  std::vector<igsioTransformName>::iterator it;
  for (it = transformNames.begin(), i = 0; it != transformNames.end(); ++it, ++i)
  {
    // Assemble tool name and add label to layout and label list
    QString toolNameString = QString("%1: %2").arg(i).arg(it->GetTransformName().c_str());

    QLabel* toolNameLabel = new QLabel(this);
    toolNameLabel->setText(toolNameString);
    toolNameLabel->setToolTip(toolNameString);
    QSizePolicy sizePolicyNameLabel(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    sizePolicyNameLabel.setHorizontalStretch(2);
    toolNameLabel->setSizePolicy(sizePolicyNameLabel);
    toolNameLabel->setMinimumHeight(24);
    grid->addWidget(toolNameLabel, i, 0, Qt::AlignLeft);
    m_ToolNameLabels.push_back(toolNameLabel);

    // Create tool status label and add it to layout and label list
    QTextEdit* toolStateLabel = new QTextEdit("N/A", this);
    toolStateLabel->setTextColor(QColor::fromRgb(96, 96, 96));
    toolStateLabel->setMaximumHeight(18);
    toolStateLabel->setLineWrapMode(QTextEdit::NoWrap);
    toolStateLabel->setReadOnly(true);
    toolStateLabel->setFrameShape(QFrame::NoFrame);
    toolStateLabel->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    toolStateLabel->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    toolStateLabel->setAlignment(Qt::AlignRight);
    toolStateLabel->setMinimumHeight(24);
    QSizePolicy sizePolicyStateLabel(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    sizePolicyStateLabel.setHorizontalStretch(1);
    toolStateLabel->setSizePolicy(sizePolicyStateLabel);
    grid->addWidget(toolStateLabel, i, 1, Qt::AlignRight);
    m_ToolStateLabels[i] = toolStateLabel;
  }

  this->setLayout(grid);

  m_Initialized = true;

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
bool QPlusToolStateDisplayWidget::IsInitialized()
{
  return m_Initialized;
}

//-----------------------------------------------------------------------------
PlusStatus QPlusToolStateDisplayWidget::Update()
{
  if (! m_Initialized)
  {
    LOG_ERROR("Widget is not inialized!");
    return PLUS_FAIL;
  }

  // Re-initialize widget if enabled statuses have changed
  int numberOfDisplayedTools = 0;

  // Get transforms
  std::vector<igsioTransformName> transformNames;
  igsioTrackedFrame trackedFrame;
  m_SelectedChannel->GetTrackedFrame(trackedFrame);
  trackedFrame.GetFrameTransformNameList(transformNames);

  if (transformNames.size() != m_ToolStateLabels.size())
  {
    LOG_WARNING("Tool number inconsistency!");

    if (InitializeTools(m_SelectedChannel, true) != PLUS_SUCCESS)
    {
      LOG_ERROR("Re-initializing tool state widget failed");
      return PLUS_FAIL;
    }
  }

  std::vector<igsioTransformName>::iterator transformIt;
  std::vector<QTextEdit*>::iterator labelIt;
  for (transformIt = transformNames.begin(), labelIt = m_ToolStateLabels.begin(); transformIt != transformNames.end() && labelIt != m_ToolStateLabels.end(); ++transformIt, ++labelIt)
  {
    QTextEdit* label = (*labelIt);

    if (label == NULL)
    {
      LOG_WARNING("Invalid tool state label");
      continue;
    }

    ToolStatus status(TOOL_INVALID);
    if (trackedFrame.GetFrameTransformStatus(*transformIt, status) != PLUS_SUCCESS)
    {
      std::string transformNameStr;
      transformIt->GetTransformName(transformNameStr);
      LOG_WARNING("Unable to get transform status for transform" << transformNameStr);
      label->setText("STATUS ERROR");
      label->setTextColor(QColor::fromRgb(223, 0, 0));
    }
    else
    {
      label->setText(igsioCommon::ConvertToolStatusToString(status).c_str());
      switch (status)
      {
      case (TOOL_OK):
        label->setTextColor(Qt::green);
        break;
      default:
        label->setTextColor(QColor::fromRgb(223, 0, 0));
        break;
      }
    }
  }

  return PLUS_SUCCESS;
}
