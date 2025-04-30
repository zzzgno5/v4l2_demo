#ifndef MPP_DECODE_H
#define MPP_DECODE_H

//C 标准函数库
#include <stdio.h>
#include <stdint.h>
#include <string.h>

//Linux 函数库
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

//C++ 标准函数库
#include <iostream>
#include <vector>


//MPP函数库
#include <rockchip/vpu.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_type.h>
#include <rockchip/vpu_api.h>
#include <rockchip/mpp_err.h>
#include <rockchip/mpp_task.h>
#include <rockchip/mpp_meta.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi_cmd.h>



class mppDecode
{
public:
	mppDecode();
	~mppDecode();

	int init();
	int init_packet_and_frame(int width, int height);
	int decode(char *srcFrm, size_t srcLen, int pktEos);

	void set_out_file_name(char *path);
	
	
private:
	//宏定义
	#define MPP_ALIGN(x, a)   (((x)+(a)-1)&~((a)-1))
	#define IN_FRAME MPP_FMT_YUV420SP
	
	#define ESC_START     "\033["
	#define ESC_END       "\033[0m"
	#define COLOR_GREEN   "32;40;1m"
	#define COLOR_RED     "31;40;1m"
	#define MPP_DBG(format, args...) //(printf( ESC_START COLOR_GREEN "[MPP DBG]-[%s]-[%05d]:" format ESC_END, __FUNCTION__, (int)__LINE__, ##args))
	#define MPP_ERR(format, args...) (printf( ESC_START COLOR_RED   "[MPP ERR]-[%s]-[%05d]:" format ESC_END, __FUNCTION__, (int)__LINE__, ##args))

private:
	void dump_mpp_frame_to_file(MppFrame frame, FILE *fp);

private:
	MppBufferGroup frmGrp;
	MppBufferGroup pktGrp;
	MppPacket      packet;
	MppFrame       frame;
	size_t         packetSize;

	MppBuffer      frmBuf   = NULL;
	MppBuffer      pktBuf   = NULL;

	char *dataBuf = NULL;

	MppCtx  mCtx   = NULL;
    MppApi *mApi   = NULL;

	char fileName[256]  = {0};
	FILE *outPutFile = NULL;

};





#endif

