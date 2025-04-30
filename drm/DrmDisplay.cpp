#include "DrmDisplay.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <iostream>
#include <stdexcept>

#define ALIGN(value, align) (((value) + (align) - 1) & ~((align) - 1))
#define DRM_DEVICE "/dev/dri/card0"

// 如果系统没有drm_fourcc.h，手动定义常用格式
#ifndef fourcc_code
#define fourcc_code(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                               ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#endif

#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 fourcc_code('X', 'R', '2', '4')
#endif

DrmDisplay::DrmDisplay() {
    std::cout << "DrmDisplay constructor called" << std::endl;
    drm_fd_ = -1;
    width_ = height_ = format_ = 0;
    crtc_id_ = conn_id_ = fb_id_ = prev_fb_id_ = 0;
    dumb_handle_ = pitch_ = 0;
    mapped_ptr_ = nullptr;
    memset(&mode_, 0, sizeof(mode_));
}

DrmDisplay::~DrmDisplay() {
    std::cout << "DrmDisplay destructor called" << std::endl;
    if (mapped_ptr_) {
        std::cout << "Unmapping buffer: " << mapped_ptr_ << std::endl;
        munmap(mapped_ptr_, pitch_ * height_);
    }
    if (dumb_handle_) {
        std::cout << "Destroying dumb buffer, handle: " << dumb_handle_ << std::endl;
        struct drm_mode_destroy_dumb destroy = { .handle = dumb_handle_ };
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    if (fb_id_ != 0) {
        std::cout << "Removing framebuffer: " << fb_id_ << std::endl;
        drmModeRmFB(drm_fd_, fb_id_);
    }
    if (prev_fb_id_ != 0) {
        std::cout << "Removing previous framebuffer: " << prev_fb_id_ << std::endl;
        drmModeRmFB(drm_fd_, prev_fb_id_);
    }
    if (drm_fd_ >= 0) {
        std::cout << "Closing DRM device" << std::endl;
        close(drm_fd_);
    }
}

void DrmDisplay::Init(int width, int height, uint32_t format) {
    std::cout << "Initializing DRM display with format: " << format << std::endl;
    drm_fd_ = open(DRM_DEVICE, O_RDWR | O_CLOEXEC);
    if (drm_fd_ < 0) {
        std::cerr << "Failed to open DRM device: " << strerror(errno) << std::endl;
        throw std::runtime_error(std::string("Failed to open DRM device: ") + strerror(errno));
    }
    std::cout << "Opened DRM device, fd: " << drm_fd_ << std::endl;

    width_ = width;
    height_ = height;
    format_ = format;

    FindDisplayResources();
    
    if (!CheckFormatSupport(format)) {
        close(drm_fd_);
        drm_fd_ = -1;
        throw std::runtime_error("Requested format not supported");
    }

    SetCrtc();
}

void DrmDisplay::InitRed() {
    std::cout << "Initializing RED display mode" << std::endl;
    const uint32_t FORCE_FORMAT = DRM_FORMAT_XRGB8888;
    std::cout << "Using forced format: XRGB8888 (" << FORCE_FORMAT << ")" << std::endl;
    
    drm_fd_ = open(DRM_DEVICE, O_RDWR | O_CLOEXEC);
    if (drm_fd_ < 0) {
        std::cerr << "Failed to open DRM device: " << strerror(errno) << std::endl;
        throw std::runtime_error(std::string("Failed to open DRM device: ") + strerror(errno));
    }
    std::cout << "Opened DRM device, fd: " << drm_fd_ << std::endl;

    width_ = 1920;
    height_ = 1080;
    format_ = FORCE_FORMAT;

    FindDisplayResources();
    CreateRedBuffer();
    SetCrtc();
}

void DrmDisplay::CreateRedBuffer() {
    std::cout << "Creating red buffer..." << std::endl;
    
    // 1. 创建Dumb Buffer
    struct drm_mode_create_dumb create = {};
    create.width = width_;
    create.height = height_;
    create.bpp = 32;
    
    std::cout << "Creating dumb buffer: " << width_ << "x" << height_ << "@32bpp" << std::endl;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        std::cerr << "Create dumb buffer failed: " << strerror(errno) << std::endl;
        throw std::runtime_error(std::string("Create dumb buffer failed: ") + strerror(errno));
    }
    dumb_handle_ = create.handle;
    pitch_ = create.pitch;
    std::cout << "Dumb buffer created, handle: " << dumb_handle_ 
              << ", pitch: " << pitch_ << std::endl;

    // 2. 映射内存
    struct drm_mode_map_dumb map = { .handle = dumb_handle_ };
    std::cout << "Mapping dumb buffer..." << std::endl;
    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        std::cerr << "Map dumb buffer failed: " << strerror(errno) << std::endl;
        throw std::runtime_error(std::string("Map dumb buffer failed: ") + strerror(errno));
    }
    
    mapped_ptr_ = mmap(0, pitch_ * height_, PROT_READ | PROT_WRITE, 
                      MAP_SHARED, drm_fd_, map.offset);
    if (mapped_ptr_ == MAP_FAILED) {
        std::cerr << "mmap failed: " << strerror(errno) << std::endl;
        throw std::runtime_error(std::string("mmap failed: ") + strerror(errno));
    }
    std::cout << "Buffer mapped at: " << mapped_ptr_ << std::endl;

    // 3. 填充纯红色
    std::cout << "Filling buffer with red color..." << std::endl;
    uint32_t* pixels = (uint32_t*)mapped_ptr_;
    const uint32_t red = 0xFFFF0000;
    for (uint32_t i = 0; i < width_ * height_; i++) {
        pixels[i] = red;
    }
    std::cout << "Buffer filled with red color" << std::endl;

    // 4. 注册FrameBuffer
    std::cout << "Adding framebuffer..." << std::endl;
    if (drmModeAddFB(drm_fd_, width_, height_, 24, 32, pitch_, 
                    dumb_handle_, &fb_id_) < 0) {
        std::cerr << "drmModeAddFB failed: " << strerror(errno) << std::endl;
        throw std::runtime_error(std::string("drmModeAddFB failed: ") + strerror(errno));
    }
    std::cout << "Framebuffer created, ID: " << fb_id_ << std::endl;
}

void DrmDisplay::DisplayRed() {
    std::cout << "DisplayRed() called" << std::endl;
    if (!mapped_ptr_) {
        std::cerr << "Error: Display not initialized in RED mode" << std::endl;
        throw std::runtime_error("Display not initialized in RED mode");
    }

    std::cout << "Setting CRTC with fb_id: " << fb_id_ 
              << ", crtc_id: " << crtc_id_ 
              << ", conn_id: " << conn_id_ << std::endl;
              
    if (drmModeSetCrtc(drm_fd_, crtc_id_, fb_id_, 
                      0, 0, &conn_id_, 1, &mode_) < 0) {
        std::cerr << "drmModeSetCrtc failed: " << strerror(errno) << std::endl;
        throw std::runtime_error(std::string("drmModeSetCrtc failed: ") + strerror(errno));
    }
    std::cout << "CRTC set successfully" << std::endl;
}

void DrmDisplay::FindDisplayResources() {
    std::cout << "Finding display resources..." << std::endl;
    drmModeRes *res = drmModeGetResources(drm_fd_);
    if (!res) {
        std::cerr << "drmModeGetResources failed: " << strerror(errno) << std::endl;
        throw std::runtime_error("drmModeGetResources failed: " + std::string(strerror(errno)));
    }

    std::cout << "Available resources:\n"
              << "  Connectors: " << res->count_connectors << "\n"
              << "  Encoders: " << res->count_encoders << "\n"
              << "  CRTCs: " << res->count_crtcs << std::endl;

    bool found = false;
    for (int i = 0; i < res->count_connectors && !found; i++) {
        drmModeConnector *conn = drmModeGetConnector(drm_fd_, res->connectors[i]);
        if (!conn) continue;

        std::cout << "Checking connector " << conn->connector_id 
                  << " (type: " << conn->connector_type
                  << "), status: " 
                  << (conn->connection == DRM_MODE_CONNECTED ? "Connected" : "Disconnected")
                  << ", modes: " << conn->count_modes << std::endl;

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            conn_id_ = conn->connector_id;
            mode_ = conn->modes[0];
            found = true;
            std::cout << "Using connector " << conn_id_ 
                      << " with mode: " << mode_.hdisplay 
                      << "x" << mode_.vdisplay 
                      << "@" << mode_.vrefresh << "Hz" << std::endl;
            
            if (conn->encoder_id) {
                drmModeEncoder *enc = drmModeGetEncoder(drm_fd_, conn->encoder_id);
                if (enc && enc->crtc_id) {
                    crtc_id_ = enc->crtc_id;
                    std::cout << "Found associated CRTC: " << crtc_id_ << std::endl;
                }
                if (enc) drmModeFreeEncoder(enc);
            }
        }
        drmModeFreeConnector(conn);
    }

    if (!found && res->count_crtcs > 0) {
        crtc_id_ = res->crtcs[0];
        std::cout << "Using fallback CRTC: " << crtc_id_ << std::endl;
    }

    drmModeFreeResources(res);

    if (crtc_id_ == 0) {
        std::cerr << "Error: No available CRTC found" << std::endl;
        throw std::runtime_error("No available CRTC found");
    }
}

void DrmDisplay::SetCrtc() {
    std::cout << "Setting CRTC..." << std::endl;
    CreateDumbBuffer();
    
    std::cout << "Calling drmModeSetCrtc with:\n"
              << "  fd: " << drm_fd_ << "\n"
              << "  crtc_id: " << crtc_id_ << "\n"
              << "  fb_id: " << fb_id_ << "\n"
              << "  conn_id: " << conn_id_ << "\n"
              << "  mode: " << mode_.hdisplay << "x" << mode_.vdisplay << std::endl;
              
    int ret = drmModeSetCrtc(drm_fd_, crtc_id_, fb_id_,
                            0, 0, &conn_id_, 1, &mode_);
    if (ret < 0) {
        std::cerr << "drmModeSetCrtc error: " << strerror(errno) << std::endl;
        throw std::runtime_error("drmModeSetCrtc failed: " + std::string(strerror(errno)));
    }
    std::cout << "CRTC set successfully" << std::endl;
}

void DrmDisplay::CreateDumbBuffer() {
    std::cout << "Creating temporary dumb buffer..." << std::endl;
    struct drm_mode_create_dumb create = {
        .height = height_,
        .width = width_,
        .bpp = 32
    };

    if (drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        std::cerr << "Create dumb buffer failed: " << strerror(errno) << std::endl;
        throw std::runtime_error("Create dumb buffer failed: " + std::string(strerror(errno)));
    }

    std::cout << "Dumb buffer created, size: " << create.size 
              << ", pitch: " << create.pitch 
              << ", handle: " << create.handle << std::endl;

    if (drmModeAddFB(drm_fd_, width_, height_, 24, 32,
                    create.pitch, create.handle, &fb_id_) < 0) {
        std::cerr << "AddFB failed: " << strerror(errno) << std::endl;
        struct drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        throw std::runtime_error("AddFB failed: " + std::string(strerror(errno)));
    }
    std::cout << "Framebuffer created, ID: " << fb_id_ << std::endl;
}

bool DrmDisplay::CheckFormatSupport(uint32_t format) {
    std::cout << "Checking format support for: " << format << std::endl;
    drmModePlaneResPtr plane_res = drmModeGetPlaneResources(drm_fd_);
    if (!plane_res) {
        std::cerr << "drmModeGetPlaneResources failed" << std::endl;
        return false;
    }

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
    
    std::cout << "Format " << format << " is " 
              << (supported ? "supported" : "NOT supported") << std::endl;
    return supported;
}

void DrmDisplay::DisplayFrame(uint8_t *yuv_data) {
    // Rockchip 特殊要求：NV12需要满足对齐
    const int ALIGN_VALUE = 16;
    const uint32_t y_pitch = ALIGN(width_, ALIGN_VALUE);
    const uint32_t uv_pitch = y_pitch;
    const uint32_t y_size = y_pitch * ALIGN(height_, ALIGN_VALUE);
    const uint32_t uv_size = y_pitch * ALIGN(height_/2, ALIGN_VALUE);

    // 1. 创建Dumb Buffer
    struct drm_mode_create_dumb create = {};
    create.width = width_;
    create.height = height_ * 3 / 2;
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

    // 4. 创建FB
    uint32_t handles[4] = {create.handle, create.handle};
    uint32_t pitches[4] = {y_pitch, uv_pitch};
    uint32_t offsets[4] = {0, y_size};

    if (drmModeAddFB2(drm_fd_, width_, height_, format_,
                     handles, pitches, offsets, &fb_id_, 0) < 0) {
        munmap(drm_buffer, create.size);
        drm_mode_destroy_dumb destroy = { .handle = create.handle };
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        throw std::runtime_error("AddFB2 failed");
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

