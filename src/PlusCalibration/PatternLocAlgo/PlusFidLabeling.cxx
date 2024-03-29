/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "PlusConfigure.h"
#include "PlusFidLabeling.h"
#include "igsioMath.h"
#include "PlusFidSegmentation.h"

// VTK includes
#include <vtkPlane.h>
#include <vtkTriangle.h>

// STL includes
#include <algorithm>

//-----------------------------------------------------------------------------
PlusFidLabeling::PlusFidLabeling()
  : m_ApproximateSpacingMmPerPixel(-1.0)
  , m_MaxAngleDiff(-1.0)
  , m_MinLinePairDistMm(-1.0)
  , m_MaxLinePairDistMm(-1.0)
  , m_MinLinePairAngleRad(-1.0)
  , m_MaxLinePairAngleRad(-1.0)
  , m_MaxLineShiftMm(10.0)
  , m_MaxLinePairDistanceErrorPercent(-1.0)
  , m_MinThetaRad(-1.0)
  , m_MaxThetaRad(-1.0)
  , m_DotsFound(false)
  , m_AngleToleranceRad(-1.0)
  , m_InclinedLineAngleRad(-1.0)
  , m_PatternIntensity(-1.0)
{
  m_FrameSize[0] = 0;
  m_FrameSize[1] = 0;
  m_FrameSize[2] = 1;
}

//-----------------------------------------------------------------------------
PlusFidLabeling::~PlusFidLabeling()
{
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::UpdateParameters()
{
  LOG_TRACE("FidLabeling::UpdateParameters");

  // Distance between lines (= distance between planes of the N-wires)
  std::vector<PlusFidPattern*>::size_type numOfPatterns = m_Patterns.size();
  double epsilon = 0.001;

  // Compute normal of each pattern and evaluate the other wire endpoints if they are on the computed plane
  std::vector<vtkSmartPointer<vtkPlane>> planes;
  for (std::vector<PlusFidPattern*>::size_type i = 0; i < numOfPatterns; ++i)
  {
    double normal[3] = {0, 0, 0};
    double v1[3], v2[3], v3[3];
    memcpy(v1, m_Patterns[i]->GetWires()[0].EndPointFront, sizeof(double) * 3);
    memcpy(v2, m_Patterns[i]->GetWires()[0].EndPointBack, sizeof(double) * 3);
    memcpy(v3, m_Patterns[i]->GetWires()[2].EndPointFront, sizeof(double) * 3);
    vtkTriangle::ComputeNormal(v1, v2, v3, normal);

    vtkSmartPointer<vtkPlane> plane = vtkSmartPointer<vtkPlane>::New();
    plane->SetNormal(normal);
    plane->SetOrigin(v1);
    planes.push_back(plane);

    std::array<double, 3> point1 = { m_Patterns[i]->GetWires()[1].EndPointFront[0], m_Patterns[i]->GetWires()[1].EndPointFront[1], m_Patterns[i]->GetWires()[1].EndPointFront[2] };
    double distance1F = plane->DistanceToPlane(point1.data());
    std::array<double, 3> point2 = { m_Patterns[i]->GetWires()[1].EndPointBack[0], m_Patterns[i]->GetWires()[1].EndPointBack[1], m_Patterns[i]->GetWires()[1].EndPointBack[2] };
    double distance1B = plane->DistanceToPlane(point2.data());
    std::array<double, 3> point3 = { m_Patterns[i]->GetWires()[2].EndPointBack[0], m_Patterns[i]->GetWires()[2].EndPointBack[1], m_Patterns[i]->GetWires()[2].EndPointBack[2] };
    double distance2B = plane->DistanceToPlane(point3.data());

    if (distance1F > epsilon || distance1B > epsilon || distance2B > epsilon)
    {
      LOG_ERROR("Pattern number " << i << " is invalid: the endpoints are not on the same plane");
    }
  }

  // Compute distances between each NWire pairs and determine the smallest and the largest distance
  double maxNPlaneDistance = -1.0;
  double minNPlaneDistance = std::numeric_limits<double>::max();
  m_MinLinePairAngleRad = vtkMath::Pi() / 2.0;
  m_MaxLinePairAngleRad = 0;
  for (std::vector<PlusFidPattern*>::size_type i = numOfPatterns - 1; i > 0; --i)
  {
    for (int j = static_cast<int>(i) - 1; j >= 0; --j)
    {
      double distance = planes.at(i)->DistanceToPlane(planes.at(j)->GetOrigin());
      if (maxNPlaneDistance < distance)
      {
        maxNPlaneDistance = distance;
      }
      if (minNPlaneDistance > distance)
      {
        minNPlaneDistance = distance;
      }
      double angle = acos(vtkMath::Dot(planes.at(i)->GetNormal(), planes.at(j)->GetNormal())
                          / vtkMath::Norm(planes.at(i)->GetNormal())
                          / vtkMath::Norm(planes.at(j)->GetNormal()));
      // Normalize between -pi/2 .. +pi/2
      if (angle > vtkMath::Pi() / 2)
      {
        angle -= vtkMath::Pi();
      }
      else if (angle < -vtkMath::Pi() / 2)
      {
        angle += vtkMath::Pi();
      }
      // Return the absolute value (0..+pi/2)
      angle = fabs(angle);
      if (angle < m_MinLinePairAngleRad)
      {
        m_MinLinePairAngleRad = angle;
      }
      if (angle > m_MaxLinePairAngleRad)
      {
        m_MaxLinePairAngleRad = angle;
      }
    }
  }

  m_MaxLinePairDistMm = maxNPlaneDistance * (1.0 + (m_MaxLinePairDistanceErrorPercent / 100.0));
  m_MinLinePairDistMm = minNPlaneDistance * (1.0 - (m_MaxLinePairDistanceErrorPercent / 100.0));
  LOG_DEBUG("Line pair distance - computed min: " << minNPlaneDistance << " , max: " << maxNPlaneDistance << ";  allowed min: " << m_MinLinePairDistMm << ", max: " << m_MaxLinePairDistMm);
}

//-----------------------------------------------------------------------------
PlusStatus PlusFidLabeling::ReadConfiguration(vtkXMLDataElement* configData, double minThetaRad, double maxThetaRad)
{
  LOG_TRACE("FidLabeling::ReadConfiguration");

  XML_FIND_NESTED_ELEMENT_OPTIONAL(segmentationParameters, configData, "Segmentation");
  if (!segmentationParameters)
  {
    segmentationParameters = igsioXmlUtils::GetNestedElementWithName(configData, "Segmentation");
    PlusFidSegmentation::SetDefaultSegmentationParameters(segmentationParameters);
  }

  XML_READ_SCALAR_ATTRIBUTE_WARNING(double, ApproximateSpacingMmPerPixel, segmentationParameters);


  //if the tolerance parameters are computed automatically
  int computeSegmentationParametersFromPhantomDefinition(0);
  if (segmentationParameters->GetScalarAttribute("ComputeSegmentationParametersFromPhantomDefinition", computeSegmentationParametersFromPhantomDefinition)
      && computeSegmentationParametersFromPhantomDefinition != 0)
  {
    LOG_WARNING("Automatic computation of the MaxLinePairDistanceErrorPercent and MaxAngleDifferenceDegrees parameters are not yet supported, use the values that are in the config file");
  }

  XML_READ_SCALAR_ATTRIBUTE_WARNING(double, MaxLinePairDistanceErrorPercent, segmentationParameters);
  XML_READ_SCALAR_ATTRIBUTE_WARNING(double, MaxAngleDifferenceDegrees, segmentationParameters);
  XML_READ_SCALAR_ATTRIBUTE_WARNING(double, AngleToleranceDegrees, segmentationParameters);
  XML_READ_SCALAR_ATTRIBUTE_WARNING(double, MaxLineShiftMm, segmentationParameters);

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, InclinedLineAngleDegrees, segmentationParameters);   // only for CIRS

  UpdateParameters();

  m_MinThetaRad = minThetaRad;
  m_MaxThetaRad = maxThetaRad;

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::Clear()
{
  //LOG_TRACE("FidLabeling::Clear");
  m_DotsVector.clear();
  m_LinesVector.clear();
  m_FoundDotsCoordinateValue.clear();
  m_Results.clear();
  m_FoundLines.clear();

  std::vector<PlusFidLine> emptyLine;
  m_LinesVector.push_back(emptyLine);   //initializing the 0 vector of lines (unused)
  m_LinesVector.push_back(emptyLine);   //initializing the 1 vector of lines (unused)
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::SetFrameSize(const FrameSizeType& frameSize)
{
  LOG_TRACE("FidLineFinder::SetFrameSize(" << frameSize[0] << ", " << frameSize[1] << ")");

  if ((m_FrameSize[0] == frameSize[0]) && (m_FrameSize[1] == frameSize[1]))
  {
    return;
  }

  m_FrameSize[0] = frameSize[0];
  m_FrameSize[1] = frameSize[1];
  m_FrameSize[2] = 1;

  if (m_FrameSize[0] < 0 || m_FrameSize[1] < 0)
  {
    LOG_ERROR("Dimensions of the frame size are not positive!");
    return;
  }
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::SetAngleToleranceDeg(double value)
{
  m_AngleToleranceRad = vtkMath::RadiansFromDegrees(value);
}

//-----------------------------------------------------------------------------
bool PlusFidLabeling::SortCompare(const std::vector<double>& temporaryLine1, const std::vector<double>& temporaryLine2)
{
  //used for SortPointsByDistanceFromOrigin
  return temporaryLine1[1] < temporaryLine2[1];
}

//-----------------------------------------------------------------------------
PlusFidLine PlusFidLabeling::SortPointsByDistanceFromStartPoint(PlusFidLine& fiducials)
{
  std::vector<std::vector<double>> temporaryLine;
  PlusFidDot startPointIndex = m_DotsVector[fiducials.GetStartPointIndex()];

  for (unsigned int i = 0 ; i < fiducials.GetNumberOfPoints() ; i++)
  {
    std::vector<double> temp;
    PlusFidDot point = m_DotsVector[fiducials.GetPoint(i)];
    temp.push_back(fiducials.GetPoint(i));
    temp.push_back(sqrt((startPointIndex.GetX() - point.GetX()) * (startPointIndex.GetX() - point.GetX()) + (startPointIndex.GetY() - point.GetY()) * (startPointIndex.GetY() - point.GetY())));
    temporaryLine.push_back(temp);
  }

  //sort the indexes by the distance of their respective pioint to the startPointIndex
  std::sort(temporaryLine.begin(), temporaryLine.end(), PlusFidLabeling::SortCompare);

  PlusFidLine resultLine = fiducials;

  for (unsigned int i = 0 ; i < fiducials.GetNumberOfPoints() ; i++)
  {
    resultLine.SetPoint(i, temporaryLine[i][0]);
  }

  return resultLine;
}

//----------------------------------------------------------------------------
std::vector<PlusFidPattern*>& PlusFidLabeling::GetPatterns()
{
  return m_Patterns;
}

//-----------------------------------------------------------------------------
double PlusFidLabeling::ComputeSlope(PlusFidLine& line)
{
  //LOG_TRACE("FidLineFinder::ComputeSlope");
  PlusFidDot dot1 = m_DotsVector[line.GetStartPointIndex()];
  PlusFidDot dot2 = m_DotsVector[line.GetEndPointIndex()];

  double x1 = dot1.GetX();
  double y1 = dot1.GetY();

  double x2 = dot2.GetX();
  double y2 = dot2.GetY();

  double y = (y2 - y1);
  double x = (x2 - x1);

  double t;
  if (fabs(x) > fabs(y))
  {
    t = vtkMath::Pi() / 2 + atan(y / x);
  }
  else
  {
    double tanTheta = x / y;
    if (tanTheta > 0)
    {
      t = vtkMath::Pi() - atan(tanTheta);
    }
    else
    {
      t = -atan(tanTheta);
    }
  }

  assert(t >= 0 && t <= vtkMath::Pi());
  return t;
}

//-----------------------------------------------------------------------------
double PlusFidLabeling::ComputeDistancePointLine(PlusFidDot& dot, PlusFidLine& line)
{
  double x[3], y[3], z[3];

  x[0] = m_DotsVector[line.GetStartPointIndex()].GetX();
  x[1] = m_DotsVector[line.GetStartPointIndex()].GetY();
  x[2] = 0;

  y[0] = m_DotsVector[line.GetEndPointIndex()].GetX();
  y[1] = m_DotsVector[line.GetEndPointIndex()].GetY();
  y[2] = 0;

  z[0] = dot.GetX();
  z[1] = dot.GetY();
  z[2] = 0;

  return igsioMath::ComputeDistanceLinePoint(x, y, z);
}

//-----------------------------------------------------------------------------
double PlusFidLabeling::ComputeShift(PlusFidLine& line1, PlusFidLine& line2)
{
  //middle of the line 1
  double midLine1[2] =
  {
    (m_DotsVector[line1.GetStartPointIndex()].GetX() + m_DotsVector[line1.GetEndPointIndex()].GetX()) / 2,
    (m_DotsVector[line1.GetStartPointIndex()].GetY() + m_DotsVector[line1.GetEndPointIndex()].GetY()) / 2
  };
  //middle of the line 2
  double midLine2[2] =
  {
    (m_DotsVector[line2.GetStartPointIndex()].GetX() + m_DotsVector[line2.GetEndPointIndex()].GetX()) / 2,
    (m_DotsVector[line2.GetStartPointIndex()].GetY() + m_DotsVector[line2.GetEndPointIndex()].GetY()) / 2
  };
  //vector from one middle to the other
  double midLine1_to_midLine2[3] =
  {
    midLine2[0] - midLine1[0],
    midLine2[1] - midLine1[1],
    0
  };

  double line1vector[3] =
  {
    m_DotsVector[line1.GetEndPointIndex()].GetX() - m_DotsVector[line1.GetStartPointIndex()].GetX(),
    m_DotsVector[line1.GetEndPointIndex()].GetY() - m_DotsVector[line1.GetStartPointIndex()].GetY(),
    0
  };
  vtkMath::Normalize(line1vector);   //need to normalize for the dot product to provide significant result

  double midLine1_to_midLine2_projectionToLine1_length = vtkMath::Dot(line1vector, midLine1_to_midLine2);

  return midLine1_to_midLine2_projectionToLine1_length;
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::UpdateNWiresResults(std::vector<PlusFidLine*>& resultLines)
{
  int numberOfLines = m_Patterns.size(); //the number of lines in the pattern
  double intensity = 0;
  std::vector<double> dotCoords;
  std::vector< std::vector<double> > foundDotsCoordinateValues = m_FoundDotsCoordinateValue;

  for (int i = 0; i < numberOfLines; ++i)
  {
    SortRightToLeft(*resultLines[i]);
  }

  for (int line = 0; line < numberOfLines; ++line)
  {
    for (unsigned int i = 0 ; i < resultLines[line]->GetNumberOfPoints() ; i++)
    {
      PlusLabelingResults result;
      result.x = m_DotsVector[resultLines[line]->GetPoint(i)].GetX();
      dotCoords.push_back(result.x);
      result.y = m_DotsVector[resultLines[line]->GetPoint(i)].GetY();
      dotCoords.push_back(result.y);
      result.patternId = 0;
      result.wireId = i;
      m_Results.push_back(result);
      foundDotsCoordinateValues.push_back(dotCoords);
      dotCoords.clear();
    }

    intensity += resultLines[line]->GetIntensity();

    m_FoundLines.push_back(*(resultLines[line]));
  }

  m_FoundDotsCoordinateValue = foundDotsCoordinateValues;
  m_PatternIntensity = intensity;
  m_DotsFound = true;
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::UpdateCirsResults(const PlusFidLine& resultLine1, const PlusFidLine& resultLine2, const PlusFidLine& resultLine3)
{
  //resultLine1 is the left line, resultLine2 is the diagonal, resultLine3 is the right line
  double intensity = 0;
  std::vector<double> dotCoords;
  std::vector<std::vector<double>> foundDotsCoordinateValues = m_FoundDotsCoordinateValue;

  for (unsigned int i = 0 ; i < resultLine1.GetNumberOfPoints() ; i++)
  {
    PlusLabelingResults result;
    result.x = m_DotsVector[resultLine1.GetPoint(i)].GetX();
    dotCoords.push_back(result.x);
    result.y = m_DotsVector[resultLine1.GetPoint(i)].GetY();
    dotCoords.push_back(result.y);
    result.patternId = 0;
    result.wireId = i;
    m_Results.push_back(result);
    foundDotsCoordinateValues.push_back(dotCoords);
    dotCoords.clear();
  }
  intensity += resultLine1.GetIntensity();

  for (unsigned int i = 0 ; i < resultLine2.GetNumberOfPoints() ; i++)
  {
    PlusLabelingResults result;
    result.x = m_DotsVector[resultLine2.GetPoint(i)].GetX();
    dotCoords.push_back(result.x);
    result.y = m_DotsVector[resultLine2.GetPoint(i)].GetY();
    dotCoords.push_back(result.y);
    result.patternId = 1;
    result.wireId = i;
    m_Results.push_back(result);
    foundDotsCoordinateValues.push_back(dotCoords);
    dotCoords.clear();
  }
  intensity += resultLine2.GetIntensity();

  for (unsigned int i = 0 ; i < resultLine3.GetNumberOfPoints() ; i++)
  {
    PlusLabelingResults result;
    result.x = m_DotsVector[resultLine3.GetPoint(i)].GetX();
    dotCoords.push_back(result.x);
    result.y = m_DotsVector[resultLine3.GetPoint(i)].GetY();
    dotCoords.push_back(result.y);
    result.patternId = 2;
    result.wireId = i;
    m_Results.push_back(result);
    foundDotsCoordinateValues.push_back(dotCoords);
    dotCoords.clear();
  }
  intensity += resultLine3.GetIntensity();

  m_FoundLines.push_back(resultLine1);
  m_FoundLines.push_back(resultLine2);
  m_FoundLines.push_back(resultLine3);

  m_FoundDotsCoordinateValue = foundDotsCoordinateValues;
  m_PatternIntensity = intensity;
  m_DotsFound = true;
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::FindPattern()
{
  //LOG_TRACE("FidLabeling::FindPattern");

  std::vector<PlusFidLine> maxPointsLines = m_LinesVector[m_LinesVector.size() - 1];
  int numberOfLines = m_Patterns.size();//the number of lines in the pattern
  int numberOfCandidateLines = maxPointsLines.size();
  std::vector<int> lineIndices(numberOfLines);
  std::vector<PlusLabelingResults> results;

  m_DotsFound = false;

  //permutation generator
  for (unsigned int i = 0 ; i < lineIndices.size() ; i++)
  {
    lineIndices[i] = lineIndices.size() - 1 - i;
  }
  lineIndices[0]--;

  bool foundPattern = false;
  do
  {
    for (int i = 0; i < numberOfLines; i++)
    {
      lineIndices[i]++;

      if (lineIndices[i] < numberOfCandidateLines - i)
      {
        // no need to carry over more
        // the i-th index is correct, so just adjust all the ones before that
        int nextIndex = lineIndices[i] + 1;
        for (int j = i - 1; j >= 0; j--)
        {
          lineIndices[j] = nextIndex;
          nextIndex++;
        }
        break; //valid permutation
      }

      // need to carry over
      if (i == numberOfLines - 1)
      {
        // we are already at the last index, cannot carry over more
        return;
      }
    }

    // We have a new permutation in lineIndices.
    // Check if the distance and angle between each possible line pairs within the permutation are within the allowed range.
    // This is a quick filtering to keep only those line combinations for further processing that may form a valid pattern.
    foundPattern = true; // assume that we've found a valid pattern (then set the flag to false if it turns out that one of the values are not within the allowed range)
    for (int i = 0 ; i < numberOfLines - 1 && foundPattern; i++)
    {
      PlusFidLine currentLine1 = maxPointsLines[lineIndices[i]];
      for (int j = i + 1 ; j < numberOfLines && foundPattern; j++)
      {
        PlusFidLine currentLine2 = maxPointsLines[lineIndices[j]];

        double angleBetweenLinesRad = PlusFidLine::ComputeAngleRad(currentLine1, currentLine2);
        if (angleBetweenLinesRad < m_AngleToleranceRad)   //The angle between 2 lines is close to 0
        {
          // Parallel lines

          // Check the distance between the lines
          double distance = ComputeDistancePointLine(m_DotsVector[currentLine1.GetStartPointIndex()], currentLine2);
          int maxLinePairDistPx = floor(m_MaxLinePairDistMm / m_ApproximateSpacingMmPerPixel + 0.5);
          int minLinePairDistPx = floor(m_MinLinePairDistMm / m_ApproximateSpacingMmPerPixel + 0.5);
          if ((distance > maxLinePairDistPx) || (distance < minLinePairDistPx))
          {
            // The distance between the lines is smaller or larger than the allowed range
            foundPattern = false;
            break;
          }

          // Check the shift (along the direction of the lines)
          double shift = ComputeShift(currentLine1, currentLine2);
          int maxLineShiftDistPx = floor(m_MaxLineShiftMm / m_ApproximateSpacingMmPerPixel + 0.5);
          //maxLineShiftDistPx = 35;
          if (fabs(shift) > maxLineShiftDistPx)
          {
            // The shift between the is larger than the allowed value
            foundPattern = false;
            break;
          }
        }
        else
        {
          // Non-parallel lines
          double minAngle = m_MinLinePairAngleRad - m_AngleToleranceRad;
          double maxAngle = m_MaxLinePairAngleRad + m_AngleToleranceRad;
          if ((angleBetweenLinesRad > maxAngle) || (angleBetweenLinesRad < minAngle))
          {
            // The angle between the patterns are not in the valid range
            foundPattern = false;
            break;
          }

          // If there are common endpoints between the lines then we check if the angle between the lines is correct
          // (Needed e.g., for the CIRS phantom model 45)
          int commonPointIndex = -1; // <0 if there are no common points between the lines, >=0 if there is a common endpoint
          if ((currentLine1.GetStartPointIndex() == currentLine2.GetStartPointIndex()) || (currentLine1.GetStartPointIndex() == currentLine2.GetEndPointIndex()))
          {
            commonPointIndex = currentLine1.GetStartPointIndex();
          }
          else if ((currentLine1.GetEndPointIndex() == currentLine2.GetStartPointIndex()) || (currentLine1.GetEndPointIndex() == currentLine2.GetEndPointIndex()))
          {
            commonPointIndex = currentLine1.GetEndPointIndex();
          }
          if (commonPointIndex != -1)
          {
            // there is a common point
            double minAngle = m_InclinedLineAngleRad - m_AngleToleranceRad;
            double maxAngle = m_InclinedLineAngleRad + m_AngleToleranceRad;
            if ((angleBetweenLinesRad > maxAngle) || (angleBetweenLinesRad < minAngle))
            {
              // The angle between the patterns are not in the valid range
              foundPattern = false;
              break;
            }
          }
        }
      }
    }
  }
  while ((lineIndices[numberOfLines - 1] != numberOfCandidateLines - numberOfLines + 2) && (!foundPattern));

  if (foundPattern)   //We have the right permutation of lines in lineIndices
  {
    //Update the results, this part is not generic but depends on the FidPattern we are looking for
    PlusNWire* nWire = dynamic_cast<PlusNWire*>(m_Patterns.at(0));
    PlusCoplanarParallelWires* coplanarParallelWire = dynamic_cast<PlusCoplanarParallelWires*>(m_Patterns.at(0));
    if (nWire != nullptr)   // NWires
    {
      std::vector<PlusFidLine*> resultLines(numberOfLines);
      std::vector<double> resultLineMiddlePointYs;

      for (std::vector<int>::iterator it = lineIndices.begin(); it != lineIndices.end(); ++it)
      {
        resultLineMiddlePointYs.push_back((m_DotsVector[maxPointsLines[*it].GetStartPointIndex()].GetY() + m_DotsVector[maxPointsLines[*it].GetEndPointIndex()].GetY()) / 2);
      }

      // Sort result lines according to middlePoint Y's
      // TODO: If the wire pattern is asymmetric then use the pattern geometry to match the lines to the intersection points instead of just sort them by Y value (https://plustoolkit.github.io/legacytickets #435)
      std::vector<double>::iterator middlePointYsBeginIt = resultLineMiddlePointYs.begin();
      for (int i = 0; i < numberOfLines; ++i)
      {
        std::vector<double>::iterator middlePointYsMinIt = std::min_element(middlePointYsBeginIt, resultLineMiddlePointYs.end());
        int minIndex = (int)std::distance(middlePointYsBeginIt, middlePointYsMinIt);
        resultLines[i] = &maxPointsLines[lineIndices[minIndex]];
        (*middlePointYsMinIt) = DBL_MAX;
      }

      UpdateNWiresResults(resultLines);
    }
    else if (coplanarParallelWire)   // CIRS phantom
    {
      PlusFidLine resultLine1 = maxPointsLines[lineIndices[0]];
      PlusFidLine resultLine2 = maxPointsLines[lineIndices[1]];
      PlusFidLine resultLine3 = maxPointsLines[lineIndices[2]];

      bool test1 = (resultLine1.GetStartPointIndex() == resultLine2.GetStartPointIndex()) || (resultLine1.GetStartPointIndex() == resultLine2.GetEndPointIndex());
      bool test2 = (resultLine1.GetEndPointIndex() == resultLine2.GetStartPointIndex()) || (resultLine1.GetEndPointIndex() == resultLine2.GetEndPointIndex());
      bool test3 = (resultLine1.GetStartPointIndex() == resultLine3.GetStartPointIndex()) || (resultLine1.GetStartPointIndex() == resultLine3.GetEndPointIndex());
      //bool test4 = (resultLine1.GetEndPointIndex() == resultLine3.GetStartPointIndex()) || (resultLine1.GetEndPointIndex() == resultLine3.GetEndPointIndex());

      if (!test1 && !test2)  //if line 1 and 2 have no point in common
      {
        if (m_DotsVector[resultLine1.GetStartPointIndex()].GetX() > m_DotsVector[resultLine2.GetStartPointIndex()].GetX())  //Line 1 is on the left on the image
        {
          UpdateCirsResults(resultLine1, resultLine3, resultLine2);
        }
        else
        {
          UpdateCirsResults(resultLine2, resultLine3, resultLine1);
        }
      }
      else if (!test1 && !test3)  //if line 1 and 3 have no point in common
      {
        if (m_DotsVector[resultLine1.GetStartPointIndex()].GetX() > m_DotsVector[resultLine3.GetStartPointIndex()].GetX())  //Line 1 is on the left on the image
        {
          UpdateCirsResults(resultLine1, resultLine2, resultLine3);
        }
        else
        {
          UpdateCirsResults(resultLine3, resultLine2, resultLine1);
        }
      }
      else//if line 2 and 3 have no point in common
      {
        if (m_DotsVector[resultLine2.GetStartPointIndex()].GetX() > m_DotsVector[resultLine3.GetStartPointIndex()].GetX())  //Line 2 is on the left on the image
        {
          UpdateCirsResults(resultLine2, resultLine1, resultLine3);
        }
        else
        {
          UpdateCirsResults(resultLine3, resultLine1, resultLine2);
        }
      }
    }
  }
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::SortRightToLeft(PlusFidLine& line)
{
  //LOG_TRACE("FidLabeling::SortRightToLeft");

  std::vector<std::vector<PlusFidDot>::iterator> pointsIterator(line.GetNumberOfPoints());

  for (unsigned int i = 0; i < line.GetNumberOfPoints() ; i++)
  {
    pointsIterator[i] = m_DotsVector.begin() + line.GetPoint(i);
  }

  std::sort(pointsIterator.begin(), pointsIterator.end(), PlusFidDot::PositionLessThan);

  for (unsigned int i = 0; i < line.GetNumberOfPoints(); i++)
  {
    line.SetPoint(i, pointsIterator[i] - m_DotsVector.begin());
  }
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::SetMinThetaDeg(double value)
{
  m_MinThetaRad = vtkMath::RadiansFromDegrees(value);
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::SetMaxThetaDeg(double value)
{
  m_MaxThetaRad = vtkMath::RadiansFromDegrees(value);
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::SetMaxLineShiftMm(double aValue)
{
  m_MaxLineShiftMm = aValue;
}

//-----------------------------------------------------------------------------
double PlusFidLabeling::GetMaxLineShiftMm()
{
  return m_MaxLineShiftMm;
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::SetAngleToleranceDegrees(double angleToleranceDegrees)
{
  m_AngleToleranceRad = vtkMath::RadiansFromDegrees(angleToleranceDegrees);
}

//-----------------------------------------------------------------------------
void PlusFidLabeling::SetInclinedLineAngleDegrees(double inclinedLineAngleDegrees)
{
  m_InclinedLineAngleRad = vtkMath::RadiansFromDegrees(inclinedLineAngleDegrees);
}
