


float testMatDiff(const cv::gpu::GpuMat& cvgm1_,const cv::gpu::GpuMat& cvgm2_ );
void testCudaCollectParticlesAndOrbDescriptors(const cv::gpu::GpuMat& cvgmFinalKeyPointsLocationsAfterNonMax_, const cv::gpu::GpuMat& cvmFinalKeyPointsResponseAfterNonMax_, const cv::gpu::GpuMat& cvgmImage_,
													const unsigned int uTotalParticles_, const unsigned short _usHalfPatchSize,
													const cv::gpu::GpuMat& cvgmPattern_,
													cv::gpu::GpuMat* pcvgmParticleResponses_, cv::gpu::GpuMat* pcvgmParticleAngle_, cv::gpu::GpuMat* pcvgmParticleDescriptor_);
unsigned int testCudaTrackOrb(const unsigned short usMatchThreshold_, const unsigned short usHalfSize_, const short sSearchRange_,
							const short* psPatternX_, const short* psPatternY_, /*const unsigned int uMaxMatchedKeyPoints_,*/
							const cv::gpu::GpuMat& cvgmParticleOrbDescriptorsPrev_, const cv::gpu::GpuMat& cvgmParticleResponsesPrev_, 
							/*const cv::gpu::GpuMat& cvgmParticlesAgePrev_,const cv::gpu::GpuMat& cvgmParticlesVelocityPrev_, 
							const cv::gpu::GpuMat& cvgmImage_,*/ const cv::gpu::GpuMat& cvgmParticleDescriptorCurrTmp_,
							const cv::gpu::GpuMat& cvgmSaliencyCurr_,
							/*cv::gpu::GpuMat* pcvgmMutex_,*/
							cv::gpu::GpuMat* pcvgmMinMatchDistance_,
							cv::gpu::GpuMat* pcvgmMatchedLocationPrev_
							/*cv::gpu::GpuMat* pcvgmParticleResponsesCurr_,
							cv::gpu::GpuMat* pcvgmParticlesAgeCurr_,cv::gpu::GpuMat* pcvgmParticlesVelocityCurr_,cv::gpu::GpuMat* pcvgmParticleOrbDescriptorsCurr_*/);
void testCudaCollectNewlyAddedKeyPoints(unsigned int uTotalParticles_, unsigned int uMaxNewKeyPoints_, const float fRho_,
										const cv::gpu::GpuMat& cvgmSaliency_,/*const cv::gpu::GpuMat& cvgmParticleResponseCurrTmp_,*/
										const cv::gpu::GpuMat& cvgmParticleDescriptorCurrTmp_,
										const cv::gpu::GpuMat& cvgmParticleVelocityPrev_,
										const cv::gpu::GpuMat& cvgmParticleAgePrev_,
										const cv::gpu::GpuMat& cvgmMinMatchDistance_,
										const cv::gpu::GpuMat& cvgmMatchedLocationPrev_,
										cv::gpu::GpuMat* pcvgmNewlyAddedKeyPointLocation_, cv::gpu::GpuMat* pcvgmNewlyAddedKeyPointResponse_,
										cv::gpu::GpuMat* pcvgmMatchedKeyPointLocation_, cv::gpu::GpuMat* pcvgmMatchedKeyPointResponse_,
										cv::gpu::GpuMat* pcvgmParticleResponseCurr_, cv::gpu::GpuMat* pcvgmParticleDescriptorCurr_,
										cv::gpu::GpuMat* pcvgmParticleVelocityCurr_, cv::gpu::GpuMat* pcvgmParticleAgeCurr_);
bool testCountMinDistAndMatchedLocation(const cv::gpu::GpuMat cvgmMinMatchDistance_, const cv::gpu::GpuMat& cvgmMatchedLocationPrev_, int* pnCounter_);
bool testCountResponseAndDescriptor(const cv::gpu::GpuMat cvgmParticleResponse_, const cv::gpu::GpuMat& cvgmParticleDescriptor_, int* pnCounter_);
