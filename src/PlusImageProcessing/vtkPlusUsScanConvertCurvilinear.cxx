/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

/*
Based on the fast interpolation program (fast_int_uint32.c) by Joergen Arendt Jensen.
Version 1.30, downloaded on June 29, 2012 from
http://server.elektro.dtu.dk/personal/jaj/31545/exercises/exercise1/programs/
With permission from the author: "You are allowed to include the source code on
non-commercial terms in your toolbox and distribute it."
*/

#include "PlusConfigure.h"
#include "igsioCommon.h"

#include "vtkPlusUsScanConvertCurvilinear.h"

#include "vtkXMLDataElement.h"

#include "vtkMath.h"
#include "vtkImageData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkStreamingDemandDrivenPipeline.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

vtkStandardNewMacro( vtkPlusUsScanConvertCurvilinear );

//----------------------------------------------------------------------------
vtkPlusUsScanConvertCurvilinear::vtkPlusUsScanConvertCurvilinear()
{
  this->OutputImageStartDepthMm = -10;
  this->RadiusStartMm = 15.0;
  this->RadiusStopMm = 70.0;
  this->ThetaStartDeg = -30.0;
  this->ThetaStopDeg = 30.0;
  this->OutputIntensityScaling = 1.0;

  // Values that are used for computing the InterpolatedPointArray
  this->InterpInputImageExtent[0] = 0;
  this->InterpInputImageExtent[1] = -1;
  this->InterpInputImageExtent[2] = 0;
  this->InterpInputImageExtent[3] = -1;
  this->InterpInputImageExtent[4] = 0;
  this->InterpInputImageExtent[5] = -1;
  this->InterpRadiusStartMm = 0.0;
  this->InterpRadiusStopMm = 0.0;
  this->InterpThetaStartDeg = 0.0;
  this->InterpThetaStopDeg = 0.0;
  this->InterpOutputImageExtent[0] = 0;
  this->InterpOutputImageExtent[1] = -1;
  this->InterpOutputImageExtent[2] = 0;
  this->InterpOutputImageExtent[3] = -1;
  this->InterpOutputImageExtent[4] = 0;
  this->InterpOutputImageExtent[5] = -1;
  this->InterpOutputImageSpacing[0] = 0.0;
  this->InterpOutputImageSpacing[1] = 0.0;
  this->InterpOutputImageSpacing[2] = 0.0;
  this->InterpTransducerCenterPixel[0] = 0.0;
  this->InterpTransducerCenterPixel[1] = 0.0;
  this->InterpIntensityScaling = 0.0;
}

//----------------------------------------------------------------------------
vtkPlusUsScanConvertCurvilinear::~vtkPlusUsScanConvertCurvilinear()
{
}

//----------------------------------------------------------------------------
void vtkPlusUsScanConvertCurvilinear::ComputeInterpolatedPointArray(
  int* inputImageExtent, double radiusStartMm, double radiusStopMm, double thetaStartDeg, double thetaStopDeg,
  int* outputImageExtent, double* outputImageSpacing, double* transducerCenterPixel, double intensityScaling )
{
  // Computing the point array is a costly operation, so perform it only if a scan conversion parameter has been changed

  // Check if any scan conversion parameter has been changed
  bool modifiedScanConversionParams = false;
  for ( int i = 0; i < 6; i++ )
  {
    if ( this->InterpInputImageExtent[i] != inputImageExtent[i] )
    {
      modifiedScanConversionParams = true;
    }
    if ( this->OutputImageExtent[i] != outputImageExtent[i] )
    {
      modifiedScanConversionParams = true;
    }
  }
  for ( int i = 0; i < 3; i++ )
  {
    if ( this->InterpOutputImageSpacing[i] != outputImageSpacing[i] )
    {
      modifiedScanConversionParams = true;
    }
  }
  if ( ( this->InterpRadiusStartMm != radiusStartMm )
       || ( this->InterpRadiusStopMm != radiusStopMm )
       || ( this->InterpThetaStartDeg != thetaStartDeg )
       || ( this->InterpThetaStopDeg != thetaStopDeg )
       || ( this->InterpTransducerCenterPixel[0] != transducerCenterPixel[0] )
       || ( this->InterpTransducerCenterPixel[1] != transducerCenterPixel[1] )
       || ( this->InterpIntensityScaling != intensityScaling ) )
  {
    modifiedScanConversionParams = true;
  }

  if ( !modifiedScanConversionParams )
  {
    // scan conversion parameters haven't been modified since the InterpolatedPointArray was last computed
    // there is no need to recompute, just return
    return;
  }

  // remember the current scan conversion parameters that are used to compute the interpolated point array
  for ( int i = 0; i < 6; i++ )
  {
    this->InterpInputImageExtent[i] = inputImageExtent[i];
    this->OutputImageExtent[i] = outputImageExtent[i];
  }
  for ( int i = 0; i < 3; i++ )
  {
    this->InterpOutputImageSpacing[i] = outputImageSpacing[i];
  }
  this->InterpRadiusStartMm = radiusStartMm;
  this->InterpRadiusStopMm = radiusStopMm;
  this->InterpThetaStartDeg = thetaStartDeg;
  this->InterpThetaStopDeg = thetaStopDeg;
  this->InterpTransducerCenterPixel[0] = transducerCenterPixel[0];
  this->InterpTransducerCenterPixel[1] = transducerCenterPixel[1];
  this->InterpIntensityScaling = intensityScaling;

  // Compute the interpolated point array now

  this->InterpolatedPointArray.clear();

  int numberOfSamples = inputImageExtent[1] - inputImageExtent[0] + 1;
  int numberOfLines = inputImageExtent[3] - inputImageExtent[2] + 1;
  double radiusDeltaMm = ( radiusStopMm - radiusStartMm ) / numberOfSamples;
  double thetaStartRad = vtkMath::RadiansFromDegrees( thetaStartDeg );
  double thetaDeltaRad = 0;
  if ( numberOfLines > 1 )
  {
    thetaDeltaRad = vtkMath::RadiansFromDegrees( ( thetaStopDeg - thetaStartDeg ) / ( numberOfLines - 1 ) );
  }
  int outputImageSizePixelsX = outputImageExtent[1] - outputImageExtent[0] + 1;
  int outputImageSizePixelsY = outputImageExtent[3] - outputImageExtent[2] + 1;

  // Increments in image coordinates in mm
  double dx = outputImageSpacing[0];
  double dz = outputImageSpacing[1];

  // Starting depth in image coordinates in mm
  double z = radiusStartMm - this->InterpTransducerCenterPixel[1] * dz;
  for ( int i = 0; i < outputImageSizePixelsY; i++ )
  {
    double x = -( this->InterpTransducerCenterPixel[0] - 0.5 ) * dx; // image coordinate, in mm
    double z2 = z * z;

    for ( int j = 0; j < outputImageSizePixelsX; j++ )
    {
      // Find which samples to select from the envelope array
      double radius = sqrt( z2 + x * x ); // Radial distance
      double theta = atan2 ( x, z ); // Angle in degrees
      double samp = ( radius - radiusStartMm ) / radiusDeltaMm; // Sample number for interpolation
      double line = ( theta - thetaStartRad ) / thetaDeltaRad; // Line number for interpolation
      int index_samp = floor( samp ); // Index for the data sample number
      int index_line = floor( line ); // Index for the data line number

      if ( ( index_samp >= 0 ) && ( index_samp + 1 < numberOfSamples ) &&
           ( index_line >= 0 ) && ( index_line + 1 < numberOfLines ) )
      {
        // The sample is inside the input image, so it can be computed
        InterpolatedPoint ip;
        double samp_val = samp - index_samp; // Sub-sample fraction for interpolation
        double line_val = line - index_line; // Sub-line fraction for interpolation

        //  Calculate the coefficients
        ip.weightCoefficients[0] = ( 1 - samp_val ) * ( 1 - line_val ) * intensityScaling;
        ip.weightCoefficients[1] =    samp_val * ( 1 - line_val ) * intensityScaling;
        ip.weightCoefficients[2] = ( 1 - samp_val ) * line_val   * intensityScaling;
        ip.weightCoefficients[3] =    samp_val * line_val   * intensityScaling;

        ip.inputPixelIndex = index_samp + index_line * numberOfSamples;
        ip.outputPixelIndex = j + outputImageSizePixelsX * i;

        this->InterpolatedPointArray.push_back( ip );
      }

      x = x + dx;
    }
    z = z + dz;
  }

}

//----------------------------------------------------------------------------
// Computes any global image information associated with regions.
int vtkPlusUsScanConvertCurvilinear::RequestInformation ( vtkInformation* vtkNotUsed( request ), vtkInformationVector** inputVector, vtkInformationVector* outputVector )
{
  // get the info objects
  vtkInformation* outInfo = outputVector->GetInformationObject( 0 );
  vtkInformation* inInfo = inputVector[0]->GetInformationObject( 0 );

  outInfo->Set( vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), this->OutputImageExtent, 6 );

  // In Plus the convention is that the image coordinate system has always unit spacing and zero origin
  double spacing[3] = {1.0, 1.0, 1.0};
  outInfo->Set( vtkDataObject::SPACING(), spacing, 3 );
  double origin[3] = {0, 0, 0};
  outInfo->Set( vtkDataObject::ORIGIN(), origin, 3 );

  int inExtent[6] = {0};
  inInfo->Get( vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), inExtent );
  //inInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(),inExtent, 6);

  // Create the interpolation table. It is recomputed only if the scan conversion parameters change.
  ComputeInterpolatedPointArray( inExtent, this->RadiusStartMm, this->RadiusStopMm, this->ThetaStartDeg, this->ThetaStopDeg,
                                 this->OutputImageExtent, this->OutputImageSpacing, this->TransducerCenterPixel, this->OutputIntensityScaling );

  return 1;
}

//----------------------------------------------------------------------------
int vtkPlusUsScanConvertCurvilinear::RequestUpdateExtent ( vtkInformation* vtkNotUsed( request ),  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed( outputVector ) )
{
  // Use the whole extent as the update extent (by default it would use the output extent, which would not be correct)
  vtkInformation* inInfo = inputVector[0]->GetInformationObject( 0 );
  int extent[6] = {0, -1, 0, -1, 0, -1};
  inInfo->Get( vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), extent );
  inInfo->Set( vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), extent, 6 );
  return 1;
}

//----------------------------------------------------------------------------
void vtkPlusUsScanConvertCurvilinear::AllocateOutputData( vtkImageData* output, vtkInformation* outInfo, int* uExtent )
{
  // The multithreaded algorithm sets only the non-zero voxels.
  // We need to initialize the rest of the voxels to zero.

  // Make sure the output is allocated
  Superclass::AllocateOutputData( output, outInfo, uExtent );

  // Initialize voxels to zero now.

  unsigned char* outPtrZ = static_cast<unsigned char*>( output->GetScalarPointerForExtent( uExtent ) );

  // Get increments to march through data
  vtkIdType outIncX, outIncY, outIncZ;
  output->GetIncrements( outIncX, outIncY, outIncZ );
  int typeSize = output->GetScalarSize();
  outIncX *= typeSize;
  outIncY *= typeSize;
  outIncZ *= typeSize;

  // Find the region to loop over
  int rowLength = ( uExtent[1] - uExtent[0] + 1 ) * output->GetNumberOfScalarComponents();
  rowLength *= typeSize;
  int maxY = uExtent[3] - uExtent[2];
  int maxZ = uExtent[5] - uExtent[4];

  // Loop through input pixels
  for ( int idxZ = 0; idxZ <= maxZ; idxZ++ )
  {
    unsigned char* outPtrY = outPtrZ;
    for ( int idxY = 0; idxY <= maxY; idxY++ )
    {
      memset( outPtrY, 0, rowLength );
      outPtrY += outIncY;
    }
    outPtrZ += outIncZ;
  }
}

//----------------------------------------------------------------------------
// The templated execute function handles all the data types.
// T: originally developed for unsigned int
template <class T>
void vtkPlusUsScanConvertExecute( vtkPlusUsScanConvertCurvilinear* self,
                                  vtkImageData* inData, T* inPtr,
                                  vtkImageData* outData, T* outPtr,
                                  int interpolationTableExt[6], int id )
{
  T* envelope_data = inPtr; // The envelope detected and log-compressed data
  int numberOfSamples = inData->GetExtent()[1] - inData->GetExtent()[0] + 1; // Number of samples in one envelope line

  T* image = outPtr; // The resulting image

  std::vector<vtkPlusUsScanConvertCurvilinear::InterpolatedPoint>::const_iterator firstPoint = self->GetInterpolatedPointArray().begin() + interpolationTableExt[0];
  std::vector<vtkPlusUsScanConvertCurvilinear::InterpolatedPoint>::const_iterator afterLastPoint = self->GetInterpolatedPointArray().begin() + interpolationTableExt[1] + 1;
  for ( std::vector<vtkPlusUsScanConvertCurvilinear::InterpolatedPoint>::const_iterator it = firstPoint; it != afterLastPoint; ++it )
  {
    T* env_pointer = ( T* ) & ( envelope_data[it->inputPixelIndex] ); // Pointer to the envelope data
    image[it->outputPixelIndex] =
      it->weightCoefficients[0] * env_pointer[0] // (+0, +0)
      + it->weightCoefficients[1] * env_pointer[1] // (+1, +0)
      + it->weightCoefficients[2] * env_pointer[numberOfSamples] // (+0, +1)
      + it->weightCoefficients[3] * env_pointer[numberOfSamples + 1] // (+1, +1)
      + 0.5; // for rounding
  }
}

//----------------------------------------------------------------------------
void vtkPlusUsScanConvertCurvilinear::ThreadedRequestData(
  vtkInformation* vtkNotUsed( request ),
  vtkInformationVector** vtkNotUsed( inputVector ),
  vtkInformationVector* vtkNotUsed( outputVector ),
  vtkImageData** *inData,
  vtkImageData** outData,
  int outExt[6], int id )
{

  void* inPtr = inData[0][0]->GetScalarPointer();
  void* outPtr = outData[0]->GetScalarPointer();

  // this filter expects that input is the same type as output.
  if ( inData[0][0]->GetScalarType() != outData[0]->GetScalarType() )
  {
    vtkErrorMacro( "Execute: input ScalarType, "
                   << inData[0][0]->GetScalarType()
                   << ", must match out ScalarType "
                   << outData[0]->GetScalarType() );
    return;
  }

  switch ( inData[0][0]->GetScalarType() )
  {
    vtkTemplateMacro(
      vtkPlusUsScanConvertExecute( this, inData[0][0],
                                   static_cast<VTK_TT*>( inPtr ), outData[0],
                                   static_cast<VTK_TT*>( outPtr ),
                                   outExt, id ) );
  default:
    vtkErrorMacro( << "Execute: Unknown ScalarType" );
    return;
  }
}

//----------------------------------------------------------------------------
void vtkPlusUsScanConvertCurvilinear::PrintSelf( ostream& os, vtkIndent indent )
{
  this->Superclass::PrintSelf( os, indent );

  os << indent << "TransducerCenterPixel: " << this->TransducerCenterPixel[0] << "; " << this->TransducerCenterPixel[1] << "\n";
  os << indent << "RadiusStartMm: " << this->RadiusStartMm << "\n";
  os << indent << "RadiusStopMm: " << this->RadiusStopMm << "\n";
  os << indent << "ThetaStartDeg: " << this->ThetaStartDeg << "\n";
  os << indent << "ThetaStopDeg: " << this->ThetaStopDeg << "\n";
  os << indent << "OutputIntensityScaling: " << this->OutputIntensityScaling << "\n";
  os << indent << "InterpolatedPointArraySize: " << this->InterpolatedPointArray.size() << "\n";

}

//----------------------------------------------------------------------------
// Splits data into num pieces for processing by each thread.
// Usually the output extent is split into pieces, but in our case
// we need to split the interpolation table.
// This method returns the number of pieces resulting from a successful split.
// This can be from 1 to "total".
// If 1 is returned, the extent cannot be split.
int vtkPlusUsScanConvertCurvilinear::SplitExtent( int splitExt[6], int startExt[6], int num, int total )
{
  // startExt is not used, because we split the interpolation table

  // Starting extent
  int min = 0;
  int max = this->InterpolatedPointArray.size() - 1;

  splitExt[0] = min;
  splitExt[1] = max;
  splitExt[2] = 0;
  splitExt[3] = 0;
  splitExt[4] = 0;
  splitExt[5] = 0;

  if ( min >= max )
  {
    // Cannot split interpolation table, as it's empty or has only one element
    return 1;
  }

  // determine the actual number of pieces that will be generated
  int range = max - min + 1;
  int valuesPerThread = static_cast<int>( ceil( range / static_cast<double>( total ) ) );
  int maxThreadIdUsed = static_cast<int>( ceil( range / static_cast<double>( valuesPerThread ) ) ) - 1;
  if ( num < maxThreadIdUsed )
  {
    splitExt[0] = splitExt[0] + num * valuesPerThread;
    splitExt[1] = splitExt[0] + valuesPerThread - 1;
  }
  if ( num == maxThreadIdUsed )
  {
    splitExt[0] = splitExt[0] + num * valuesPerThread;
  }

  vtkDebugMacro( "  Split Piece: ( " << splitExt[0] << ", " << splitExt[1] << ", "
                 << splitExt[2] << ", " << splitExt[3] << ", "
                 << splitExt[4] << ", " << splitExt[5] << ")" );

  return maxThreadIdUsed + 1;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusUsScanConvertCurvilinear::ReadConfiguration( vtkXMLDataElement* scanConversionElement )
{
  LOG_TRACE( "vtkPlusUsScanConvertCurvilinear::ReadConfiguration" );
  if ( this->Superclass::ReadConfiguration( scanConversionElement ) != PLUS_SUCCESS )
  {
    return PLUS_FAIL;
  }

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL( double, RadiusStartMm, scanConversionElement );
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL( double, RadiusStopMm, scanConversionElement );

  // this->TransducerCenterPixel values will be used in this file, so set them to meaningful default values if the user has not specified them
  if ( !this->TransducerCenterPixelSpecified )
  {
    this->TransducerCenterPixel[0] = double( this->OutputImageExtent[1] - this->OutputImageExtent[0] + 1 ) / 2.0;
    this->TransducerCenterPixel[1] = this->RadiusStartMm / this->OutputImageSpacing[1];
  }

  // Fill the TransducerCenterPixel from the deprecated OutputImageStartDepthMm element
  double outputImageStartDepthMm = 0;
  if ( scanConversionElement->GetScalarAttribute( "OutputImageStartDepthMm", outputImageStartDepthMm ) )
  {
    if ( this->TransducerCenterPixelSpecified )
    {
      LOG_WARNING( "Transducer center position in the image is specified by the TransducerCenterPixel attribute, therefore the OutputImageStartDepthMm will be ignored. Please remove the OutputImageStartDepthMm attribute from the configuration." );
    }
    else
    {
      this->TransducerCenterPixel[0] = double( this->OutputImageExtent[1] - this->OutputImageExtent[0] + 1 ) / 2.0; // image center
      this->TransducerCenterPixel[1] = ( this->RadiusStartMm - outputImageStartDepthMm ) / this->OutputImageSpacing[1]; // row index of the centerpoint of the curvature circle
      this->TransducerCenterPixelSpecified = true;
      LOG_WARNING( "OutputImageStartDepthMm is deprecated. Use the following equivalent attribute instead: TransducerCenterPixel=\""
                   << this->TransducerCenterPixel[0] << " " << this->TransducerCenterPixel[1] << "\"" );
    }
  }

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL( double, ThetaStartDeg, scanConversionElement );
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL( double, ThetaStopDeg, scanConversionElement );

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusUsScanConvertCurvilinear::WriteConfiguration( vtkXMLDataElement* scanConversionElement )
{
  LOG_TRACE( "vtkPlusUsScanConvertCurvilinear::WriteConfiguration" );
  if ( this->Superclass::WriteConfiguration( scanConversionElement ) != PLUS_SUCCESS )
  {
    return PLUS_FAIL;
  }

  scanConversionElement->SetDoubleAttribute( "RadiusStartMm", this->RadiusStartMm );
  scanConversionElement->SetDoubleAttribute( "RadiusStopMm", this->RadiusStopMm );

  scanConversionElement->SetDoubleAttribute( "ThetaStartDeg", this->ThetaStartDeg );
  scanConversionElement->SetDoubleAttribute( "ThetaStopDeg", this->ThetaStopDeg );

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusUsScanConvertCurvilinear::GetScanLineEndPoints( int scanLineIndex, double scanlineStartPoint_OutputImage[4], double scanlineEndPoint_OutputImage[4] )
{
  // (imagecenter,0) is in the pixel centerpoint
  double fanOriginPosition_OutputImage[2] = { this->TransducerCenterPixel[0], this->TransducerCenterPixel[1] - this->RadiusStartMm / this->OutputImageSpacing[1]};

  int numberOfLines = this->InputImageExtent[3] - this->InputImageExtent[2] + 1;

  double thetaDeltaDeg = 0; // angle between two scanlines
  if ( numberOfLines > 1 )
  {
    thetaDeltaDeg = ( ( this->ThetaStopDeg - this->ThetaStartDeg ) / ( numberOfLines - 1 ) );
  }
  double thetaRad = vtkMath::RadiansFromDegrees( this->ThetaStartDeg + scanLineIndex * thetaDeltaDeg );

  scanlineStartPoint_OutputImage[0] = fanOriginPosition_OutputImage[0] + this->RadiusStartMm * sin( thetaRad ) / this->OutputImageSpacing[0];
  scanlineStartPoint_OutputImage[1] = fanOriginPosition_OutputImage[1] + this->RadiusStartMm * cos( thetaRad ) / this->OutputImageSpacing[1];
  scanlineStartPoint_OutputImage[2] = 0;
  scanlineStartPoint_OutputImage[3] = 1;

  scanlineEndPoint_OutputImage[0] = fanOriginPosition_OutputImage[0] + this->RadiusStopMm * sin( thetaRad ) / this->OutputImageSpacing[0];
  scanlineEndPoint_OutputImage[1] = fanOriginPosition_OutputImage[1] + this->RadiusStopMm * cos( thetaRad ) / this->OutputImageSpacing[1];
  scanlineEndPoint_OutputImage[2] = 0;
  scanlineEndPoint_OutputImage[3] = 1;

  return PLUS_FAIL;
}

//-----------------------------------------------------------------------------
double vtkPlusUsScanConvertCurvilinear::GetDistanceBetweenScanlineSamplePointsMm()
{
  int scanLineLengthPixels = this->InputImageExtent[1] - this->InputImageExtent[0] + 1;
  if ( scanLineLengthPixels < 1 )
  {
    LOG_ERROR( "Cannot determine DistanceBetweenScanlineSamplePointsMm because scanLineLengthPixels=" << scanLineLengthPixels << " is invalid" );
    return 0.0;
  }
  double distanceBetweenScanlineSamplePointsMm = ( this->RadiusStopMm - this->RadiusStartMm ) / double( scanLineLengthPixels );
  return distanceBetweenScanlineSamplePointsMm;
}

//-----------------------------------------------------------------------------
vtkImageData* vtkPlusUsScanConvertCurvilinear::GetOutput()
{
  return vtkImageAlgorithm::GetOutput();
}
