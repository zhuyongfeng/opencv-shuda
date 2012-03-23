#ifndef BTL_GEOMETRY_MODEL
#define BTL_GEOMETRY_MODEL

namespace btl{ namespace geometry
{
#define RESOL 512//256//128  
#define LEVEL 262144//65536//16384 //VOLUME_RESOL * VOLUME_RESOL 
#define TOTAL 134217728//16777216//2097152//VOLUME_LEVEL * VOLUME_RESOL 
class CModel
{
//type
public:
	typedef boost::shared_ptr<CModel> tp_shared_ptr;
	enum {VOLUME_RESOL = RESOL, VOLUME_LEVEL = LEVEL, VOXEL_TOTAL = TOTAL};
private:
	//methods
public:
	CModel();
	~CModel();
	void gpuRenderVoxelInWorldCVGL();
	void gpuCreateVBO(btl::gl_util::CGLUtil::tp_ptr pGL_);
	void gpuIntegrateFrameIntoVolumeCVCV(const btl::kinect::CKeyFrame& cFrame_);
	void unpack_tsdf (short2 value, float& tsdf, int& weight);
	void gpuRaycast(const btl::kinect::CKeyFrame& cCurrentFrame_, btl::kinect::CKeyFrame* pVirtualFrame_ ) const;
public:

	//data
	Eigen::Vector3d _eivAvgNormal;
	double _dAvgPosition;
	std::vector<unsigned int> _vVolumIdx;
//volume data
	//the center of the volume defines the origin of the world coordinate
	//and follows the right-hand cv-convention
	//physical size of the volume
	float _fVolumeSizeM;//in meter
	float _fVoxelSizeM; //in meter
	//truncated distance in meter
	//must be larger than 2*voxelsize 
	float _fTruncateDistanceM;
	//host
	cv::Mat _cvmYZxXVolContent; //y*z,x,CV_32FC1,x-first
	//device
	cv::gpu::GpuMat _cvgmYZxXVolContentCV;
	//render context
	btl::gl_util::CGLUtil::tp_ptr _pGL;
	GLuint _uVBO;
	cudaGraphicsResource* _pResourceVBO;
};




}//geometry
}//btl
#endif

