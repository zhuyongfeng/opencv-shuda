
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif

#include <opencv2/gpu/gpu.hpp>
#include <opencv2/gpu/devmem2d.hpp>
#include "cv/common.hpp" //copied from opencv
#include "../OtherUtil.hpp"
#include <math_constants.h>
#include "pcl/limits.hpp"
#include "pcl/device.hpp"
#include "pcl/vector_math.hpp"
#include "pcl/block.hpp"
#include <vector>

namespace btl{ namespace device {
typedef double float_type;
using namespace pcl::device;

struct SDeviceICPRegistration
{
    enum {
		CTA_SIZE_X = 32,
		CTA_SIZE_Y = 8,
		CTA_SIZE = CTA_SIZE_X * CTA_SIZE_Y
    };

    struct SDevPlus {
		__forceinline__ __device__ float operator () (const float_type &lhs, const volatile float_type& rhs) const {
			return (lhs + rhs);
		}
    };

    pcl::device::Intr _sCamIntr;

    pcl::device::Mat33  _mRwCurTrans;
    float3 _vTwCur;

    cv::gpu::DevMem2D_<float3> _cvgmVMapLocalCur;
    cv::gpu::DevMem2D_<float3> _cvgmNMapLocalCur;

    pcl::device::Mat33  _mRwPrev;
    float3 _vTwPrev;

    cv::gpu::DevMem2D_<float3> _cvgmVMapWorldPrev;
	cv::gpu::DevMem2D_<float3> _cvgmNMapWorldPrev;

    float _fDistThres;
    float _fSinAngleThres;

    int _nCols;
    int _nRows;

    mutable cv::gpu::DevMem2D_<float_type> _cvgmBuf;

    __device__ __forceinline__ bool searchForCorrespondence(int nX_, int nY_, float3* pf3NlPrev_, float3* pf3PtPrev_, float3* pf3PtCur_) const{
		//retrieve normal
		const float3& f3NlLocalCur = _cvgmNMapLocalCur.ptr(nY_)[nX_];	if (isnan (f3NlLocalCur.x)) return false;
		//transform the current vetex to reference camera coodinate system
		float3 f3PtLocalCur = _cvgmVMapLocalCur.ptr(nY_)[nX_]; if (isnan (f3PtLocalCur.x)) return false; //retrieve vertex from current frame
		float3 f3PtWorldCur = _mRwCurTrans * (f3PtLocalCur - _vTwCur); //transform LocalCur into World
		float3 f3PtCur_LocalPrev = _mRwPrev * f3PtWorldCur + _vTwPrev; //transform WorldCur into Prev Local
		//projection onto reference image
		int2 n2Ref;        
		n2Ref.x = __float2int_rn (f3PtCur_LocalPrev.x * _sCamIntr.fx / f3PtCur_LocalPrev.z + _sCamIntr.cx);  
		n2Ref.y = __float2int_rn (f3PtCur_LocalPrev.y * _sCamIntr.fy / f3PtCur_LocalPrev.z + _sCamIntr.cy);  
		//if projected out of the frame, return false
		if (n2Ref.x < 0 || n2Ref.y < 0 || n2Ref.x >= _nCols || n2Ref.y >= _nRows || f3PtCur_LocalPrev.z < 0) return false;
		//retrieve corresponding reference normal
		const float3& f3NlWorldRef = _cvgmNMapWorldPrev.ptr(n2Ref.y)[n2Ref.x];	if (isnan (f3NlWorldRef.x))  return false;
		//retrieve corresponding reference vertex
		const float3& f3PtWorldRef = _cvgmVMapWorldPrev.ptr (n2Ref.y)[n2Ref.x]; if (isnan (f3PtWorldRef.x))  return false;
		//check distance
		float fDist = norm (f3PtWorldRef - f3PtWorldCur); 
		if (fDist > _fDistThres)  return (false);
		//transform current normal to world
	    float3 f3NlWorldCur = _mRwCurTrans * f3NlLocalCur; 
		//check normal angle
		float fSin = norm ( cross(f3NlWorldCur, f3NlWorldRef) ); 
		if (fSin >= _fSinAngleThres) return (false);
		//return values
		*pf3NlPrev_ = f3NlWorldRef;
		*pf3PtPrev_ = f3PtWorldRef;
		*pf3PtCur_ = f3PtWorldCur;
		return (true);
    }//searchForCorrespondence()
	    //32*8 threads and cols/32 * rows/8 blocks
	    __device__ __forceinline__ void operator () () const {
		int nX = threadIdx.x + blockIdx.x * CTA_SIZE_X;
		int nY = threadIdx.y + blockIdx.y * CTA_SIZE_Y;

		float3 f3NlPrev, f3PtPrev, f3PtCurr;
		bool bCorrespondenceFound = false;
		if (nX < _nCols || nY < _nRows)  bCorrespondenceFound = searchForCorrespondence (nX, nY, &f3NlPrev, &f3PtPrev, &f3PtCurr);

		float row[7];

		if (bCorrespondenceFound){
			*(float3*)&row[0] = cross (f3PtCurr, f3NlPrev);
			*(float3*)&row[3] = f3NlPrev;
			row[6] = dot (f3NlPrev, f3PtPrev - f3PtCurr);
		}//if correspondence found
		else{
			row[0] = row[1] = row[2] = row[3] = row[4] = row[5] = row[6] = 0.f;
		}//if not found

		__shared__ float_type smem[CTA_SIZE]; // CTA_SIZE is 32*8 == the number of threads in the brock
	    int nThrID = Block::flattenedThreadId ();

		int nShift = 0;
		for (int i = 0; i < 6; ++i){ //_nRows
			#pragma unroll
			for (int j = i; j < 7; ++j){ // _nCols + b
				__syncthreads ();
				smem[nThrID] = row[i] * row[j]; //fill all the shared memory
				__syncthreads ();

				Block::reduce<CTA_SIZE>(smem, SDevPlus ()); //reduce to thread 0;
				if (nThrID == 0) _cvgmBuf.ptr(nShift++)[blockIdx.x + gridDim.x * blockIdx.y] = smem[0]; //nShift < 27 = 21 + 6, upper triangle of 6x6
			}//for
		}//for
		return;
    }//operator()
};//SDeviceICPRegistration

struct STranformReduction
{
    enum{
		CTA_SIZE = 512,
		STRIDE = CTA_SIZE, // 512 threads per block

		B = 6, COLS = 6, ROWS = 6, DIAG = 6, 
		UPPER_DIAG_MAT = (COLS * ROWS - DIAG) / 2 + DIAG, 
		TOTAL = UPPER_DIAG_MAT + B,
		GRID_X = TOTAL
    };

    cv::gpu::DevMem2D_<float_type> _cvgmBuf;
    int length; //# of blocks
    mutable float_type* pOutput;
	// 512 threeads and 27 blocks
    __device__ __forceinline__ void  operator () () const
    {
		const float_type *beg = _cvgmBuf.ptr (blockIdx.x); // 27 * # of blocks in previous kernel launch
		const float_type *end = beg + length;

		int tid = threadIdx.x;

		float_type sum = 0.f;
		for (const float_type *t = beg + tid; t < end; t += STRIDE) 
			sum += *t;

		__shared__ float_type smem[CTA_SIZE];

		smem[tid] = sum;
		__syncthreads ();

		Block::reduce<CTA_SIZE>(smem, SDeviceICPRegistration::SDevPlus ());

		if (tid == 0) pOutput[blockIdx.x] = smem[0];
    }//operator ()
};//STranformReduction

__global__ void kernelRegistration ( SDeviceICPRegistration sICP ) {
    sICP ();
}
__global__ void kernelTransformEstimator ( STranformReduction sTR ) {
	sTR ();
}

void registrationICP( const Intr& sCamIntr_, float fDistThres_, float fSinAngleThres_,
					  const pcl::device::Mat33& RwCurTrans_, const float3& TwCur_, 
					  const pcl::device::Mat33& RwPrev_, const float3& TwPrev_, 
					  const cv::gpu::GpuMat& cvgmVMapWorldPrev_, const cv::gpu::GpuMat& cvgmNMapWorldPrev_, 
					  const cv::gpu::GpuMat& cvgmVMapLocalCur_,  const cv::gpu::GpuMat& cvgmNMapLocalCur_,
					  cv::gpu::GpuMat* pcvgmSumBuf_){

	SDeviceICPRegistration sICP;

	sICP._sCamIntr = sCamIntr_;

	sICP._mRwCurTrans = RwCurTrans_;
    sICP._vTwCur = TwCur_;

    sICP._cvgmVMapLocalCur = cvgmVMapLocalCur_;
    sICP._cvgmNMapLocalCur = cvgmNMapLocalCur_;

    sICP._mRwPrev = RwPrev_;
    sICP._vTwPrev = TwPrev_;

    sICP._cvgmVMapWorldPrev = cvgmVMapWorldPrev_;
	sICP._cvgmNMapWorldPrev = cvgmNMapWorldPrev_;

    sICP._fDistThres = fDistThres_;
    sICP._fSinAngleThres = fSinAngleThres_;

    sICP._nCols = cvgmVMapLocalCur_.cols;
    sICP._nRows = cvgmVMapLocalCur_.rows;


	dim3 block (SDeviceICPRegistration::CTA_SIZE_X, SDeviceICPRegistration::CTA_SIZE_Y);
    dim3 grid (1, 1, 1);
	grid.x = cv::gpu::divUp (cvgmVMapWorldPrev_.cols, block.x);
	grid.y = cv::gpu::divUp (cvgmVMapWorldPrev_.rows, block.y);
		
	cv::gpu::GpuMat cvgmBuf(STranformReduction::TOTAL, grid.x * grid.y, CV_64FC1);
	//the # of rows is STranformReduction::TOTAL, 27, which is calculated in this way:
	// | 1  2  3  4  5  6  7 |
	// |    8  9 10 11 12 13 |
	// |      14 15 16 17 18 |
	// |         19 20 21 22 |
	// |            23 24 25 |
	// |               26 27 |
	//the # of cols is equal to the # of blocks.

	sICP._cvgmBuf = cvgmBuf;
	
	kernelRegistration<<<grid, block>>>(sICP);
	cudaSafeCall ( cudaGetLastError () );
	cudaSafeCall ( cudaDeviceSynchronize() );

	STranformReduction sTR;
	sTR._cvgmBuf = cvgmBuf;
	sTR.length = grid.x * grid.y; // # of the blocks
    
	pcvgmSumBuf_->create ( 1, STranformReduction::TOTAL, CV_64FC1 );
	sTR.pOutput = (float_type*) pcvgmSumBuf_->data;

	kernelTransformEstimator<<<STranformReduction::TOTAL, STranformReduction::CTA_SIZE>>>(sTR);
	cudaSafeCall ( cudaGetLastError () );
	cudaSafeCall ( cudaDeviceSynchronize () );
}//registration()


struct SDeviceICPRegistrationImage
{
    enum {
		CTA_SIZE_X = 32,
		CTA_SIZE_Y = 8,
		CTA_SIZE = CTA_SIZE_X * CTA_SIZE_Y
    };

    struct SDevPlus {
		__forceinline__ __device__ float operator () (const float_type &lhs, const volatile float_type& rhs) const {
			return (lhs + rhs);
		}
    };

    pcl::device::Intr _sCamIntr;

    pcl::device::Mat33  _mRwCurTrans;
    float3 _vTwCur;

    cv::gpu::DevMem2D_<float3> _cvgmVMapLocalCur;
    cv::gpu::DevMem2D_<float3> _cvgmNMapLocalCur;

    pcl::device::Mat33  _mRwPrev;
    float3 _vTwPrev;

    cv::gpu::DevMem2D_<float3> _cvgmVMapWorldPrev;
	cv::gpu::DevMem2D_<float3> _cvgmNMapWorldPrev;

    float _fDistThres;
    float _fSinAngleThres;

    int _nCols;
    int _nRows;

    mutable cv::gpu::DevMem2D_<float_type> _cvgmBuf;

    __device__ __forceinline__ bool searchForCorrespondence(int nX_, int nY_, float3* pf3NlPrev_, float3* pf3PtPrev_, float3* pf3PtCur_) const{
		//retrieve normal
		const float3& f3NlLocalCur = _cvgmNMapLocalCur.ptr(nY_)[nX_];	if (isnan (f3NlLocalCur.x)) return false;
		//transform the current vetex to reference camera coodinate system
		float3 f3PtLocalCur = _cvgmVMapLocalCur.ptr(nY_)[nX_]; if (isnan (f3PtLocalCur.x)) return false; //retrieve vertex from current frame
		float3 f3PtWorldCur = _mRwCurTrans * (f3PtLocalCur - _vTwCur); //transform LocalCur into World
		float3 f3PtCur_LocalPrev = _mRwPrev * f3PtWorldCur + _vTwPrev; //transform WorldCur into Prev Local
		//projection onto reference image
		int2 n2Ref;        
		n2Ref.x = __float2int_rn (f3PtCur_LocalPrev.x * _sCamIntr.fx / f3PtCur_LocalPrev.z + _sCamIntr.cx);  
		n2Ref.y = __float2int_rn (f3PtCur_LocalPrev.y * _sCamIntr.fy / f3PtCur_LocalPrev.z + _sCamIntr.cy);  
		//if projected out of the frame, return false
		if (n2Ref.x < 0 || n2Ref.y < 0 || n2Ref.x >= _nCols || n2Ref.y >= _nRows || f3PtCur_LocalPrev.z < 0) return false;
		//retrieve corresponding reference normal
		const float3& f3NlWorldRef = _cvgmNMapWorldPrev.ptr(n2Ref.y)[n2Ref.x];	if (isnan (f3NlWorldRef.x))  return false;
		//retrieve corresponding reference vertex
		const float3& f3PtWorldRef = _cvgmVMapWorldPrev.ptr(n2Ref.y)[n2Ref.x]; if (isnan (f3PtWorldRef.x))  return false;
		//check distance
		float fDist = norm (f3PtWorldRef - f3PtWorldCur); 
		if (fDist > _fDistThres)  return (false);
		//transform current normal to world
	    float3 f3NlWorldCur = _mRwCurTrans * f3NlLocalCur; 
		//check normal angle
		float fSin = norm ( cross(f3NlWorldCur, f3NlWorldRef) ); 
		if (fSin >= _fSinAngleThres) return (false);
		//return values
		*pf3NlPrev_ = f3NlWorldRef;
		*pf3PtPrev_ = f3PtWorldRef;
		*pf3PtCur_ = f3PtWorldCur;
		return (true);
    }//searchForCorrespondence()

    __device__ __forceinline__ void operator () () const {
		int nX = threadIdx.x + blockIdx.x * CTA_SIZE_X;
		int nY = threadIdx.y + blockIdx.y * CTA_SIZE_Y;

		float3 f3NlPrev, f3PtPrev, f3PtCurr;
		bool bCorrespondenceFound = false;
		if (nX < _nCols || nY < _nRows)  bCorrespondenceFound = searchForCorrespondence (nX, nY, &f3NlPrev, &f3PtPrev, &f3PtCurr);

		float row[7];

		if (bCorrespondenceFound){
			*(float3*)&row[0] = cross (f3PtCurr, f3NlPrev);
			*(float3*)&row[3] = f3NlPrev;
			row[6] = dot (f3NlPrev, f3PtPrev - f3PtCurr);
		}//if correspondence found
		else{
			row[0] = row[1] = row[2] = row[3] = row[4] = row[5] = row[6] = 0.f;
		}//if not found

		__shared__ float_type smem[CTA_SIZE];
	    int nThrID = Block::flattenedThreadId ();

		int nShift = 0;
		for (int i = 0; i < 6; ++i){        //_nRows
			#pragma unroll
			for (int j = i; j < 7; ++j){          // _nCols + b
				__syncthreads ();
				smem[nThrID] = row[i] * row[j];
				__syncthreads ();

				Block::reduce<CTA_SIZE>(smem, SDevPlus ());
				if (nThrID == 0) _cvgmBuf.ptr(nShift++)[blockIdx.x + gridDim.x * blockIdx.y] = smem[0];
				//nThrID 
			}//for
		}//for
		return;
    }//operator()
};//SDeviceICPRegistration

void registrationImageICP(  unsigned int uMatchedFeatures_, 
							const cv::gpu::GpuMat& cvgmMinMatchDistance_, const cv::gpu::GpuMat& cvgmMatchedLocationPrev_,
							const pcl::device::Intr& sCamIntr_, float fDistThres_, float fSinAngleThres_,
							const pcl::device::Mat33& RwCurTrans_, const float3& TwCur_, 
							const pcl::device::Mat33& RwRef_,      const float3& TwRef_, 
							const cv::gpu::GpuMat& cvgmVMapWorldPrev_, const cv::gpu::GpuMat& cvgmNMapWorldPrev_, 
							const cv::gpu::GpuMat& cvgmVMapLocalCur_,  const cv::gpu::GpuMat& cvgmNMapLocalCur_,
							cv::gpu::GpuMat* pcvgmSumBuf_){

	
}

}//device
}//btl