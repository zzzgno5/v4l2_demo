#include "DrmDisplay.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <iostream>

#define ALIGN(value, align) (((value) + (align) - 1) & ~((align) - 1))
#define DRM_DEVICE "/dev/dri/card0"

// 初始化 DRM 显示
// 在 DrmDisplay::Init() 中添加格式检查
void DrmDisplay::Init(int width, int height, uint32_t format) {
    // 1. 打开DRM设备（必须首先执行）
    drm_fd_ = open(DRM_DEVICE, O_RDWR | O_CLOEXEC);
    if (drm_fd_ < 0) {
        throw std::runtime_error(std::string("Failed to open DRM device: ") + strerror(errno));
    }

    // 2. 设置基本参数
    width_ = width;
    height_ = height;
    format_ = format;

    // 3. 查找显示资源（必须先于plane操作）
    FindDisplayResources();

    // 4. 检查格式支持（可选步骤，可移到DisplayFrame中）
    drmModePlaneResPtr plane_res = drmModeGetPlaneResources(drm_fd_);
    if (!plane_res) {
        close(drm_fd_);
        drm_fd_ = -1;
        throw std::runtime_error("drmModeGetPlaneResources failed");
    }

    bool format_supported = false;
    for (uint32_t i = 0; i < plane_res->count_planes; i++) {
        drmModePlanePtr plane = drmModeGetPlane(drm_fd_, plane_res->planes[i]);
        if (!plane) continue;

        for (uint32_t j = 0; j < plane->count_formats; j++) {
            if (plane->formats[j] == format) {
                format_supported = true;
                break;
            }
        }
        drmModeFreePlane(plane);
        if (format_supported) break;
    }
    drmModeFreePlaneResources(plane_res);

    if (!format_supported) {
        close(drm_fd_);
        drm_fd_ = -1;
        throw std::runtime_error("Requested format not supported");
    }

    // 5. 初始化CRTC
    SetCrtc();
}

void DrmDisplay::FindDisplayResources() {
    drmModeRes *res = drmModeGetResources(drm_fd_);
    if (!res) {
        throw std::runtime_error("drmModeGetResources failed: " + std::string(strerror(errno)));
    }

    // 打印可用资源
    std::cout << "DRM Resources:"
              << "\n  Connectors: " << res->count_connectors
              << "\n  Encoders: " << res->count_encoders
              << "\n  CRTCs: " << res->count_crtcs
              << std::endl;

    // 查找第一个已连接的connector
    bool found = false;
    for (int i = 0; i < res->count_connectors && !found; i++) {
        drmModeConnector *conn = drmModeGetConnector(drm_fd_, res->connectors[i]);
        if (!conn) continue;

        std::cout << "Connector " << conn->connector_id
                  << " (type: " << conn->connector_type
                  << "): " << (conn->connection == DRM_MODE_CONNECTED ? "Connected" : "Disconnected")
                  << ", Modes: " << conn->count_modes
                  << std::endl;

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            conn_id_ = conn->connector_id;
            mode_ = conn->modes[0];  // 使用第一个支持的模式
            found = true;
            
            // 查找关联的CRTC
            if (conn->encoder_id) {
                drmModeEncoder *enc = drmModeGetEncoder(drm_fd_, conn->encoder_id);
                if (enc && enc->crtc_id) {
                    crtc_id_ = enc->crtc_id;
                }
                if (enc) drmModeFreeEncoder(enc);
            }
        }
        drmModeFreeConnector(conn);
    }

    // 如果没有找到活动的connector，尝试使用默认CRTC
    if (!found && res->count_crtcs > 0) {
        crtc_id_ = res->crtcs[0];
        std::cout << "Using fallback CRTC: " << crtc_id_ << std::endl;
    }

    drmModeFreeResources(res);

    if (crtc_id_ == 0) {
        throw std::runtime_error("No available CRTC found");
    }
}



void DrmDisplay::SetCrtc() {
    // 确保有有效的connector和mode
    if (conn_id_ == 0 || mode_.hdisplay == 0) {
        throw std::runtime_error("Invalid connector or mode");
    }

    // 创建临时dumb buffer
    CreateDumbBuffer();
    
    // 打印调试信息
    std::cout << "Setting CRTC:"
              << "\n  fd=" << drm_fd_
              << "\n  crtc_id=" << crtc_id_
              << "\n  fb_id=" << fb_id_
              << "\n  conn_id=" << conn_id_
              << "\n  mode=" << mode_.hdisplay << "x" << mode_.vdisplay
              << "@" << mode_.vrefresh << "Hz"
              << std::endl;

    // 尝试设置CRTC
    int ret = drmModeSetCrtc(drm_fd_, crtc_id_, fb_id_,
                            0, 0, &conn_id_, 1, &mode_);
    if (ret < 0) {
        std::cerr << "drmModeSetCrtc error: " << strerror(errno) << std::endl;
        throw std::runtime_error("drmModeSetCrtc failed: " + std::string(strerror(errno)));
    }
}

void DrmDisplay::DisplayFrame(uint8_t *yuv_data) {
   // Rockchip 特殊要求：NV12需要满足以下条件
    const int ALIGN_VALUE = 16;  // Rockchip通常需要16字节对齐
    const uint32_t y_pitch = ALIGN(width_, ALIGN_VALUE);
    const uint32_t uv_pitch = y_pitch;  // NV12 UV plane与Y同pitch
    const uint32_t y_size = y_pitch * ALIGN(height_, ALIGN_VALUE);
    const uint32_t uv_size = y_pitch * ALIGN(height_/2, ALIGN_VALUE);

    // 1. 创建Dumb Buffer
    struct drm_mode_create_dumb create = {};
    create.width = width_;
    create.height = height_ * 3 / 2;  // NV12总高度是1.5倍
    create.bpp = 8;
    
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        throw std::runtime_error("Create dumb failed: " + std::string(strerror(errno)));
    }

    // 2. 映射Buffer
    struct drm_mode_map_dumb map = { .handle = create.handle };
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        throw std::runtime_error("Map dumb failed");
    }

    void *drm_buffer = mmap(0, create.size, PROT_WRITE, MAP_SHARED, drm_fd_, map.offset);
    if (drm_buffer == MAP_FAILED) {
        drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        throw std::runtime_error("mmap failed");
    }

    // 3. 拷贝数据（考虑对齐）
    // Y平面
    for (int i = 0; i < height_; i++) {
        memcpy((uint8_t*)drm_buffer + i * y_pitch, 
               yuv_data + i * width_, 
               width_);
    }
    // UV平面
    for (int i = 0; i < height_/2; i++) {
        memcpy((uint8_t*)drm_buffer + y_size + i * uv_pitch,
               yuv_data + width_ * height_ + i * width_,
               width_);
    }

    // 4. 创建FB（关键修改）
    uint32_t handles[4] = {create.handle, create.handle};  // NV12需要两个plane
    uint32_t pitches[4] = {y_pitch, uv_pitch};
    uint32_t offsets[4] = {0, y_size};  // UV偏移量

    std::cout << "Creating FB with:\n"
              << "  width=" << width_ << "\n"
              << "  height=" << height_ << "\n"
              << "  format=" << format_ << "\n"
              << "  pitches=[" << pitches[0] << "," << pitches[1] << "]\n"
              << "  offsets=[" << offsets[0] << "," << offsets[1] << "]\n";

    if (drmModeAddFB2(drm_fd_, width_, height_, format_,
                     handles, pitches, offsets, &fb_id_, 0) < 0) {
        std::cerr << "AddFB2 error detail: " << strerror(errno) << std::endl;
        munmap(drm_buffer, create.size);
        drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        throw std::runtime_error("AddFB2 failed: Invalid argument");
    } 

    // 5. 显示到屏幕
    if (drmModeSetCrtc(drm_fd_, crtc_id_, fb_id_, 
                      0, 0, &conn_id_, 1, &mode_) < 0) {
        drmModeRmFB(drm_fd_, fb_id_);
        munmap(drm_buffer, create.size);
        throw std::runtime_error("SetCrtc failed");
    }

    // 6. 清理资源
    munmap(drm_buffer, create.size);
    if (prev_fb_id_ != 0) {
        drmModeRmFB(drm_fd_, prev_fb_id_);
    }
    prev_fb_id_ = fb_id_;
}

void DrmDisplay::CreateDumbBuffer() {
    struct drm_mode_create_dumb create = {
        .height = height_,
        .width = width_,
        .bpp = 32
    };

    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        throw std::runtime_error("Create dumb buffer failed: " + std::string(strerror(errno)));
    }

    // 打印buffer信息
    std::cout << "Created dumb buffer:"
              << "\n  Size: " << create.size
              << "\n  Pitch: " << create.pitch
              << "\n  Handle: " << create.handle
              << std::endl;

    // 创建framebuffer
    if (drmModeAddFB(drm_fd_, width_, height_, 24, 32,
                    create.pitch, create.handle, &fb_id_) < 0) {
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        throw std::runtime_error("AddFB failed: " + std::string(strerror(errno)));
    }
}

// 检查Rockchip是否支持NV12
bool DrmDisplay::CheckFormatSupport(uint32_t format) {
    drmModePlaneResPtr plane_res = drmModeGetPlaneResources(drm_fd_);
    if (!plane_res) return false;

    bool supported = false;
    for (uint32_t i = 0; i < plane_res->count_planes; i++) {
        drmModePlanePtr plane = drmModeGetPlane(drm_fd_, plane_res->planes[i]);
        if (!plane) continue;

        for (uint32_t j = 0; j < plane->count_formats; j++) {
            if (plane->formats[j] == format) {
                supported = true;
                break;
            }
        }
        drmModeFreePlane(plane);
        if (supported) break;
    }
    drmModeFreePlaneResources(plane_res);
    return supported;
}

// 析构函数：释放资源
DrmDisplay::~DrmDisplay() {
    if (fb_id_ != 0) {
        drmModeRmFB(drm_fd_, fb_id_);
    }
    if (drm_fd_ >= 0) {
        close(drm_fd_);
    }
}

