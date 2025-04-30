#pragma once
#include <sys/mman.h>  // 添加mmap相关声明
#include <unistd.h>    // 添加close等声明
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <cstdint>
#include <stdexcept>

class DrmDisplay {
public:
    // 初始化 DRM 并设置显示模式
    void Init(int width, int height, uint32_t format = DRM_FORMAT_NV12);
    
    // 显示 DMA-BUF 数据（NV12格式）
    //void DisplayDmaBuf(int dmabuf_fd); //  保留原有DMA-BUF
    void DisplayFrame(uint8_t *yuv_data);  // 用于显示内存中的YUV数据

    // 清理资源
    ~DrmDisplay();

private:
    int drm_fd_ = -1;
    uint32_t conn_id_ = 0, crtc_id_ = 0;
    drmModeModeInfo mode_ = {};
    uint32_t fb_id_ = 0;
    uint32_t width_ = 0, height_ = 0;
    uint32_t format_ = DRM_FORMAT_NV12;
    uint32_t y_pitch;
    uint32_t y_size;
    uint32_t uv_pitch;
    uint32_t uv_size;
    bool CheckFormatSupport(uint32_t format);
    void FindDisplayResources();
    void CreateDumbBuffer();
    void SetCrtc();
    uint32_t prev_fb_id_ = 0;
};

