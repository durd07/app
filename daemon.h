#pragma once

#include <sys/epoll.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "fswebcam/fswebcam_export.h"

extern int init_gpio(int epoll_fd);
extern int handle_gpio_btn(int epoll_fd, struct epoll_event *event);
extern int handle_gpio_light(int epoll_fd, struct epoll_event *event);

extern int qr_scan_init();
extern char * qr_scan(const char * img, int length);
extern int qr_scan_uninit();

#ifdef __cplusplus
}
#endif
