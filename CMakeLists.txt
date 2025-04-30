# CMake 最低版本号要求
cmake_minimum_required (VERSION 2.8)

# 项目信息
project (MJPEG_TEST_DEMO)

SET (CMAKE_CXX_COMPILER "g++")
SET (CMAKE_CXX_FLAGS "-O3 -g -lm -ldl -pthread")
SET (CMAKE_C_COMPILER "gcc")
SET (CMAKE_C_FLAGS "-O3 -g -lm -ldl -pthread")

include_directories(./mpp/)
include_directories(./v4l2/)
include_directories(./drm/)

# 查找目录下的所有源文件
# 并将名称保存到 DIR_SRCS 变量
aux_source_directory(./                       USR_SRCS)
aux_source_directory(./mpp                    MPP_SRCS)
aux_source_directory(./v4l2                   V4L2_SRCS)
aux_source_directory(./drm                   DRM_SRCS)

# 指定生成目标
add_executable(mjpegDecTest /
${USR_SRCS} /
${MPP_SRCS} /
${V4L2_SRCS} /
${DRM_SRCS} /
)

target_link_libraries(mjpegDecTest -lrockchip_mpp drm)
