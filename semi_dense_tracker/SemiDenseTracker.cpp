#include <opencv2/opencv.hpp>
#include <opencv2/gpu/gpu.hpp>

#include <boost/shared_ptr.hpp>

#include "SemiDenseTracker.h"

#include <cuda.h>
#include <cuda_runtime.h>

#include "Helper.hpp"
#include "TestCudaFast.h"

__device__ short2 operator + (const short2 s2O1_, const short2 s2O2_);
__device__ short2 operator - (const short2 s2O1_, const short2 s2O2_);
__device__ short2 operator * (const float fO1_, const short2 s2O2_);

namespace btl{ namespace device{ namespace semidense{
	//for debug
	void cudaCalcMaxContrast(const cv::gpu::GpuMat& cvgmImage_, const unsigned char ucContrastThreshold_, cv::gpu::GpuMat* pcvgmContrast_);
	//for debug
	void cudaCalcMinDiameterContrast(const cv::gpu::GpuMat& cvgmImage_, cv::gpu::GpuMat* pcvgmContrast_);
	unsigned int cudaCalcSaliency(const cv::gpu::GpuMat& cvgmImage_, const unsigned short usHalfSizeRound_,
		const unsigned char ucContrastThreshold_, const float& fSaliencyThreshold_, 
		cv::gpu::GpuMat* pcvgmSaliency_, cv::gpu::GpuMat* pcvgmKeyPointLocations_);
	unsigned int cudaNonMaxSupression(const cv::gpu::GpuMat& cvgmKeyPointLocation_, const unsigned int uMaxSalientPoints_, 
		const cv::gpu::GpuMat& cvgmSaliency_, short2* ps2devLocations_, float* pfdevResponse_);
	//sort
	void thrustSort(short2* pnLoc_, float* pfResponse_, const unsigned int nCorners_);
	unsigned int cudaPredictAndMatch(const unsigned int uFinalSalientPoints_, const cv::gpu::GpuMat& cvgmImage_,const cv::gpu::GpuMat& cvgmSaliency_, cv::gpu::GpuMat& cvgmFinalKeyPointsLocations_,cv::gpu::GpuMat& cvgmFinalKeyPointsResponse_,cv::gpu::GpuMat& cvgmParticlesAge_,cv::gpu::GpuMat& cvgmParticlesVelocity_, cv::gpu::GpuMat& cvgmParticlesDescriptors_);
	void cudaExtractAllDescriptorFast(const cv::gpu::GpuMat& cvgmImage_, 
									  const short2* ps2KeyPointsLocations_, const float* pfKeyPointsResponse_, 
									  const unsigned int uTotalParticles_,  const unsigned int usHalfPatchSize_, 
									  cv::gpu::GpuMat* pcvgmParticleResponses_, cv::gpu::GpuMat* pcvgmParticleDescriptor_ );

	unsigned int cudaTrackFast(float fMatchThreshold_, const unsigned short usHalfSize_, const short sSearchRange_, 
								const cv::gpu::GpuMat& cvgmParticleDescriptorPrev_, const cv::gpu::GpuMat& cvgmParticleResponsesPrev_, 
								const cv::gpu::GpuMat& cvgmParticleDescriptorCurrTmp_, const cv::gpu::GpuMat& cvgmSaliencyCurr_, 
								cv::gpu::GpuMat* pcvgmMinMatchDistance_,
								cv::gpu::GpuMat* pcvgmMatchedLocationPrev_);
	void cudaCollectKeyPointsFast(unsigned int uTotalParticles_, unsigned int uMaxNewKeyPoints_, const float fRho_,
									const cv::gpu::GpuMat& cvgmSaliency_, 
									const cv::gpu::GpuMat& cvgmParticleDescriptorCurrTmp_,
									const cv::gpu::GpuMat& cvgmParticleVelocityPrev_,
									const cv::gpu::GpuMat& cvgmParticleAgePrev_,
									const cv::gpu::GpuMat& cvgmMinMatchDistance_,
									const cv::gpu::GpuMat& cvgmMatchedLocationPrev_, 
									cv::gpu::GpuMat* pcvgmNewlyAddedKeyPointLocation_, cv::gpu::GpuMat* pcvgmNewlyAddedKeyPointResponse_,
									cv::gpu::GpuMat* pcvgmMatchedKeyPointLocation_, cv::gpu::GpuMat* pcvgmMatchedKeyPointResponse_,
									cv::gpu::GpuMat* pcvgmParticleResponseCurr_, cv::gpu::GpuMat* pcvgmParticleDescriptorCurr_,
									cv::gpu::GpuMat* pcvgmParticleVelocityCurr_, cv::gpu::GpuMat* pcvgmParticleAgeCurr_);
}//semidense
}//device
}//btl

unsigned int testCudaTrack(const float fMatchThreshold_, const short sSearchRange_, 
	const cv::gpu::GpuMat& cvgmParticleDescriptorsPrev_, const cv::gpu::GpuMat& cvgmParticleResponsesPrev_,
	const cv::gpu::GpuMat& cvgmParticlesAgePrev_,const cv::gpu::GpuMat& cvgmParticlesVelocityPrev_, 
	const cv::gpu::GpuMat& cvgmBlurredCurr_,
	cv::gpu::GpuMat* pcvgmSaliency_,
	cv::gpu::GpuMat* pcvgmParticlesAgeCurr_,cv::gpu::GpuMat* pcvgmParticlesVelocityCurr_,cv::gpu::GpuMat* pcvgmParticleDescriptorsCurr_);


btl::image::semidense::CSemiDenseTracker::CSemiDenseTracker()
{
	//Gaussian filter
	_fSigma = 1.f; // page3: r=3/6 and sigma = 1.f/2.f respectively
	_uRadius = 3; // 
	_uGaussianKernelSize = 2*_uRadius + 1;
	//contrast threshold
	_ucContrastThresold = 5; // 255 * 0.02 = 5.1

	//saliency threshold
	_fSaliencyThreshold = 0.2f;
	//match threshold
	_fMatchThreshold = 5.f;

	//# of Max key points
	_uMaxKeyPointsBeforeNonMax[0] = 80000;
	_uMaxKeyPointsBeforeNonMax[1] = 10000;
	_uMaxKeyPointsBeforeNonMax[2] =  2500;
	_uMaxKeyPointsBeforeNonMax[3] =   650;

	_uMaxKeyPointsAfterNonMax[0] = 20000;
	_uMaxKeyPointsAfterNonMax[1] =  2500;
	_uMaxKeyPointsAfterNonMax[2] =   600;
	_uMaxKeyPointsAfterNonMax[3] =   150;

	_uTotalParticles[0] = 8000;
	_uTotalParticles[1] = 2000;
	_uTotalParticles[2] =  500;
	_uTotalParticles[3] =  100;
	_usHalfPatchSize = 6;
	_sSearchRange = 5;

	_nFrameIdx = 0;
	_uMatchedPoints[0] = 0;
	_uMatchedPoints[1] = 0;
	_uMatchedPoints[2] = 0;
	_uMatchedPoints[3] = 0;
}

bool btl::image::semidense::CSemiDenseTracker::initialize( boost::shared_ptr<cv::gpu::GpuMat> _acvgmShrPtrPyrBW[4] )
{
	_nFrameIdx = 0;
	for (int n = 3; n>-1; --n ){
		_cvgmSaliency[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_32FC1);
		_cvgmInitKeyPointLocation[n].create(1, _uMaxKeyPointsBeforeNonMax[n], CV_16SC2);
		_cvgmFinalKeyPointsLocationsAfterNonMax[n].create(1, _uMaxKeyPointsAfterNonMax[n], CV_16SC2);//short2 location;
		_cvgmFinalKeyPointsResponseAfterNonMax[n].create(1, _uMaxKeyPointsAfterNonMax[n], CV_32FC1);// float corner strength(response);  

		_cvgmMatchedKeyPointLocation[n].create(1, _uTotalParticles[n], CV_16SC2);
		_cvgmMatchedKeyPointResponse[n].create(1, _uTotalParticles[n], CV_32FC1);
		_cvgmNewlyAddedKeyPointLocation[n].create(1, _uMaxKeyPointsAfterNonMax[n], CV_16SC2);
		_cvgmNewlyAddedKeyPointResponse[n].create(1, _uMaxKeyPointsAfterNonMax[n], CV_32FC1);

		//init particles
		_cvgmParticleResponsePrev[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_32FC1);_cvgmParticleResponsePrev[n].setTo(0);
		_cvgmParticleVelocityPrev[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_16SC2);_cvgmParticleVelocityPrev[n].setTo(cv::Scalar::all(0));//float velocity; 
		_cvgmParticleAgePrev[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_8UC1);	  _cvgmParticleAgePrev[n].setTo(0);//uchar age;
		_cvgmParticleDescriptorPrev[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_32SC4);_cvgmParticleDescriptorPrev[n].setTo(cv::Scalar::all(0));

		_cvgmParticleResponseCurr[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_32FC1);_cvgmParticleResponseCurr[n].setTo(0);
		_cvgmParticleVelocityCurr[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_16SC2);_cvgmParticleVelocityCurr[n].setTo(cv::Scalar::all(0));//float velocity; 
		_cvgmParticleAgeCurr[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_8UC1);	  _cvgmParticleAgeCurr[n].setTo(0);//uchar age;
		_cvgmParticleDescriptorCurr[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_32SC4);_cvgmParticleDescriptorCurr[n].setTo(cv::Scalar::all(0));
		_cvgmParticleDescriptorCurrTmp[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_32SC4);_cvgmParticleDescriptorCurr[n].setTo(cv::Scalar::all(0));

		_cvgmMinMatchDistance[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_32FC1);
		_cvgmMatchedLocationPrev[n].create(_acvgmShrPtrPyrBW[n]->size(),CV_16SC2);

		//allocate filter
		if (_pBlurFilter.empty()){
			_pBlurFilter = cv::gpu::createGaussianFilter_GPU(CV_8UC1, cv::Size(_uGaussianKernelSize, _uGaussianKernelSize), _fSigma, _fSigma, cv::BORDER_REFLECT_101);
		}

		//processing the frame
		//apply gaussian filter
		_pBlurFilter->apply(*_acvgmShrPtrPyrBW[n], _cvgmBlurredPrev[n], cv::Rect(0, 0, _acvgmShrPtrPyrBW[n]->cols, _acvgmShrPtrPyrBW[n]->rows));
		//detect key points
		//1.compute the saliency score 
		unsigned int uTotalSalientPoints = btl::device::semidense::cudaCalcSaliency(_cvgmBlurredPrev[n], unsigned short(_usHalfPatchSize*1.5), _ucContrastThresold, _fSaliencyThreshold, 
																					&_cvgmSaliency[n],&_cvgmInitKeyPointLocation[n]); 
		if (uTotalSalientPoints< 50) return false;
		uTotalSalientPoints = std::min( uTotalSalientPoints, _uMaxKeyPointsBeforeNonMax[n] );
	
		//2.do a non-max suppression and initialize particles ( extract feature descriptors ) 
		unsigned int uFinalSalientPointsAfterNonMax = btl::device::semidense::cudaNonMaxSupression(_cvgmInitKeyPointLocation[n], uTotalSalientPoints, _cvgmSaliency[n], 
																								   _cvgmFinalKeyPointsLocationsAfterNonMax[n].ptr<short2>(), _cvgmFinalKeyPointsResponseAfterNonMax[n].ptr<float>() ); 
		uFinalSalientPointsAfterNonMax = std::min( uFinalSalientPointsAfterNonMax, _uMaxKeyPointsAfterNonMax[n] );
	
		//3.sort all salient points according to their strength 
		btl::device::semidense::thrustSort(_cvgmFinalKeyPointsLocationsAfterNonMax[n].ptr<short2>(),_cvgmFinalKeyPointsResponseAfterNonMax[n].ptr<float>(),uFinalSalientPointsAfterNonMax);
		_uTotalParticles[n] = std::min( _uTotalParticles[n], uFinalSalientPointsAfterNonMax );

		//4.collect all salient points and descriptors on them
		_cvgmParticleResponsePrev[n].setTo(0.f);
		btl::device::semidense::cudaExtractAllDescriptorFast(_cvgmBlurredPrev[n], 
															 _cvgmFinalKeyPointsLocationsAfterNonMax[n].ptr<short2>(),_cvgmFinalKeyPointsResponseAfterNonMax[n].ptr<float>(),
															 _uTotalParticles[n], _usHalfPatchSize,
															 &_cvgmParticleResponsePrev[n],&_cvgmParticleDescriptorPrev[n]);

		//test
		/*int nCounter = 0;
		bool bIsLegal = testCountResponseAndDescriptorFast(_cvgmParticleResponsePrev,_cvgmParticleDescriptorPrev,&nCounter);*/

		//store velocity
		_cvgmParticleVelocityPrev[n].download(_cvmKeyPointVelocity[_nFrameIdx][n]);
	}

	return true;
}

bool btl::image::semidense::CSemiDenseTracker::init( boost::shared_ptr<cv::gpu::GpuMat> _acvgmShrPtrPyrBW[4] )
{
	return initialize( _acvgmShrPtrPyrBW );
}

void btl::image::semidense::CSemiDenseTracker::trackAll(boost::shared_ptr<cv::gpu::GpuMat> _acvgmShrPtrPyrBW[4] ){
	track( _acvgmShrPtrPyrBW );
}

void btl::image::semidense::CSemiDenseTracker::track(boost::shared_ptr<cv::gpu::GpuMat> _acvgmShrPtrPyrBW[4] )
{
	btl::other::increase<int>(30,&_nFrameIdx);

	for (int n = 3; n>-1; --n ){
	//processing the frame
	//Gaussian smoothes the input image 
	_pBlurFilter->apply(*_acvgmShrPtrPyrBW[n], _cvgmBlurredCurr[n], cv::Rect(0, 0, _acvgmShrPtrPyrBW[n]->cols, _acvgmShrPtrPyrBW[n]->rows));
	//calc the saliency score for each pixel
	unsigned int uTotalSalientPoints = btl::device::semidense::cudaCalcSaliency(_cvgmBlurredCurr[n], unsigned short( _usHalfPatchSize*1.5) /*the fast corner radius*/, 
		                                                                        _ucContrastThresold, _fSaliencyThreshold, &_cvgmSaliency[n], &_cvgmInitKeyPointLocation[n]);
	uTotalSalientPoints = std::min( uTotalSalientPoints, _uMaxKeyPointsBeforeNonMax[n] );
	
	//do a non-max suppression and collect the candidate particles into a temporary vectors( extract feature descriptors ) 
	unsigned int uFinalSalientPoints = btl::device::semidense::cudaNonMaxSupression(_cvgmInitKeyPointLocation[n], uTotalSalientPoints, _cvgmSaliency[n], 
																					_cvgmFinalKeyPointsLocationsAfterNonMax[n].ptr<short2>(), _cvgmFinalKeyPointsResponseAfterNonMax[n].ptr<float>() );
	_uFinalSalientPoints[n] = uFinalSalientPoints = std::min( uFinalSalientPoints, _uMaxKeyPointsAfterNonMax[n] );
	_cvgmSaliency[n].setTo(0.f);//clear saliency scores
	//redeploy the saliency matrix
	btl::device::semidense::cudaExtractAllDescriptorFast(_cvgmBlurredCurr[n],
														_cvgmFinalKeyPointsLocationsAfterNonMax[n].ptr<short2>(),_cvgmFinalKeyPointsResponseAfterNonMax[n].ptr<float>(),
														uFinalSalientPoints, _usHalfPatchSize,
														&_cvgmSaliency[n], &_cvgmParticleDescriptorCurrTmp[n]);
	/*
	int nCounter = 0;
	bool bIsLegal = testCountResponseAndDescriptorFast(_cvgmSaliency,_cvgmParticleDescriptorCurrTmp,&nCounter);*/

	//track particles in previous frame by searching the candidates of current frame. 
	//Note that _cvgmSaliency is the input as well as output, tracked particles are marked as negative scores
	_cvgmParticleDescriptorCurr[n].setTo(cv::Scalar::all(0));_cvgmParticleAgeCurr[n].setTo(0);_cvgmParticleVelocityCurr[n].setTo(cv::Scalar::all(0));//clear all memory
	_uMatchedPoints[n] = btl::device::semidense::cudaTrackFast(_fMatchThreshold,_usHalfPatchSize, _sSearchRange,
															_cvgmParticleDescriptorPrev[n],  _cvgmParticleResponsePrev[n], 
															_cvgmParticleDescriptorCurrTmp[n], _cvgmSaliency[n],
															&_cvgmMinMatchDistance[n],
															&_cvgmMatchedLocationPrev[n]);
	/*
	nCounter = 0;
	bIsLegal = testCountMinDistAndMatchedLocationFast( _cvgmMinMatchDistance, _cvgmMatchedLocationPrev, &nCounter );
	cv::gpu::GpuMat _cvgmMinMatchDistanceTest(_cvgmMinMatchDistance),_cvgmMatchedLocationPrevTest(_cvgmMatchedLocationPrev);
	unsigned int uMatchedPointsTest = testCudaTrackFast(_fMatchThreshold,_usHalfPatchSize, _sSearchRange,
														_cvgmParticleDescriptorPrev,  _cvgmParticleResponsePrev, 
														_cvgmParticleDescriptorCurrTmp, _cvgmSaliency,
														&_cvgmMinMatchDistanceTest,
														&_cvgmMatchedLocationPrevTest);
	nCounter = 0;
	bIsLegal = testCountMinDistAndMatchedLocationFast( _cvgmMinMatchDistanceTest, _cvgmMatchedLocationPrevTest, &nCounter );
	float fD0 = testMatDiff(_cvgmMatchedLocationPrev, _cvgmMatchedLocationPrevTest);
	float fD1 = testMatDiff(_cvgmMinMatchDistance,_cvgmMinMatchDistanceTest);
	float fD2 = (float)uMatchedPointsTest - nCounter;
	*/
	//separate tracked particles and rest of candidates. Note that saliency scores are updated 
	//Note that _cvgmSaliency is the input as well as output, after the tracked particles are separated with rest of candidates, their negative saliency
	//scores are recovered into positive scores
	_cvgmMatchedKeyPointLocation   [n].setTo(cv::Scalar::all(0));//clear all memory
	_cvgmMatchedKeyPointResponse   [n].setTo(0.f);
	_cvgmNewlyAddedKeyPointLocation[n].setTo(cv::Scalar::all(0));//clear all memory
	_cvgmNewlyAddedKeyPointResponse[n].setTo(0.f);
	btl::device::semidense::cudaCollectKeyPointsFast(_uTotalParticles[n], _uMaxKeyPointsAfterNonMax[n], 0.75f,
													_cvgmSaliency[n], _cvgmParticleDescriptorCurrTmp[n],
													_cvgmParticleVelocityPrev[n],_cvgmParticleAgePrev[n],
													_cvgmMinMatchDistance[n],_cvgmMatchedLocationPrev[n],
													&_cvgmNewlyAddedKeyPointLocation[n], &_cvgmNewlyAddedKeyPointResponse[n], 
													&_cvgmMatchedKeyPointLocation[n], &_cvgmMatchedKeyPointResponse[n],
													&_cvgmParticleResponseCurr[n], &_cvgmParticleDescriptorCurr[n],
													&_cvgmParticleVelocityCurr[n],&_cvgmParticleAgeCurr[n]);
	/*
	nCounter = 0;
	bIsLegal = testCountResponseAndDescriptorFast(_cvgmParticleResponseCurr,_cvgmParticleDescriptorCurr,&nCounter);
	
    cv::gpu::GpuMat _cvgmMatchedKeyPointLocationTest(_cvgmMatchedKeyPointLocation);      _cvgmMatchedKeyPointLocationTest.setTo(cv::Scalar::all(0));//clear all memory
	cv::gpu::GpuMat _cvgmMatchedKeyPointResponseTest(_cvgmMatchedKeyPointResponse);      _cvgmMatchedKeyPointResponseTest.setTo(0.f);
	cv::gpu::GpuMat _cvgmNewlyAddedKeyPointLocationTest(_cvgmNewlyAddedKeyPointLocation);_cvgmNewlyAddedKeyPointLocationTest.setTo(cv::Scalar::all(0));//clear all memory
	cv::gpu::GpuMat _cvgmNewlyAddedKeyPointResponseTest(_cvgmNewlyAddedKeyPointResponse);_cvgmNewlyAddedKeyPointResponseTest.setTo(0.f);
	cv::gpu::GpuMat _cvgmParticleResponseCurrTest(_cvgmParticleResponseCurr);
	cv::gpu::GpuMat _cvgmParticleDescriptorCurrTest(_cvgmParticleDescriptorCurr);
	cv::gpu::GpuMat _cvgmParticleVelocityCurrTest(_cvgmParticleVelocityCurr);
	cv::gpu::GpuMat _cvgmParticleAgeCurrTest(_cvgmParticleAgeCurr);

	testCudaCollectKeyPointsFast(_uTotalParticles, _uMaxKeyPointsAfterNonMax, 0.75f,
								_cvgmSaliency, _cvgmParticleDescriptorCurrTmp,
								_cvgmParticleVelocityPrev,_cvgmParticleAgePrev,
								_cvgmMinMatchDistance,_cvgmMatchedLocationPrev,
								&_cvgmNewlyAddedKeyPointLocationTest, &_cvgmNewlyAddedKeyPointResponseTest, 
								&_cvgmMatchedKeyPointLocationTest, &_cvgmMatchedKeyPointResponseTest,
								&_cvgmParticleResponseCurrTest, &_cvgmParticleDescriptorCurrTest,
								&_cvgmParticleVelocityCurrTest,&_cvgmParticleAgeCurrTest);


	nCounter = 0;
	bIsLegal = testCountResponseAndDescriptorFast(_cvgmParticleResponseCurrTest,_cvgmParticleDescriptorCurrTest,&nCounter);

	float fD3 = testMatDiff(_cvgmNewlyAddedKeyPointLocationTest, _cvgmNewlyAddedKeyPointLocation);
	float fD4 = testMatDiff(_cvgmNewlyAddedKeyPointResponseTest, _cvgmNewlyAddedKeyPointResponse);
	float fD5 = testMatDiff(_cvgmMatchedKeyPointLocationTest, _cvgmMatchedKeyPointLocation);
	float fD6 = testMatDiff(_cvgmMatchedKeyPointResponseTest, _cvgmMatchedKeyPointResponse);
	float fD7 = testMatDiff(_cvgmParticleResponseCurrTest, _cvgmParticleResponseCurr);
	float fD8 = testMatDiff(_cvgmParticleDescriptorCurrTest, _cvgmParticleDescriptorCurr);
	float fD9 = testMatDiff(_cvgmParticleVelocityCurrTest, _cvgmParticleVelocityCurr);
	float fD10= testMatDiff(_cvgmParticleAgeCurrTest, _cvgmParticleAgeCurr);
	*/
		
	//h) assign the current frame to previous frame
	_cvgmBlurredCurr		 [n].copyTo(_cvgmBlurredPrev[n]);
	_cvgmParticleResponseCurr[n].copyTo(_cvgmParticleResponsePrev[n]);
	_cvgmParticleAgeCurr	 [n].copyTo(_cvgmParticleAgePrev[n]);
	_cvgmParticleVelocityCurr[n].copyTo(_cvgmParticleVelocityPrev[n]);
	_cvgmParticleDescriptorCurr[n].copyTo(_cvgmParticleDescriptorPrev[n]);

	
	//store velocity
	_cvgmParticleVelocityCurr[n].download(_cvmKeyPointVelocity[_nFrameIdx][n]);
	//render keypoints
	_cvgmMatchedKeyPointLocation[n].download(_cvmKeyPointLocation[n]);
	_cvgmParticleAgeCurr[n].download(_cvmKeyPointAge[n]);
	}
	
	return;	
}

void btl::image::semidense::CSemiDenseTracker::displayCandidates( cv::Mat& cvmColorFrame_ ){
	cv::Mat cvmKeyPoint;
	for( int n = 0; n< 4; n++ ){
		int t = 1<<n;
		_cvgmFinalKeyPointsLocationsAfterNonMax[n].download(cvmKeyPoint);
		for (unsigned int i=0;i<_uFinalSalientPoints[n]; i++){
			short2 ptCurr = cvmKeyPoint.ptr<short2>()[i];
			//cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),2,cv::Scalar(0,0,255.));
			if( n == 0)	cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),1,cv::Scalar(0,0,255.));
			if( n == 1)	cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),2,cv::Scalar(0,255.,0));
			if( n == 2)	cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),4,cv::Scalar(255.,0,0));
			if( n == 3)	cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),8,cv::Scalar(255.,0,255.));
		}
	}
	return;
}

void btl::image::semidense::CSemiDenseTracker::display(cv::Mat& cvmColorFrame_) const{
	cvmColorFrame_.setTo(cv::Scalar::all(255));
	float fAvgAge = 0.f; 
	for(int n = 0; n< 2; n++ ) {
		int t = 1<<n;
		for (unsigned int i=0;i<_uMatchedPoints[n]; i+=1){
			short2 ptCurr = _cvmKeyPointLocation[n].ptr<short2>()[i];
			uchar ucAge = _cvmKeyPointAge[n].ptr(ptCurr.y)[ptCurr.x];
			//if(ucAge < 2 ) continue;
			if( n == 0)	cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),1,cv::Scalar(0,0,255.));
			if( n == 1)	cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),1,cv::Scalar(0,255.,0));
			if( n == 2)	cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),1,cv::Scalar(255.,0,0));
			if( n == 3)	cv::circle(cvmColorFrame_,cv::Point(ptCurr.x*t,ptCurr.y*t),1,cv::Scalar(255.,0,255));
			short2 vi = _cvmKeyPointVelocity[_nFrameIdx][n].ptr<short2>(ptCurr.y)[ptCurr.x];
			int nFrameCurr = _nFrameIdx;
			fAvgAge += ucAge;
			int nFrame = 0;
			while (ucAge > 0 && nFrame < 1){//render trajectory 
				short2 ptPrev = ptCurr - vi;

				if( n == 0) cv::line(cvmColorFrame_, cv::Point(ptCurr.x*t,ptCurr.y*t), cv::Point(ptPrev.x*t,ptPrev.y*t), cv::Scalar(0,0,128));
				if( n == 1) cv::line(cvmColorFrame_, cv::Point(ptCurr.x*t,ptCurr.y*t), cv::Point(ptPrev.x*t,ptPrev.y*t), cv::Scalar(0,128,0));
				if( n == 2) cv::line(cvmColorFrame_, cv::Point(ptCurr.x*t,ptCurr.y*t), cv::Point(ptPrev.x*t,ptPrev.y*t), cv::Scalar(128,0,0));
				if( n == 3) cv::line(cvmColorFrame_, cv::Point(ptCurr.x*t,ptCurr.y*t), cv::Point(ptPrev.x*t,ptPrev.y*t), cv::Scalar(128,0,128));
				ptCurr = ptPrev;
				btl::other::decrease<int>(30,&nFrameCurr);
				vi = _cvmKeyPointVelocity[nFrameCurr][n].ptr<short2>(ptCurr.y)[ptCurr.x];
				--ucAge; ++nFrame;
			}
		}
		fAvgAge /= _uMatchedPoints[n];
	}
	return;
}












