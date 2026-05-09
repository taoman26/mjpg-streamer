#ifndef _COMPAT_LINUX_VERSION_H
#define _COMPAT_LINUX_VERSION_H
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#define LINUX_VERSION_CODE KERNEL_VERSION(3,0,0)
#endif
