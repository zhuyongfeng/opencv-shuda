#define INFO
#define TIMER
#include <vector>
#include "Utility.hpp"
#include "opencv2/gpu/gpu.hpp"
#include "VideoSourceKinect.hpp"
#include "Model.h"

#include <pcl/point_types.h>
#include <pcl/ModelCoefficients.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>

namespace btl
{
namespace extra
{

CModel::CModel(VideoSourceKinect& cKinect_)
	:_cKinect(cKinect_)
{
	//allocate
	//control
	_nKNearest = 6;

	for(int i=0; i<4; i++)
	{
		int nRows = KINECT_HEIGHT>>i; 
		int nCols = KINECT_WIDTH>>i;
		_acvmShrPtrNormalClusters[i].reset(new cv::Mat(nRows,nCols,CV_16SC1));
		_acvmShrPtrDistanceClusters[i].reset(new cv::Mat(nRows,nCols,CV_16SC1));
	}
	PRINTSTR("CModel initialized...");
}
CModel::~CModel(void)
{
}
void CModel::storeCurrentFrame()
{
	return;
}

void CModel::extractPlaneGL(unsigned int uLevel_, const std::vector<int>& vX_, const std::vector<int>& vY_, std::vector<Eigen::Vector3d>* pvPlane_)
{
	cv::Mat& cvmDepth = _vcvmPyramidDepths[uLevel_];
	pvPlane_->clear();
	for (unsigned int i=0;i<vX_.size(); i++)
	{
		int x = vX_[i];
		int y = vY_[i];
		Eigen::Vector3d eivPt;
		//_cKinect.unprojectRGBGL(cvmDepth,y,x,eivPt.data(),uLevel_);
		pvPlane_->push_back(eivPt);
		cvmDepth.at<float>(y,x)=0.f;
	}
}
void CModel::detectPlanePCL(unsigned int uLevel_,std::vector<int>* pvXIdx_, std::vector<int>* pvYIdx_)
{
	const cv::Mat& cvmDepth = _vcvmPyramidDepths[uLevel_];
	//do plane detection in disparity domain
	cv::Mat	cvmDisparity;
	float fMin,fMax,fRange,fRatio;
	btl::utility::convert2DisparityDomain<float>(cvmDepth,&cvmDisparity,&fMax,&fMin);
	//normalize the x y into the same scale as disparity
	fRange = fMax - fMin;
	PRINT(fRange);
	fRatio = fRange/cvmDepth.cols;//make the x
	//each pixel in cvmDisparity is now equivalent to (x*fRatio, y*fRatio, disparity)
	//construct PCL point cloud data
	float* pDisparity = (float*)cvmDisparity.data;
	pcl::PointCloud<pcl::PointXYZ> pclNoneZero;
	
	for(int r = 0; r<cvmDisparity.rows; r++)
	for(int c = 0; c<cvmDisparity.cols; c++)
	{
		float dz = *pDisparity;
		if( fabs(dz) > SMALL )
		{
			pcl::PointXYZ point(c*fRatio,r*fRatio,dz);
			pclNoneZero.push_back(point);
		}
		pDisparity++;
	}
	//detect
	pcl::ModelCoefficients::Ptr pCoefficients (new pcl::ModelCoefficients);
	pcl::PointIndices::Ptr pInliers (new pcl::PointIndices);
	// Create the segmentation object
	pcl::SACSegmentation<pcl::PointXYZ> cSeg;
	// Optional
	cSeg.setOptimizeCoefficients (true);
	// Mandatory
	cSeg.setModelType (pcl::SACMODEL_PLANE);
	cSeg.setMethodType (pcl::SAC_RANSAC);
	cSeg.setDistanceThreshold (fRange/1000.);
	cSeg.setInputCloud (pclNoneZero.makeShared ());
	cSeg.segment (*pInliers, *pCoefficients);
	// retrieve inliers
	pvXIdx_->clear();pvYIdx_->clear();
	for (size_t i = 0; i < pInliers->indices.size (); ++i)
	{
		int y = int( pclNoneZero.points[pInliers->indices[i]].y/fRatio + .5);
		int x = int( pclNoneZero.points[pInliers->indices[i]].x/fRatio + .5);
		pvYIdx_->push_back(y);
		pvXIdx_->push_back(x);
	}
	return;
}
void CModel::normalHistogram( const cv::Mat& cvmNls_, int nSamples_, std::vector< tp_normal_hist_bin >* pvNormalHistogram_)
{
	//clear and re-initialize pvvIdx_
	int nSampleAzimuth = nSamples_<<2; //nSamples*4
	pvNormalHistogram_->clear();
	pvNormalHistogram_->resize(nSamples_*nSampleAzimuth,tp_normal_hist_bin(std::vector<unsigned int>(),Eigen::Vector3d(0,0,0)));
	const double dS = M_PI_2/nSamples_;//sampling step
	int r,c,rc;
	const float* pNl = (const float*) cvmNls_.data;
	for(unsigned int i =0; i< cvmNls_.total(); i++, pNl+=3)	{
		if( pNl[2]<0 || fabs(pNl[0])+fabs(pNl[1])+fabs(pNl[2])<0.0001 ) {continue;}
		btl::utility::normalVotes<float>(pNl,dS,&r,&c);
		rc = r*nSampleAzimuth+c;
		(*pvNormalHistogram_)[rc].first.push_back(i);
		(*pvNormalHistogram_)[rc].second += Eigen::Vector3d(pNl[0],pNl[1],pNl[2]);
	}
	//average the 
	for(std::vector<tp_normal_hist_bin>::iterator it_vNormalHist = pvNormalHistogram_->begin();
		it_vNormalHist!=pvNormalHistogram_->end(); it_vNormalHist++) {
		if(it_vNormalHist->first.size()>0) {
			it_vNormalHist->second.normalize();
		}
	}

	return;
}
void CModel::clusterNormal(const unsigned short& uPyrLevel_,cv::Mat* pcvmLabel_,std::vector< std::vector< unsigned int > >* pvvLabelPointIdx_)
{
	//define constants
	const int nSampleElevation = 4;
	const double dCosThreshold = std::cos(M_PI_4/nSampleElevation);
	const cv::Mat& cvmNls = *_cKinect._acvmShrPtrPyrNls[uPyrLevel_];
	//make a histogram on the top pyramid
	std::vector< tp_normal_hist_bin > vNormalHist;//idx of sampling the unit half sphere of top pyramid
	//_vvIdx is organized as r(elevation)*c(azimuth) and stores the idx of Normals
	normalHistogram(cvmNls,nSampleElevation,&vNormalHist);
	
	//re-cluster the normals
	pvvLabelPointIdx_->clear();
	pcvmLabel_->setTo(-1);
	short nLabel =0;
	for(unsigned int uIdxBin = 0; uIdxBin < vNormalHist.size(); uIdxBin++){
		if(vNormalHist[uIdxBin].first.size() < _usMinArea ) continue;
		//get neighborhood of a sampling bin
		std::vector<unsigned int> vNeighourhood; 
		btl::utility::getNeighbourIdxCylinder< unsigned int >(nSampleElevation,nSampleElevation*4,uIdxBin,&vNeighourhood);
		//traverse the neighborhood and cluster the 
		std::vector<unsigned int> vLabelNormalIdx;
		for( std::vector<unsigned int>::const_iterator cit_vNeighbourhood=vNeighourhood.begin();
			cit_vNeighbourhood!=vNeighourhood.end();cit_vNeighbourhood++) {
			btl::utility::normalCluster<double>(cvmNls,vNormalHist[*cit_vNeighbourhood].first,vNormalHist[*cit_vNeighbourhood].second,dCosThreshold,nLabel,pcvmLabel_,&vLabelNormalIdx);
		}
		nLabel++;
		pvvLabelPointIdx_->push_back(vLabelNormalIdx);
		//compute average normal
		/*Eigen::Vector3d eivAvgNl;
		btl::utility::avgNormals<double>(cvmNls,vLabelNormalIdx,&eivAvgNl);
		_vLabelAvgNormals.push_back(eivAvgNl);*/
	}
	return;
}
void CModel::distanceHistogram( const cv::Mat& cvmNls_, const cv::Mat& cvmPts_, const unsigned int& nSamples, 
	const std::vector< unsigned int >& vIdx_, tp_hist* pvDistHist )
{
	const double dLow  = -3;
	const double dHigh =  3;
	const double dSampleStep = ( dHigh - dLow )/nSamples; 

	pvDistHist->clear();
	pvDistHist->resize(nSamples,tp_pair_hist_bin(std::vector<tp_pair_hist_element>(), 0.) );
	const float*const pPt = (float*) cvmPts_.data;
	const float*const pNl = (float*) cvmNls_.data;
	//collect the distance histogram
	for(std::vector< unsigned int >::const_iterator cit_vPointIdx = vIdx_.begin();
		cit_vPointIdx!=vIdx_.end(); cit_vPointIdx++)
	{
		unsigned int uOffset = (*cit_vPointIdx)*3;
		double dDist = pPt[uOffset]*pNl[uOffset] + pPt[uOffset+1]*pNl[uOffset+1] + pPt[uOffset+2]*pNl[uOffset+2];

		int nBin = floor( (dDist -dLow)/ dSampleStep );
		if( nBin >= 0 && nBin <nSamples)
		{
			(*pvDistHist)[nBin].first.push_back(tp_pair_hist_element(dDist,*cit_vPointIdx));
			(*pvDistHist)[nBin].second += dDist;
		}
	}

	//calc the avg distance for each bin 
	//construct a list for sorting
	for(std::vector< tp_pair_hist_bin >::iterator cit_vDistHist = pvDistHist->begin();
		cit_vDistHist != pvDistHist->end(); cit_vDistHist++ )
	{
		unsigned int uBinSize = cit_vDistHist->first.size();
		if( uBinSize==0 ) continue;

		//calculate avg distance
		cit_vDistHist->second /= uBinSize;
	}
	return;
}
void CModel::clusterDistance( const unsigned short uPyrLevel_, const std::vector< std::vector<unsigned int> >& vvNormalClusterPtIdx_, cv::Mat* cvmDistanceClusters_ )
{
	cvmDistanceClusters_->setTo(-1);
	//construct the label mat
	const cv::Mat& cvmPts = *_cKinect._acvmShrPtrPyrPts[uPyrLevel_];
	const cv::Mat& cvmNls = *_cKinect._acvmShrPtrPyrNls[uPyrLevel_];
	const double dLow  = -3;
	const double dHigh =  3;
	const int nSamples = 400;
	const double dSampleStep = ( dHigh - dLow )/nSamples; 
	const double dMergeStep = dSampleStep;

	tp_hist	vDistHist; //histogram of distancte vector< vDist, cit_vIdx > 
	short sLabel = 0;
	for(std::vector< std::vector< unsigned int > >::const_iterator cit_vvLabelPointIdx = vvNormalClusterPtIdx_.begin();
		cit_vvLabelPointIdx!=vvNormalClusterPtIdx_.end(); cit_vvLabelPointIdx++){
			//collect 
			distanceHistogram( cvmNls, cvmPts, nSamples, *cit_vvLabelPointIdx, &vDistHist );
			std::vector< tp_flag > vMergeFlags(nSamples, CModel::EMPTY); //==0 no merging, ==1 merge with left, ==2 merge with right, ==3 merging with both
			calcMergeFlag( vDistHist, dMergeStep, &vMergeFlags ); // EMPTY/NO_MERGE/MERGE_WITH_LEFT/MERGE_WITH_BOTH/MERGE_WITH_RIGHT 
			//cluster
			mergeBins( vMergeFlags, vDistHist, *cit_vvLabelPointIdx, &sLabel, &*_acvmShrPtrDistanceClusters[uPyrLevel_] );
			sLabel++;
	}//for each normal label

}
void CModel::detectPlaneFromCurrentFrame(const short uPyrLevel_)
{
	//get next frame
#ifdef TIMER	
	// timer on
	_cT0 =  boost::posix_time::microsec_clock::local_time(); 
#endif
//load pyramids
	_cKinect.getNextPyramid(4); //output _vvN
	_usMinArea = btl::extra::videosource::__aKinectWxH[uPyrLevel_]/60;
//cluster the top pyramid
	clusterNormal(uPyrLevel_,&*_acvmShrPtrNormalClusters[uPyrLevel_],&_vvLabelPointIdx);
//enforce position continuity
	clusterDistance(uPyrLevel_,_vvLabelPointIdx,&*_acvmShrPtrDistanceClusters[uPyrLevel_]);

#ifdef TIMER
	// timer off
	_cT1 =  boost::posix_time::microsec_clock::local_time(); 
	_cTDAll = _cT1 - _cT0 ;
	_fFPS = 1000.f/_cTDAll.total_milliseconds();
	PRINT( _fFPS );
#endif
	return;
}

void CModel::calcMergeFlag( const tp_hist& vDistHist, const double& dMergeDistance, std::vector< tp_flag >* pvMergeFlags_ )
{
	//merge the bins whose distance is similar
	std::vector< tp_flag >::iterator it_vMergeFlags = pvMergeFlags_->begin()+1; 
	std::vector< tp_flag >::iterator it_prev;
	std::vector< tp_pair_hist_bin >::const_iterator cit_prev;
	std::vector< tp_pair_hist_bin >::const_iterator cit_endm1 = vDistHist.end() - 1;

	for(std::vector< tp_pair_hist_bin >::const_iterator cit_vDistHist = vDistHist.begin() + 1;
		cit_vDistHist != cit_endm1; cit_vDistHist++,it_vMergeFlags++ ) {
			unsigned int uBinSize = cit_vDistHist->first.size();
			if(0==uBinSize) continue;
			*it_vMergeFlags = CModel::NO_MERGE;
			cit_prev = cit_vDistHist -1;
			it_prev  = it_vMergeFlags-1;
			if( CModel::EMPTY == *it_prev ) continue;

			if( fabs(cit_prev->second - cit_vDistHist->second) < dMergeDistance ){ //avg distance smaller than the sample step.
				//previou bin
				if     (CModel::NO_MERGE       ==*it_prev){	*it_prev = CModel::MERGE_WITH_RIGHT;}
				else if(CModel::MERGE_WITH_LEFT==*it_prev){ *it_prev = CModel::MERGE_WITH_BOTH; }
				//current bin
				*it_vMergeFlags = CModel::MERGE_WITH_LEFT;
			}//if mergable
	}//for each bin
}

void CModel::mergeBins( const std::vector< tp_flag >& vMergeFlags_, const tp_hist& vDistHist_, const std::vector< unsigned int >& vLabelPointIdx_, short* pLabel_, cv::Mat* pcvmLabel_ )
{
	std::vector< tp_flag >::const_iterator cit_vMergeFlags = vMergeFlags_.begin();
	std::vector< tp_pair_hist_bin >::const_iterator cit_endm1 = vDistHist_.end() - 1;
	short* pDistanceLabel = (short*) pcvmLabel_->data;
	for(std::vector< tp_pair_hist_bin >::const_iterator cit_vDistHist = vDistHist_.begin() + 1;
		cit_vDistHist != cit_endm1; cit_vDistHist++,cit_vMergeFlags++ )	{
			if(CModel::EMPTY==*cit_vMergeFlags) continue;
			if(CModel::NO_MERGE==*cit_vMergeFlags||CModel::MERGE_WITH_RIGHT==*cit_vMergeFlags||
				CModel::MERGE_WITH_BOTH==*cit_vMergeFlags||CModel::MERGE_WITH_LEFT==*cit_vMergeFlags){
					if(cit_vDistHist->first.size()>_usMinArea){
						for( std::vector<tp_pair_hist_element>::const_iterator cit_vPair = cit_vDistHist->first.begin();
							cit_vPair != cit_vDistHist->first.end(); cit_vPair++ ){
								pDistanceLabel[cit_vPair->second] = *pLabel_;
						}//for 
					}//if
			}
			if(CModel::NO_MERGE==*cit_vMergeFlags||CModel::MERGE_WITH_LEFT==*cit_vMergeFlags){
				(*pLabel_)++;
			}
	}//for
}



}//extra
}//btl
