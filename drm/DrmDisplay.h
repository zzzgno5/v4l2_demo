#ifndef DRM_DISPLAY_H
#define DRM_DISPLAY_H

#include <cstdint>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>  // 添加这行以获取DRM格式定义
#include <stdexcept>
#include <string>

class DrmDisplay {
public:
    DrmDisplay();
    ~DrmDisplay();
    
    void Init(int width, int height, uint32_t format);
    void InitRed();
    void DisplayRed();
    void DisplayFrame(uint8_t *yuv_data);
    bool CheckFormatSupport(uint32_t format);

private:
    // DRM基础变量
    int drm_fd_ = -1;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t format_ = 0;
    
    // DRM资源ID
    uint32_t crtc_id_ = 0;
    uint32_t conn_id_ = 0;      // 修复：统一使用conn_id_
    drmModeModeInfo mode_ = {};
    
    // 缓冲区相关
    uint32_t fb_id_ = 0;
    uint32_t prev_fb_id_ = 0;   // 新增prev_fb_id_
    uint32_t dumb_handle_ = 0;  // 新增dumb_handle_
    uint32_t pitch_ = 0;        // 新增pitch_
    void* mapped_ptr_ = nullptr;// 新增mapped_ptr_

    // 私有方法
    void FindDisplayResources();
    void SetCrtc();
    void CreateDumbBuffer();
    void CreateRedBuffer();
};

#endif

