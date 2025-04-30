//C 库
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>


//C++ 标准函数库
#include <iostream>
#include <vector>
#include <queue>



#include "V4l2.h"
#include "mppdecode.h"
#include "DrmDisplay.h"

//宏定义
#define IMAGE_WIDTH  1920
#define IMAGE_HEIGHT 1080

using namespace std;


static int64_t get_time() 
{
    struct timeval tv_date;
    gettimeofday(&tv_date, NULL);
    return (int64_t)tv_date.tv_sec * 1000000 + (int64_t)tv_date.tv_usec;
}

int main()
{
	/*************************** 初始化摄像头 *********************************/
    
	struct v4l2_buf* mV4l2Buf;
	int cameraDevFd = usb_camera_open("/dev/video0", &mV4l2Buf ,IMAGE_WIDTH, IMAGE_HEIGHT);
	if(cameraDevFd < 0)
	{
		return -1;
	}
    
    //读取摄像头帧率
    struct v4l2_streamparm Stream_Parm;
	int ret = v4l2_g_parm(cameraDevFd, &Stream_Parm);
	if(ret < 0)
	{
		printf("get usb camera fps failed\r\n");
		return -1;
	}
	int fpsNum = (int)(Stream_Parm.parm.capture.timeperframe.denominator);
	if(fpsNum <= 0)
	{
		fpsNum = 10;
	}
	printf("fpsNum:%d\r\n",fpsNum);


	 /*************************** 初始化DRM显示 *********************************/
    DrmDisplay drm;
    try {
        //drm.Init(IMAGE_WIDTH, IMAGE_HEIGHT, DRM_FORMAT_NV12); // 使用NV12格式
    	drm.InitRed();
	drm.DisplayRed();
	sleep(10);
    } catch (const std::exception &e) {
        std::cerr << "DRM初始化失败: " << e.what() << std::endl;
        return -1;
    }	
    
	/*************************** 启动MJPEG解码 *********************************/
	mppDecode *mppDec = new mppDecode;
    
    //设置FILE name 后会将解码后的数据保存到文件
	mppDec->set_out_file_name((char *)"./camera.yuv");
    
    
	ret = mppDec->init();
	if (ret != MPP_OK)
	{
		printf("mppDec->init erron (%d) \r\n", ret);
		return -1;
	}

	ret = mppDec->init_packet_and_frame(IMAGE_WIDTH, IMAGE_HEIGHT);
	if (ret != MPP_OK)
	{
		printf("mppApi->init init_packet_and_frame (%d) \r\n", ret);
		return -1;
	}
    struct v4l2_buf_unit* mV4l2BufUnit;
	int framCount = 0;
    
	int64_t timeS = get_time();

	while(1)
	{
		ret = v4l2_poll(cameraDevFd);
        if(ret < 0)
        {
			printf("[1]v4l2_poll failed\r\n");
			usleep(100*1000);
			continue;
		}

		mV4l2BufUnit = v4l2_dqbuf(cameraDevFd, mV4l2Buf);
		if(!mV4l2BufUnit)
        {
            usleep(100*1000);
			printf("v4l2_dqbuf empty\r\n");
            continue;
        }

		char *dataAddr = (char *)mV4l2BufUnit->start;
		ret = mppDec->decode(dataAddr,mV4l2BufUnit->length, 0);
		if (ret >= 0)
		{
   			// 获取解码后的NV12数据
			    uint8_t* yuv_data = mppDec->get_decoded_frame();
			    size_t yuv_size = mppDec->get_decoded_size();
				   if (yuv_data && yuv_size > 0) {
		        // 现在可以将yuv_data传递给DRM显示
        			drm.DisplayFrame(yuv_data);
        
       			 // 或者保存到文件（调试用）
		        // FILE* fp = fopen("frame.nv12", "wb");
       			 // fwrite(yuv_data, 1, yuv_size, fp);
        		// fclose(fp);
   			 }
		}
		else
		{
			printf("decode erron");
			usleep(500*1000);
		}


		ret = v4l2_qbuf(cameraDevFd, mV4l2BufUnit);
        if(ret < 0)
        {
            printf("failed to q buf\n");
        }
        
        //usleep(10*1000); //测试延时后CPU占用率是否下降
        
		mV4l2BufUnit = NULL;
		framCount++;
		int64_t timeE = get_time();
		if( (timeE-timeS)/1000 >= 1000  )
		{
			printf("framCount:%d \r\n", framCount);
			framCount = 0;
			timeS = get_time();
		}

	}

	return 0;
}


