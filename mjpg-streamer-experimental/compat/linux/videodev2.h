#ifndef _COMPAT_LINUX_VIDEODEV2_H
#define _COMPAT_LINUX_VIDEODEV2_H
/* Minimal V4L2 compatibility shim for non-Linux platforms (e.g., Haiku) */
#include <linux/types.h>

#define V4L2_CTRL_CLASS_USER  0x00980000
#define V4L2_CTRL_TYPE_INTEGER    1
#define V4L2_CTRL_TYPE_BOOLEAN    2
#define V4L2_CTRL_TYPE_MENU       3
#define V4L2_CTRL_TYPE_BUTTON     4
#define V4L2_CTRL_TYPE_INTEGER64  5
#define V4L2_CTRL_TYPE_CTRL_CLASS 6
#define V4L2_CTRL_TYPE_STRING     7

#define V4L2_CTRL_FLAG_DISABLED   0x0001
#define V4L2_CTRL_FLAG_GRABBED    0x0002
#define V4L2_CTRL_FLAG_READ_ONLY  0x0004
#define V4L2_CTRL_FLAG_UPDATE     0x0008
#define V4L2_CTRL_FLAG_INACTIVE   0x0010
#define V4L2_CTRL_FLAG_SLIDER     0x0020
#define V4L2_CTRL_FLAG_WRITE_ONLY 0x0040

#define V4L2_BUF_TYPE_VIDEO_CAPTURE 1

#define V4L2_PIX_FMT_MJPEG  0x47504a4d
#define V4L2_PIX_FMT_JPEG   0x4745504a
#define V4L2_PIX_FMT_YUYV   0x56595559
#define V4L2_PIX_FMT_RGB24  0x33424752

struct v4l2_queryctrl {
    __u32 id;
    __u32 type;
    __u8  name[32];
    __s32 minimum;
    __s32 maximum;
    __s32 step;
    __s32 default_value;
    __u32 flags;
    __u32 reserved[2];
};

struct v4l2_querymenu {
    __u32 id;
    __u32 index;
    union {
        __u8  name[32];
        __s64 value;
    };
    __u32 reserved;
} __attribute__((packed));

struct v4l2_fmtdesc {
    __u32 index;
    __u32 type;
    __u32 flags;
    __u8  description[32];
    __u32 pixelformat;
    __u32 reserved[4];
};

struct v4l2_jpegcompression {
    int quality;
    int  APPn;
    int  APP_len;
    char APP_data[60];
    int  COM_len;
    char COM_data[60];
    __u32 jpeg_markers;
};

#endif /* _COMPAT_LINUX_VIDEODEV2_H */
