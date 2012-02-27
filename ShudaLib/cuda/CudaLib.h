#ifndef BTL_CUDA_HEADER
#define BTL_CUDA_HEADER
#include "../OtherUtil.hpp"

namespace btl { namespace cuda_util
{

void cudaTestFloat3( const cv::gpu::GpuMat& cvgmIn_, cv::gpu::GpuMat* pcvgmOut_ );
void cudaDepth2Disparity( const cv::gpu::GpuMat& cvgmDepth_, cv::gpu::GpuMat* pcvgmDisparity_ );
void cudaDisparity2Depth( const cv::gpu::GpuMat& cvgmDisparity_, cv::gpu::GpuMat* pcvgmDepth_ );
void cudaUnprojectIR(const cv::gpu::GpuMat& cvgmDepth_ , 
	const float& dFxIR_, const float& dFyIR_, const float& uIR_, const float& vIR_, 
	cv::gpu::GpuMat* pcvgmIRWorld_ );
//template void cudaTransformIR2RGB<float>(const cv::gpu::GpuMat& cvgmIRWorld_, const T* aR_, const T* aRT_, cv::gpu::GpuMat* pcvgmRGBWorld_);
void cudaTransformIR2RGB(const cv::gpu::GpuMat& cvgmIRWorld_, const float* aR_, const float* aRT_, cv::gpu::GpuMat* pcvgmRGBWorld_);
void cudaProjectRGB(const cv::gpu::GpuMat& cvgmRGBWorld_, 
	const float& dFxRGB_, const float& dFyRGB_, const float& uRGB_, const float& vRGB_, 
	cv::gpu::GpuMat* pcvgmAligned_ );
void cudaBilateralFiltering(const cv::gpu::GpuMat& cvgmSrc_, const float& fSigmaSpace_, const float& fSigmaColor_, cv::gpu::GpuMat* pcvgmDst_ );
void cudaPyrDown (const cv::gpu::GpuMat& cvgmSrc_, const float& fSigmaColor_, cv::gpu::GpuMat* pcvgmDst_);
void cudaUnprojectRGB ( const cv::gpu::GpuMat& cvgmDepths_, 
	const float& fFxRGB_,const float& fFyRGB_,const float& uRGB_, const float& vRGB_, unsigned int uLevel_, 
	cv::gpu::GpuMat* pcvgmPts_, 
	btl::utility::tp_coordinate_convention eConvention_ = btl::utility::BTL_GL );
void cudaFastNormalEstimation(const cv::gpu::GpuMat& cvgmPts_, cv::gpu::GpuMat* pcvgmNls_ );
void cudaNormalHistogram(const cv::gpu::GpuMat& cvgmNls_, const unsigned short usSamplesAzimuth_, const unsigned short usSamplesElevationZ_, 
	const unsigned short usWidth_,const unsigned short usLevel_,  const float fNormalBinSize_, cv::gpu::GpuMat* pcvgmBinIdx_);
//set the rotation angle and axis for rendering disk GL convention; the input are normals in cv-convention
void cudaNormalCVSetRotationAxisGL(const cv::gpu::GpuMat& cvgmNlCVs_, cv::gpu::GpuMat* pcvgmAAs_ );
}//cuda_util
}//btl
#endif