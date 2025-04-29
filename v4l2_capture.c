#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define DEVICE_NAME "/dev/video0"
#define OUTPUT_FILE "capture.dat"
#define FRAME_COUNT 30
#define VIDEO_MAX_PLANES 4

struct buffer {
    void *start[VIDEO_MAX_PLANES];
    size_t length[VIDEO_MAX_PLANES];
    int n_planes;
};

static int fd = -1;
static struct buffer *buffers = NULL;
static unsigned int n_buffers = 0;
static FILE *output_file = NULL;
static int is_multiplanar = 0;
static unsigned int pixel_format = V4L2_PIX_FMT_YUYV;

// 支持的格式列表
struct format_entry {
    const char *name;
    unsigned int fourcc;
    int is_multiplanar;
};

const struct format_entry supported_formats[] = {
    {"YUYV", V4L2_PIX_FMT_YUYV, 0},
    {"MJPG", V4L2_PIX_FMT_MJPEG, 0},
    {"NV12", V4L2_PIX_FMT_NV12, 1},
    {"NV21", V4L2_PIX_FMT_NV21, 1},
    {"YUV420", V4L2_PIX_FMT_YUV420, 1},
    {NULL, 0, 0}
};

void errno_exit(const char *s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

int xioctl(int fh, int request, void *arg) {
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

void print_capabilities(struct v4l2_capability *cap) {
    printf("Driver: %s\n", cap->driver);
    printf("Card: %s\n", cap->card);
    printf("Bus info: %s\n", cap->bus_info);
    printf("Version: %u.%u.%u\n", 
           (cap->version >> 16) & 0xFF,
           (cap->version >> 8) & 0xFF,
           cap->version & 0xFF);
    printf("Capabilities: 0x%08X\n", cap->capabilities);
    
    if (cap->capabilities & V4L2_CAP_VIDEO_CAPTURE)
        printf("  Video capture\n");
    if (cap->capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        printf("  Multi-planar video capture\n");
    if (cap->capabilities & V4L2_CAP_STREAMING)
        printf("  Streaming\n");
}

void print_format(struct v4l2_format *fmt) {
    if (is_multiplanar) {
        struct v4l2_pix_format_mplane *pix_mp = &fmt->fmt.pix_mp;
        printf("Multi-planar format:\n");
        printf("  Width: %u\n", pix_mp->width);
        printf("  Height: %u\n", pix_mp->height);
        printf("  Pixel Format: %c%c%c%c\n",
               pix_mp->pixelformat & 0xFF,
               (pix_mp->pixelformat >> 8) & 0xFF,
               (pix_mp->pixelformat >> 16) & 0xFF,
               (pix_mp->pixelformat >> 24) & 0xFF);
        printf("  Field: %u\n", pix_mp->field);
        printf("  Number of planes: %u\n", pix_mp->num_planes);
        for (int i = 0; i < pix_mp->num_planes; i++) {
            printf("  Plane %d: sizeimage=%u bytesperline=%u\n",
                   i, pix_mp->plane_fmt[i].sizeimage,
                   pix_mp->plane_fmt[i].bytesperline);
        }
    } else {
        struct v4l2_pix_format *pix = &fmt->fmt.pix;
        printf("Single-planar format:\n");
        printf("  Width: %u\n", pix->width);
        printf("  Height: %u\n", pix->height);
        printf("  Pixel Format: %c%c%c%c\n",
               pix->pixelformat & 0xFF,
               (pix->pixelformat >> 8) & 0xFF,
               (pix->pixelformat >> 16) & 0xFF,
               (pix->pixelformat >> 24) & 0xFF);
        printf("  Field: %u\n", pix->field);
        printf("  Sizeimage: %u\n", pix->sizeimage);
        printf("  Bytesperline: %u\n", pix->bytesperline);
    }
}

void print_formats() {
    struct v4l2_fmtdesc fmt;
    CLEAR(fmt);
    
    printf("\nSupported single-planar formats:\n");
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        printf("  %d: %s (0x%08X)\n", fmt.index, fmt.description, fmt.pixelformat);
        fmt.index++;
    }
    
    printf("\nSupported multi-planar formats:\n");
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        printf("  %d: %s (0x%08X)\n", fmt.index, fmt.description, fmt.pixelformat);
        fmt.index++;
    }
}

void print_help(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d <device>    Video device (default: /dev/video0)\n");
    printf("  -w <width>     Frame width (default: 640)\n");
    printf("  -h <height>    Frame height (default: 480)\n");
    printf("  -f <format>    Pixel format (YUYV, MJPG, NV12, NV21, YUV420)\n");
    printf("  -o <file>      Output file (default: capture.dat)\n");
    printf("  -n <count>     Number of frames to capture (default: 30)\n");
    printf("  -l             List available formats and exit\n");
    printf("\nAvailable formats:\n");
    
    const struct format_entry *fmt = supported_formats;
    while (fmt->name != NULL) {
        printf("  %-6s - %s\n", fmt->name, 
               fmt->is_multiplanar ? "Multi-planar" : "Single-planar");
        fmt++;
    }
}

int set_format_by_name(const char *name) {
    const struct format_entry *fmt = supported_formats;
    while (fmt->name != NULL) {
        if (strcasecmp(name, fmt->name) == 0) {
            pixel_format = fmt->fourcc;
            is_multiplanar = fmt->is_multiplanar;
            return 1;
        }
        fmt++;
    }
    return 0;
}

void init_device(int width, int height) {
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    
    // Query device capabilities
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", DEVICE_NAME);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_QUERYCAP");
        }
    }
    
    print_capabilities(&cap);
    
    // Check if requested format matches device capabilities
    if (is_multiplanar && !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        fprintf(stderr, "Device does not support multi-planar capture\n");
        exit(EXIT_FAILURE);
    }
    
    // Set format
    CLEAR(fmt);
    if (is_multiplanar) {
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = pixel_format;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        
        if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
            fprintf(stderr, "Failed to set format '%c%c%c%c'\n",
                   pixel_format & 0xFF,
                   (pixel_format >> 8) & 0xFF,
                   (pixel_format >> 16) & 0xFF,
                   (pixel_format >> 24) & 0xFF);
            errno_exit("VIDIOC_S_FMT (MPLANE)");
        }
        
        // Update actual format settings
        pixel_format = fmt.fmt.pix_mp.pixelformat;
    } else {
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = pixel_format;
        fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
        
        if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
            fprintf(stderr, "Failed to set format '%c%c%c%c'\n",
                   pixel_format & 0xFF,
                   (pixel_format >> 8) & 0xFF,
                   (pixel_format >> 16) & 0xFF,
                   (pixel_format >> 24) & 0xFF);
            errno_exit("VIDIOC_S_FMT");
        }
        
        // Update actual format settings
        pixel_format = fmt.fmt.pix.pixelformat;
    }
    
    printf("\nCurrent format settings:\n");
    print_format(&fmt);
    
    // Request buffers
    struct v4l2_requestbuffers req;
    CLEAR(req);
    req.type = is_multiplanar ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = 4;
    
    if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", DEVICE_NAME);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_REQBUFS");
        }
    }
    
    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", DEVICE_NAME);
        exit(EXIT_FAILURE);
    }
    
    buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }
    
    // Map buffers
    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        if (is_multiplanar) {
            struct v4l2_plane planes[VIDEO_MAX_PLANES];
            struct v4l2_buffer buf;
            CLEAR(buf);
            CLEAR(planes);
            
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = n_buffers;
            buf.m.planes = planes;
            buf.length = VIDEO_MAX_PLANES;
            
            if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1)
                errno_exit("VIDIOC_QUERYBUF (MPLANE)");
            
            buffers[n_buffers].n_planes = buf.length;
            for (int i = 0; i < buf.length; i++) {
                buffers[n_buffers].length[i] = buf.m.planes[i].length;
                buffers[n_buffers].start[i] = mmap(NULL, buf.m.planes[i].length,
                                                  PROT_READ | PROT_WRITE, MAP_SHARED,
                                                  fd, buf.m.planes[i].m.mem_offset);
                if (buffers[n_buffers].start[i] == MAP_FAILED)
                    errno_exit("mmap (plane)");
            }
        } else {
            struct v4l2_buffer buf;
            CLEAR(buf);
            
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = n_buffers;
            
            if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1)
                errno_exit("VIDIOC_QUERYBUF");
            
            buffers[n_buffers].n_planes = 1;
            buffers[n_buffers].length[0] = buf.length;
            buffers[n_buffers].start[0] = mmap(NULL, buf.length,
                                             PROT_READ | PROT_WRITE, MAP_SHARED,
                                             fd, buf.m.offset);
            if (buffers[n_buffers].start[0] == MAP_FAILED)
                errno_exit("mmap");
        }
    }
}

void start_capturing() {
    unsigned int i;
    enum v4l2_buf_type type;
    
    for (i = 0; i < n_buffers; ++i) {
        if (is_multiplanar) {
            struct v4l2_plane planes[VIDEO_MAX_PLANES];
            struct v4l2_buffer buf;
            CLEAR(buf);
            CLEAR(planes);
            
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            buf.m.planes = planes;
            buf.length = buffers[i].n_planes;
            
            if (xioctl(fd, VIDIOC_QBUF, &buf) == -1)
                errno_exit("VIDIOC_QBUF (MPLANE)");
        } else {
            struct v4l2_buffer buf;
            CLEAR(buf);
            
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            
            if (xioctl(fd, VIDIOC_QBUF, &buf) == -1)
                errno_exit("VIDIOC_QBUF");
        }
    }
    
    type = is_multiplanar ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) == -1)
        errno_exit("VIDIOC_STREAMON");
}

void stop_capturing() {
    enum v4l2_buf_type type;
    type = is_multiplanar ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) == -1)
        errno_exit("VIDIOC_STREAMOFF");
}

void uninit_device() {
    unsigned int i;
    
    for (i = 0; i < n_buffers; ++i) {
        for (int j = 0; j < buffers[i].n_planes; j++) {
            if (munmap(buffers[i].start[j], buffers[i].length[j]) == -1)
                errno_exit("munmap");
        }
    }
    
    free(buffers);
}

void process_image(struct buffer *buf) {
    static int frame_count = 0;
    
    if (output_file) {
        printf("Captured frame %d\n", ++frame_count);
        
        // 写入所有平面数据
        for (int i = 0; i < buf->n_planes; i++) {
            fwrite(buf->start[i], buf->length[i], 1, output_file);
        }
    }
}

void mainloop(int frame_count) {
    while (frame_count-- > 0) {
        fd_set fds;
        struct timeval tv;
        int r;
        
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        r = select(fd + 1, &fds, NULL, NULL, &tv);
        
        if (r == -1) {
            if (errno == EINTR)
                continue;
            errno_exit("select");
        }
        
        if (r == 0) {
            fprintf(stderr, "select timeout\n");
            exit(EXIT_FAILURE);
        }
        
        if (is_multiplanar) {
            struct v4l2_plane planes[VIDEO_MAX_PLANES];
            struct v4l2_buffer buf;
            CLEAR(buf);
            CLEAR(planes);
            
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.m.planes = planes;
            buf.length = VIDEO_MAX_PLANES;
            
            if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1)
                errno_exit("VIDIOC_DQBUF (MPLANE)");
            
            process_image(&buffers[buf.index]);
            
            if (xioctl(fd, VIDIOC_QBUF, &buf) == -1)
                errno_exit("VIDIOC_QBUF (MPLANE)");
        } else {
            struct v4l2_buffer buf;
            CLEAR(buf);
            
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            
            if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1)
                errno_exit("VIDIOC_DQBUF");
            
            process_image(&buffers[buf.index]);
            
            if (xioctl(fd, VIDIOC_QBUF, &buf) == -1)
                errno_exit("VIDIOC_QBUF");
        }
    }
}

int main(int argc, char **argv) {
    const char *dev_name = DEVICE_NAME;
    const char *out_file = OUTPUT_FILE;
    int width = 640;
    int height = 480;
    int frame_count = FRAME_COUNT;
    int list_formats = 0;
    
    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "d:w:h:f:o:n:l")) != -1) {
        switch (opt) {
        case 'd':
            dev_name = optarg;
            break;
        case 'w':
            width = atoi(optarg);
            break;
        case 'h':
            height = atoi(optarg);
            break;
        case 'f':
            if (!set_format_by_name(optarg)) {
                fprintf(stderr, "Unsupported format: %s\n", optarg);
                print_help(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;
        case 'o':
            out_file = optarg;
            break;
        case 'n':
            frame_count = atoi(optarg);
            break;
        case 'l':
            list_formats = 1;
            break;
        default:
            print_help(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    
    printf("Opening video device: %s\n", dev_name);
    fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n",
                dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    if (list_formats) {
        print_formats();
        close(fd);
        return 0;
    }
    
    output_file = fopen(out_file, "wb");
    if (!output_file) {
        fprintf(stderr, "Cannot open '%s'\n", out_file);
        exit(EXIT_FAILURE);
    }
    
    init_device(width, height);
    start_capturing();
    mainloop(frame_count);
    stop_capturing();
    uninit_device();
    
    fclose(output_file);
    close(fd);
    
    printf("\nCapture completed. Saved %d frames to %s\n", frame_count, out_file);
    return 0;
}

