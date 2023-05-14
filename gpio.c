#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <sys/epoll.h>
#include "daemon.h"

#define GPIO_PIN "11" // set to the desired GPIO pin number
#define GPIO_LIGHT "6"

enum GPIO_DIRECTION {
	GPIO_DIRECTION_IN = 0,
	GPIO_DIRECTION_OUT = 1
};

enum GPIO_INT_EDGE_TYPE {
	GPIO_INT_EDGE_NONE,
	GPIO_INT_EDGE_FALLING,
	GPIO_INT_EDGE_RISING,
	GPIO_INT_EDGE_BOTH
};

enum GPIO_PIN_SWITCH {
	GPIO_PIN_ON = 0,
	GPIO_PIN_OFF = 1
};

const char * GPIO_DIRECTION_STR[] = {
	[GPIO_DIRECTION_IN] = "in",
	[GPIO_DIRECTION_OUT] = "out"
};

const char * GPIO_INT_EDGE_TYPE_STR[] = {
	[GPIO_INT_EDGE_NONE] = "none",
	[GPIO_INT_EDGE_FALLING] = "falling",
	[GPIO_INT_EDGE_RISING] = "rising",
	[GPIO_INT_EDGE_BOTH] = "both"
};

struct gpio {
	const char * gpio_pin;
	enum GPIO_DIRECTION gpio_direction;
	enum GPIO_INT_EDGE_TYPE gpio_int_edge_type;
	uint8_t gpio_value;
	int gpio_fd;
	int (*gpio_handler)(int epoll_fd, struct epoll_event *event)
};

static int pin_value = 0;
static struct timespec btn_pressed_timestamp, btn_released_timestamp;

static char * wifi_config_str = NULL;

static int capture_callback(void * img, unsigned long length, void * extra);
static int wifi_config();
static int wifi_config_capture_callback(void * img, unsigned long length, void * extra);

int init_gpio2(struct gpio * gpio_p) {
    // export the GPIO pin
    int export_fd = open("/sys/class/gpio/export", O_WRONLY);
    write(export_fd, gpio_p->gpio_pin, strlen(gpio_p->gpio_pin));
    close(export_fd);

    // set the pin direction as input
    char dir_path[50];
    memset(dir_path, 0, sizeof(dir_path));
    snprintf(dir_path, sizeof(dir_path), "/sys/class/gpio/gpio%s/direction", gpio_p->gpio_pin);
    int dir_fd = open(dir_path, O_WRONLY);
    write(dir_fd, gpio_p->gpio_direction, strlen(gpio_p->gpio_direction));
    close(dir_fd);

    if (gpio_p->gpio_direction == GPIO_DIRECTION_IN) {
	    // set the pin edge both
	    memset(dir_path, 0, sizeof(dir_path));
	    snprintf(dir_path, sizeof(dir_path), "/sys/class/gpio/gpio%s/edge", gpio_p->gpio_pin);
	    int edge_fd = open(dir_path, O_WRONLY);
	    write(edge_fd, gpio_p->gpio_int_edge_type, strlen(gpio_p->gpio_int_edge_type));
	    close(edge_fd);
    }


    // open the GPIO file descriptor
    int gpip_fd = -1;
    char val_path[50];
    snprintf(val_path, sizeof(val_path), "/sys/class/gpio/gpio%s/value", gpio_p->gpio_pin);

    if (gpio_p->gpio_direction == GPIO_DIRECTION_IN) {
	    gpio_p->gpio_fd = open(val_path, O_RDONLY);
    } else {
	    gpio_p->gpio_fd = open(val_path, O_WRONLY);
    }

    return 0;
}

int init_gpio(int epoll_fd) {
    int ret = 0;

    struct gpio gpio_btn = {
	    .gpio_pin = "11",
	    .gpio_direction = GPIO_DIRECTION_IN,
	    .gpio_int_edge_type = GPIO_INT_EDGE_BOTH,
            .gpio_handler = handle_gpio_btn
    };
    ret = init_gpio2(&gpio_btn);

    struct gpio gpio_light = {
	    .gpio_pin = "6",
	    .gpio_direction = GPIO_DIRECTION_OUT,
            .gpio_handler = handle_gpio_light
    };
    ret = init_gpio2(&gpio_light);

    event.data.fd = gpio_fd;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, gpio_fd, &event) < 0)
    {
            perror("Failed to add GPIO to epoll instance");
            exit(EXIT_FAILURE);
    }
}

int handle_gpio_btn(int epoll_fd, struct epoll_event *event)
{
	char buf[2] = "";
	int fd = event->data.fd;
	lseek(fd, 0, SEEK_SET);

	if (read(fd, buf, 1) == -1) {
            perror("read");
            exit(1);
        }
	buf[1] = '\0';

	int tmp_value = atoi((const char*)(buf));
	if (pin_value != tmp_value) {
		pin_value = tmp_value;
		printf("value is %d\n", pin_value);

	}

	if (GPIO_PIN_ON == pin_value) {
		// the button pressed down
		clock_gettime(CLOCK_MONOTONIC, &btn_pressed_timestamp);
	} else {
		clock_gettime(CLOCK_MONOTONIC, &btn_released_timestamp);

		double elapsed_sec = btn_released_timestamp.tv_sec - btn_pressed_timestamp.tv_sec;
		double elapsed_ms = (btn_released_timestamp.tv_nsec - btn_pressed_timestamp.tv_nsec) / 1000000.0;

		if (elapsed_sec >= 5 && elapsed_sec < 10) {
			printf("config mode\n");
			wifi_config();
		}
		else if (elapsed_ms >= 10 && elapsed_sec <5) {
			printf("capture\n");
			fswebcam_grab(capture_callback, NULL);
		}

	}
	return 0;
}

int handle_gpio_light(int epoll_fd, struct epoll_event *event) 
{
	return 0;
}

int capture_callback(void * img, unsigned long length, void * extra)
{
	FILE *f = fopen("image.jpg", "wb");
	if (!f) {
		perror("Open file failed");
	}
    	fwrite(img, 1, length, f);
    	fclose(f);
	return 0;
}

int wifi_config_capture_callback(void * img, unsigned long length, void * extra)
{
#if 0
	FILE *f = fopen("image.jpg", "wb");
	if (!f) {
		perror("Open file failed");
	}
    	fwrite(img, 1, length, f);
    	fclose(f);
#endif
	char * ret = qr_scan((const char *)img, length);
	if (ret != NULL) {
		wifi_config_str = ret;
	}
}

void parse_wifi_qr_string(char* qr_string, char* ssid, char* password, char* security)
{
    // Check if string starts with "WIFI:"
    if(strncmp(qr_string, "WIFI:", 5) != 0) {
        printf("Invalid WiFi QR code string\n");
        return;
    }

    // Move pointer to after "WIFI:"
    char* p = qr_string + 5;

    // Parse key-value pairs
    char* token;
    char* key;
    char* value;
    while ((token = strtok_r(p, ";", &p))) {
        key = strtok(token, ":");
        value = strtok(NULL, ":");
        if (!key || !value) {
            printf("Invalid WiFi QR code string\n");
            return;
        }
        if (strcmp(key, "T") == 0) {
		strncpy(security, value, 8);
        } else if (strcmp(key, "S") == 0) {
		strncpy(ssid, value, 64);
        } else if (strcmp(key, "P") == 0) {
		strncpy(password, value, 64);
        }
    }
}

int wifi_config()
{
	qr_scan_init();
	wifi_config_str = NULL;

	for (int i = 0; i < 20; i++) {
		if (wifi_config_str != NULL) {
			break;
		}

		printf("scan\n");
		fswebcam_grab(wifi_config_capture_callback, NULL);
	}
	qr_scan_uninit();

	// TODO Wifi config
	char ssid[64], password[64], security[8];	
	parse_wifi_qr_string(wifi_config_str, ssid, password, security);

	char cmd[256];	
	snprintf(cmd, sizeof(cmd), "nmcli device wifi connect '%s' password '%s'", ssid, password);
        int ret = system(cmd);
        if (ret == 0) {
            printf("Command executed successfully.\n");
        } else {
            printf("Command failed with error code: %d\n", ret);
        }
	
	wifi_config_str = NULL;
}
