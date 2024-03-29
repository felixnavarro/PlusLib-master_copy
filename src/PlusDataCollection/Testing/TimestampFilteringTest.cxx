/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

/*!
  \file TimestampFilteringTest.cxx
  \brief This program tests the timestamp filtering algorithm.
*/

// Local includes
#include "PlusConfigure.h"
#ifdef PLUS_RENDERING_ENABLED
#include "PlusPlotter.h"
#endif
#include "vtkPlusBuffer.h"
#include "vtkPlusHTMLGenerator.h"
#include "vtkIGSIOSequenceIO.h"
#include "vtkIGSIOTrackedFrameList.h"

// VTK includes
#include <vtksys/CommandLineArguments.hxx>
#include <vtksys/SystemTools.hxx>
#include <vtkTable.h>


int main(int argc, char** argv)
{
  int numberOfErrors(0);

  bool printHelp(false);
  std::string inputMetafile;
  std::string inputBaselineReportFilePath("");
  int inputAveragedItemsForFiltering(20);
  double inputMaxTimestampDifference(0.080);
  double inputMinStdevReductionFactor(3.0);
  std::string inputTransformName;

  int verboseLevel = vtkPlusLogger::LOG_LEVEL_UNDEFINED;

  vtksys::CommandLineArguments args;
  args.Initialize(argc, argv);

  args.AddArgument("--transform", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputTransformName, "Transform name used for generating timestamp filtering");
  args.AddArgument("--help", vtksys::CommandLineArguments::NO_ARGUMENT, &printHelp, "Print this help.");
  args.AddArgument("--source-seq-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputMetafile, "Input sequence metafile.");
  args.AddArgument("--averaged-items-for-filtering", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputAveragedItemsForFiltering, "Number of averaged items used for filtering (Default: 20).");
  args.AddArgument("--max-timestamp-difference", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputMaxTimestampDifference, "The maximum difference between the filtered and nonfiltered timestamps for each frame (Default: 0.08s).");
  args.AddArgument("--min-stdev-reduction-factor", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputMinStdevReductionFactor, "Minimum factor that the filtering should reduces the standard deviation of the frame periods on filtered data (Default: 3.0 ).");
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");

  if (!args.Parse())
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  if (printHelp)
  {
    std::cout << args.GetHelp() << std::endl;
    exit(EXIT_SUCCESS);
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  if (inputMetafile.empty())
  {
    std::cerr << "input-metafile argument required!" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  igsioTransformName transformName;
  if (transformName.SetTransformName(inputTransformName.c_str()) != PLUS_SUCCESS)
  {
    LOG_ERROR("Invalid transform name: " << inputTransformName);
    return EXIT_FAILURE;
  }

  // Read buffer
  LOG_INFO("Reading meta file...");
  vtkSmartPointer<vtkIGSIOTrackedFrameList> trackerFrameList = vtkSmartPointer<vtkIGSIOTrackedFrameList>::New();
  if (vtkIGSIOSequenceIO::Read(inputMetafile, trackerFrameList) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to read sequence metafile from file: " << inputMetafile);
  }

  LOG_INFO("Copy buffer to tracker buffer...");
  vtkSmartPointer<vtkPlusBuffer> trackerBuffer = vtkSmartPointer<vtkPlusBuffer>::New();
  trackerBuffer->SetTimeStampReporting(true);
  // compute filtered timestamps now to test the filtering
  if (trackerBuffer->CopyTransformFromTrackedFrameList(trackerFrameList, vtkPlusBuffer::READ_UNFILTERED_COMPUTE_FILTERED_TIMESTAMPS, transformName) != PLUS_SUCCESS)
  {
    LOG_ERROR("CopyDefaultTrackerDataToBuffer failed");
    numberOfErrors++;
  }

  // Check filtering results
  //************************

  // 1. The maximum difference between the filtered and nonfiltered timestamps for each frame shall be below a specified threshold (fabs(timestamp-unfilteredTimestamp) < maxTimestampDifference)
  double maxTimestampDifference(0);
  for (BufferItemUidType item = trackerBuffer->GetOldestItemUidInBuffer(); item <= trackerBuffer->GetLatestItemUidInBuffer(); ++item)
  {
    StreamBufferItem bufferItem;
    if (trackerBuffer->GetStreamBufferItem(item, &bufferItem) != ITEM_OK)
    {
      LOG_WARNING("Failed to get buffer item with UID: " << item);
      numberOfErrors++;
      continue;
    }

    double timestampDifference = fabs(bufferItem.GetUnfilteredTimestamp(0) - bufferItem.GetFilteredTimestamp(0));
    if (timestampDifference > maxTimestampDifference)
    {
      maxTimestampDifference = timestampDifference;
    }
    if (timestampDifference > inputMaxTimestampDifference)
    {
      LOG_ERROR("Difference between the filtered and nonfiltered timestamps are higher than the threshold (UID: " << item
                << ", unfilteredTimestamp: " << std::fixed << bufferItem.GetUnfilteredTimestamp(0)
                << ", filteredTimestamp: " << std::fixed << bufferItem.GetFilteredTimestamp(0)
                << ", timestamp diference: " << timestampDifference << ", threshold: " << inputMaxTimestampDifference << ")");
      numberOfErrors++;
    }
  }

  LOG_INFO("Maximum filtered and unfiltered timestamp difference: " << maxTimestampDifference * 1000 << "ms");

  //2. The standard deviation of the frame periods in the filtered data should be better than without filtering (stdevFramePeriodsUnfiltered / stdevFramePeriodsFiltered < minStdevReductionFactor)
  vnl_vector<double>unfilteredFramePeriods(trackerBuffer->GetNumberOfItems() - 1);
  vnl_vector<double>filteredFramePeriods(trackerBuffer->GetNumberOfItems() - 1);
  int i = 0;
  for (BufferItemUidType item = trackerBuffer->GetOldestItemUidInBuffer(); item < trackerBuffer->GetLatestItemUidInBuffer(); ++item)
  {
    StreamBufferItem bufferItem_1;
    if (trackerBuffer->GetStreamBufferItem(item, &bufferItem_1) != ITEM_OK)
    {
      LOG_WARNING("Failed to get buffer item with UID: " << item);
      numberOfErrors++;
      continue;
    }

    StreamBufferItem bufferItem_2;
    if (trackerBuffer->GetStreamBufferItem(item + 1, &bufferItem_2) != ITEM_OK)
    {
      LOG_WARNING("Failed to get buffer item with UID: " << item + 1);
      numberOfErrors++;
      continue;
    }

    unfilteredFramePeriods.put(i, (bufferItem_2.GetUnfilteredTimestamp(0) - bufferItem_1.GetUnfilteredTimestamp(0)) / (bufferItem_2.GetIndex() - bufferItem_1.GetIndex()));
    filteredFramePeriods.put(i, (bufferItem_2.GetFilteredTimestamp(0) - bufferItem_1.GetFilteredTimestamp(0)) / (bufferItem_2.GetIndex() - bufferItem_1.GetIndex()));
    i++;
  }

  // Compute frame period means
  double unfilteredFramePeriodsMean = unfilteredFramePeriods.mean();
  double filteredFramePeriodsMean = filteredFramePeriods.mean();

  // Compute frame period stdev
  vnl_vector<double> diffFromMeanUnfilteredFramePeriods = unfilteredFramePeriods - unfilteredFramePeriodsMean;
  double unfilteredFramePeriodsStd = sqrt(diffFromMeanUnfilteredFramePeriods.squared_magnitude() / diffFromMeanUnfilteredFramePeriods.size());

  LOG_INFO("Unfiltered frame periods mean: " << std::fixed << unfilteredFramePeriodsMean * 1000 << "ms stdev: " << unfilteredFramePeriodsStd * 1000 << "ms");

  vnl_vector<double> diffFromMeanFilteredFramePeriods = filteredFramePeriods - filteredFramePeriodsMean;
  double filteredFramePeriodsStd = sqrt(diffFromMeanFilteredFramePeriods.squared_magnitude() / diffFromMeanFilteredFramePeriods.size());

  LOG_INFO("Filtered frame periods mean: " << std::fixed << filteredFramePeriodsMean * 1000 << "ms stdev: " << filteredFramePeriodsStd * 1000 << "ms");

  LOG_INFO("Filtered data frame period reduction factor: " << std::fixed << unfilteredFramePeriodsStd / filteredFramePeriodsStd);

  if (unfilteredFramePeriodsStd / filteredFramePeriodsStd < inputMinStdevReductionFactor)
  {
    LOG_ERROR("Filtered data frame period reduction factor is smaller than the threshold (factor: " << std::fixed << unfilteredFramePeriodsStd / filteredFramePeriodsStd << ", threshold: " << inputMinStdevReductionFactor << ")");
    numberOfErrors++;
  }


  vtkSmartPointer<vtkTable> timestampReportTable = vtkSmartPointer<vtkTable>::New();
  if (trackerBuffer->GetTimeStampReportTable(timestampReportTable) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get time stamp report table!");
    numberOfErrors++;
  }

  std::string reportFile = vtksys::SystemTools::GetCurrentWorkingDirectory() + std::string("/TimestampReport.txt");

#ifdef PLUS_RENDERING_ENABLED
  if (PlusPlotter::WriteTableToFile(*timestampReportTable, reportFile.c_str()) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to write table to file");
    return PLUS_FAIL;
  }
#endif

  if (vtkPlusLogger::Instance()->GetLogLevel() >= vtkPlusLogger::LOG_LEVEL_DEBUG)
  {
    timestampReportTable->Dump();
  }

  if (numberOfErrors != 0)
  {
    LOG_INFO("Test failed!");
    return EXIT_FAILURE;
  }

  LOG_INFO("Test completed successfully!");
  return EXIT_SUCCESS;
}
