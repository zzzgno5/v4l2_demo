#include "mppdecode.h"

mppDecode::mppDecode()
{

}

mppDecode::~mppDecode()
{
	if (packet) 
	{
        mpp_packet_deinit(&packet);
        packet = NULL;
    }

	if (frame) 
	{
        mpp_frame_deinit(&frame);
        frame = NULL;
    }

	if (mCtx) 
	{
        mpp_destroy(mCtx);
        mCtx = NULL;
    }

	if (pktBuf) 
	{
        mpp_buffer_put(pktBuf);
        pktBuf = NULL;
    }

    if (frmBuf) 
	{
        mpp_buffer_put(frmBuf);
        frmBuf = NULL;
    }

	if (pktGrp) {
        mpp_buffer_group_put(pktGrp);
        pktGrp = NULL;
    }

    if (frmGrp) {
        mpp_buffer_group_put(frmGrp);
        frmGrp = NULL;
    }

    if (outPutFile) {
        fclose(outPutFile);
        outPutFile = NULL;
    }

}

void mppDecode::set_out_file_name(char *path)
{
	memset(fileName ,0, sizeof(fileName));
	strcpy(fileName, path);
}

int mppDecode::init()
{
	MPP_RET ret = MPP_OK;
	MpiCmd mpi_cmd = MPP_CMD_BASE;
    MppParam param = NULL;
	
	//创建 MPP context 和 MPP api 接口
	ret = mpp_create(&mCtx, &mApi);
    if (ret != MPP_OK) 
	{
		MPP_ERR("mpp_create erron (%d) \n", ret);
        return ret;
    }

	uint32_t need_split = 0;
	//MPP_DEC_SET_PARSER_SPLIT_MODE ：  （仅限解码）
	//自动拼包（建议开启），硬编解码器每次解码就是一个Frame，
	//所以如果输入的数据不确定是不是一个Frame
	//（例如可能是一个Slice、一个Nalu或者一个FU-A分包，甚至可能随意读的任意长度数据），
	//那就必须把该模式打开，MPP会自动分包拼包成一个完整Frame送给硬解码器
	mpi_cmd = MPP_DEC_SET_PARSER_SPLIT_MODE;
	param = &need_split;
	ret = mApi->control(mCtx, mpi_cmd, param);
	if (ret != MPP_OK) 
	{
        MPP_ERR("MPP_DEC_SET_PARSER_SPLIT_MODE set erron (%d) \n", ret);
        return ret;
    }

	//设置MPP为解码模式 
	//MPP_CTX_DEC ： 解码
	//MPP_VIDEO_CodingAVC ： H.264
	//MPP_VIDEO_CodingHEVC :  H.265
	//MPP_VIDEO_CodingVP8 :  VP8
	//MPP_VIDEO_CodingVP9 :  VP9
	//MPP_VIDEO_CodingMJPEG : MJPEG
	ret = mpp_init(mCtx, MPP_CTX_DEC, MppCodingType::MPP_VIDEO_CodingMJPEG);
	if (MPP_OK != ret) 
	{
		MPP_ERR("mpp_init erron (%d) \n", ret);
        return ret;
	}

	MppFrameFormat frmType = IN_FRAME;
	param = &frmType;
	mApi->control(mCtx, MPP_DEC_SET_OUTPUT_FORMAT, param);

	if(strlen(fileName))
	{
		outPutFile = fopen(fileName, "wb+");
	}

	return MPP_OK;
}


int mppDecode::init_packet_and_frame(int width, int height)
{
	RK_U32 hor_stride = MPP_ALIGN(width, 16);
    RK_U32 ver_stride = MPP_ALIGN(height, 16);
    

	int ret;
	ret = mpp_buffer_group_get_internal(&frmGrp, MPP_BUFFER_TYPE_ION);
	if(ret)
	{
		MPP_ERR("frmGrp mpp_buffer_group_get_internal erron (%d)\r\n",ret);
		return -1;
	}
    

	ret = mpp_buffer_group_get_internal(&pktGrp, MPP_BUFFER_TYPE_ION);
	if(ret)
	{
		MPP_ERR("frmGrp mpp_buffer_group_get_internal erron (%d)\r\n",ret);
		return -1;
	}
	ret = mpp_frame_init(&frame); /* output frame */
    if (MPP_OK != ret) 
	{
        MPP_ERR("mpp_frame_init failed\n");
        return -1;
    }
	ret = mpp_buffer_get(frmGrp, &frmBuf, hor_stride * ver_stride * 4);
    if (ret) 
	{
        MPP_ERR("frmGrp mpp_buffer_get erron (%d) \n", ret);
        return -1;
    }
	ret = mpp_buffer_get(pktGrp, &pktBuf, width*height*2);
    if (ret) 
	{
        MPP_ERR("pktGrp mpp_buffer_get erron (%d) \n", ret);
        return -1;
    }
	mpp_packet_init_with_buffer(&packet, pktBuf);
	dataBuf = (char *)mpp_buffer_get_ptr(pktBuf);

	mpp_frame_set_buffer(frame, frmBuf);
    return 0;
}

int mppDecode::decode(char *srcFrm, size_t srcLen, int pktEos)
{
	MppTask task = NULL;
	int ret;

	memset(dataBuf, 0, sizeof(dataBuf));
	memcpy(dataBuf, srcFrm, srcLen);
    //return 0; //测试memset 和 memcpy 对CPU占用率的影响
	mpp_packet_set_pos(packet, dataBuf);
    mpp_packet_set_length(packet, srcLen);

	if(pktEos)
	{
		mpp_packet_set_eos(packet);
	}

	ret = mApi->poll(mCtx, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret) 
	{
        MPP_ERR("mpp input poll failed\n");
        return ret;
    }

	ret = mApi->dequeue(mCtx, MPP_PORT_INPUT, &task);  /* input queue */
    if (ret) 
	{
        MPP_ERR("mpp task input dequeue failed\n");
        return ret;
    }

	mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, packet);
    mpp_task_meta_set_frame (task, KEY_OUTPUT_FRAME,  frame);

	ret = mApi->enqueue(mCtx, MPP_PORT_INPUT, task);  /* input queue */
    if (ret) 
	{
        MPP_ERR("mpp task input enqueue failed\n");
        return ret;
    }

	/* poll and wait here */
    ret = mApi->poll(mCtx, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret) 
	{
        MPP_ERR("mpp output poll failed\n");
        return ret;
    }

	ret = mApi->dequeue(mCtx, MPP_PORT_OUTPUT, &task); /* output queue */
    if (ret) 
	{
        MPP_ERR("mpp task output dequeue failed\n");
        return ret;
    }

	if (task)
	{
		MppFrame frameOut = NULL;
		mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &frameOut);

		if (frame) 
		{
            /* write frame to file here */
            //if (data->fp_output)
            //    dump_mpp_frame_to_file(frame, data->fp_output);
            if (outPutFile)
			{
                printf("write frame to file\r\n");
				dump_mpp_frame_to_file(frame, outPutFile);
			}

            if (mpp_frame_get_eos(frameOut))
            {
				MPP_DBG("found eos frame\n");
			}
        }

		ret = mApi->enqueue(mCtx, MPP_PORT_OUTPUT, task);
        if (ret)
        {
			MPP_ERR("mpp task output enqueue failed\n");
		}
	}
	return ret;
}


void mppDecode::dump_mpp_frame_to_file(MppFrame frame, FILE *fp)
{
    RK_U32 width    = 0;
    RK_U32 height   = 0;
    RK_U32 h_stride = 0;
    RK_U32 v_stride = 0;
    MppFrameFormat fmt  = IN_FRAME;
    MppBuffer buffer    = NULL;
    RK_U8 *base = NULL;


    if (NULL == fp || NULL == frame)
        return ;

    width    = mpp_frame_get_width(frame);
    height   = mpp_frame_get_height(frame);
    h_stride = mpp_frame_get_hor_stride(frame);
    v_stride = mpp_frame_get_ver_stride(frame);
    fmt      = mpp_frame_get_fmt(frame);
    buffer   = mpp_frame_get_buffer(frame);

    if (NULL == buffer)
        return ;

    base = (RK_U8 *)mpp_buffer_get_ptr(buffer);

    switch (fmt) 
	{
	    case MPP_FMT_YUV422SP : 
		{
	        /* YUV422SP -> YUV422P for better display */
	        RK_U32 i, j;
	        RK_U8 *base_y = base;
	        RK_U8 *base_c = base + h_stride * v_stride;
	        RK_U8 *tmp = new RK_U8[h_stride * height * 2];
	        RK_U8 *tmp_u = tmp;
	        RK_U8 *tmp_v = tmp + width * height / 2;

	        for (i = 0; i < height; i++, base_y += h_stride)
	            fwrite(base_y, 1, width, fp);

	        for (i = 0; i < height; i++, base_c += h_stride) {
	            for (j = 0; j < width / 2; j++) {
	                tmp_u[j] = base_c[2 * j + 0];
	                tmp_v[j] = base_c[2 * j + 1];
	            }
	            tmp_u += width / 2;
	            tmp_v += width / 2;
	        }

	        fwrite(tmp, 1, width * height, fp);
	
	        delete []tmp;
			break;
	    } 
	    case MPP_FMT_YUV420SP : 
		{
	        RK_U32 i;
	        RK_U8 *base_y = base;
	        RK_U8 *base_c = base + h_stride * v_stride;

			
	        for (i = 0; i < height; i++, base_y += h_stride) 
			{
	            fwrite(base_y, 1, width, fp);
	        }
	        for (i = 0; i < height / 2; i++, base_c += h_stride) 
			{
	            fwrite(base_c, 1, width, fp);
	        }
			break;
	    } 
	    case MPP_FMT_YUV420P : 
		{
	        RK_U32 i;
	        RK_U8 *base_y = base;
	        RK_U8 *base_c = base + h_stride * v_stride;

	        for (i = 0; i < height; i++, base_y += h_stride)
			{
	            fwrite(base_y, 1, width, fp);
	        }
	        for (i = 0; i < height / 2; i++, base_c += h_stride / 2) 
			{
	            fwrite(base_c, 1, width / 2, fp);
	        }
	        for (i = 0; i < height / 2; i++, base_c += h_stride / 2) 
			{
	            fwrite(base_c, 1, width / 2, fp);
	        }
			break;
	    } 	
	    case MPP_FMT_YUV444SP : 
		{
	        /* YUV444SP -> YUV444P for better display */
	        RK_U32 i, j;
	        RK_U8 *base_y = base;
	        RK_U8 *base_c = base + h_stride * v_stride;
	        RK_U8 *tmp = new RK_U8[h_stride * height * 2];
	        RK_U8 *tmp_u = tmp;
	        RK_U8 *tmp_v = tmp + width * height;

	        for (i = 0; i < height; i++, base_y += h_stride)
	        {
	        	fwrite(base_y, 1, width, fp);
			}
	            

	        for (i = 0; i < height; i++, base_c += h_stride * 2) 
			{
	            for (j = 0; j < width; j++) 
				{
	                tmp_u[j] = base_c[2 * j + 0];
	                tmp_v[j] = base_c[2 * j + 1];
	            }
	            tmp_u += width;
	            tmp_v += width;
	        }
	        fwrite(tmp, 1, width * height * 2, fp);

	        delete []tmp;
			break;
	    } 
	    case MPP_FMT_YUV400: 
		{
	        RK_U32 i;
	        RK_U8 *base_y = base;
	        RK_U8 *tmp = new RK_U8[h_stride * height];

	        for (i = 0; i < height; i++, base_y += h_stride)
	        {
				fwrite(base_y, 1, width, fp);
			}

	        delete []tmp;
			break;
	    } 
	    default : 
	   	{
	        MPP_ERR("not supported format %d\n", fmt);
			break;
	    } 
    }
}





