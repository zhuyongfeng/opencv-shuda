#cmake_minimum_required (VERSION 2.6)
#this line will produce a policy warning
project (Classifier)
find_package( OpenCV )
find_package( CUDA )
include(FindCUDA)

if( UNIX )
    include_directories ( "/space/csxsl/src/opencv-shuda/btl_rgbd/include" )
    include_directories ( "/usr/local/include/eigen3" )
    include_directories ( "/usr/include/ni" )
    link_directories ( "/usr/local/lib64" )
    link_directories ( "/space/csxsl/src/opencv-shuda/btl_rgbd/lib/" )

#    set ( FLANN_LIBRARY "/usr/local/lib64/libflann.so" )
# set collection of libraries
    set ( EXTRA_LIBS ${EXTRA_LIBS} BtlRgbd OpenNI boost_system boost_filesystem boost_serialization 
    yaml-cpp glut GLU opencv_core opencv_highgui opencv_calib3d opencv_features2d opencv_video opencv_imgproc )
elseif( WIN32 )
    include_directories ( $ENV{EIGEN_INCLUDE_DIR} )
	include_directories ( $ENV{OPEN_NI_INCLUDE} )
    include_directories ( $ENV{BOOST_INCLUDE_DIR} ) 
    include_directories ( $ENV{FREEGLUT_INCLUDE_DIR} )
    include_directories ( $ENV{YAML-CPP_INCLUDE_DIR} )
    include_directories ( $ENV{GLEW_INCLUDE_DIR} )
	include_directories ( $ENV{CUDA_INC_PATH} )
    include_directories ( "C:/csxsl/src/opencv-shuda/btl_rgbd/include" )
    
    link_directories ( "C:/csxsl/src/opencv-shuda/btl_rgbd/lib" )
    link_directories ( $ENV{OPEN_NI_LIB_DIR}  )
    link_directories ( $ENV{BOOST_LIB_DIR} )
    link_directories ( $ENV{FREEGLUT_LIB_DIR} )
    link_directories ( $ENV{YAML-CPP_LIB_DIR} )
    link_directories ( $ENV{GLEW_LIB_DIR} )
    if(MSVC)
        set(SHUDALIB optimized BtlRgbd debug BtlRgbdd)
	    set(CUDALIB  optimized CudaLib  debug CudaLibd )
    	set(YAML-CPP optimized yaml-cpp debug yaml-cppd)
        set ( EXTRA_LIBS ${EXTRA_LIBS} ${SHUDALIB} ${CUDALIB} ${CUDA_CUDA_LIBRARY} ${CUDA_CUDART_LIBRARY} OpenNI64  freeglut GLU32 ${OpenCV_LIBS} ${YAML-CPP} glew32)
		#${Boost_SYSTEM_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SERIALIZATION_LIBRARY}
    endif()
endif()

#find_package(PCL 1.2 REQUIRED)
#include_directories(${PCL_INCLUDE_DIRS})
#link_directories(${PCL_LIBRARY_DIRS})
#add_definitions(${PCL_DEFINITIONS})

add_executable(Classifier classifier/Classifier.cpp)
# link with the 3d party libraries
target_link_libraries( Classifier ${PCL_LIBRARIES} ${EXTRA_LIBS} )
