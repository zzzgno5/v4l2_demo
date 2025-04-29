# v4l2_demo
Test v4l2 performance index parameters, etc.


# help
Usage: ./v4l2_capture [options]
Options:
  -d <device>    Video device (default: /dev/video0)
  -w <width>     Frame width (default: 640)
  -h <height>    Frame height (default: 480)
  -f <format>    Pixel format (YUYV, MJPG, NV12, NV21, YUV420)
  -o <file>      Output file (default: capture.dat)
  -n <count>     Number of frames to capture (default: 30)
  -l             List available formats and exit

Available formats:
  YUYV   - Single-planar
  MJPG   - Single-planar
  NV12   - Multi-planar
  NV21   - Multi-planar
  YUV420 - Multi-planar

