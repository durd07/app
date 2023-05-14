#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "daemon.h"

#define SERVER_PORT 6666
#define EPOLL_MAX_EVENTS 10
#define MAX_MESSAGE_SIZE 8192
#define MESSAGE_QUEUE_NAME "/test_queue"

#define MAX_MSG_SIZE 1024
#define MAX_MSGQ_SIZE 10

void * img_data = NULL;

void make_socket_non_blocking(int sockfd)
{
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1)
	{
		perror("fcntl F_GETFL");
		exit(EXIT_FAILURE);
	}

	if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		perror("fcntl F_SETFL O_NONBLOCK");
		exit(EXIT_FAILURE);
	}
}

int init_socket()
{
	int sock_fd;
	struct sockaddr_in server_addr;

	// Create TCP socket
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0)
	{
		perror("Failed to create socket");
		exit(EXIT_FAILURE);
	}

	// Set socket non-blocking
	make_socket_non_blocking(sock_fd);

	// Set socket options
	int enable = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
	{
		perror("setsockopt(SO_REUSEADDR) failed");
		exit(EXIT_FAILURE);
	}

	// Bind socket to local address
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(8080);
	if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		perror("Failed to bind socket");
		exit(EXIT_FAILURE);
	}

	// Listen for incoming connections
	if (listen(sock_fd, SOMAXCONN) < 0)
	{
		perror("Failed to listen on socket");
		exit(EXIT_FAILURE);
	}

	return sock_fd;
}

#if 0
int init_message_queue()
{
	// Create message queue
	struct mq_attr attr;
	attr.mq_maxmsg = MAX_MSGQ_SIZE;
	attr.mq_msgsize = MAX_MSG_SIZE;
	int mq_fd = mq_open(MESSAGE_QUEUE_NAME, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR, &attr);
	if (mq_fd < 0)
	{
		perror("Failed to create message queue");
		exit(EXIT_FAILURE);
	}

	return mq_fd;
}
#endif

int socket_handle_grab_cb(void * img, unsigned long length, void * extra)
{
	int conn_fd = *(int *)extra;
	printf("send back %d\n", length);
	int len = htonl(length);
	if (send(conn_fd, &len, sizeof(len), 0) < 0)
	{
		perror("send");
		return -1;
	}

	int sent = 0;
	while (sent < length) {
	    ssize_t n;

	    if ((n = send(conn_fd, img + sent, length - sent, 0)) < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
		    continue;
		} else {
		    perror("send failed");
		    break;
		}
	    }
	    
	    sent += n;
	}
}

int handle_socket_event(int epollfd, struct epoll_event *event)
{
	int conn_fd = event->data.fd;
	if (event->events & EPOLLIN)
	{
		// Receive data
		char buf[MAX_MSG_SIZE];
		int n = recv(conn_fd, buf, MAX_MSG_SIZE, 0);
		if (n < 0)
		{
			if (errno != EWOULDBLOCK && errno != EAGAIN)
			{
				perror("recv");
				return -1;
			}
		}
		else if (n == 0)
		{
			printf("Connection closed by client\n");
			close(conn_fd);
			epoll_ctl(epollfd, EPOLL_CTL_DEL, conn_fd, event);
		}
		else
		{
			fswebcam_grab(socket_handle_grab_cb, &conn_fd);
		}
	}
	else if (event->events & (EPOLLHUP | EPOLLERR))
	{
		// Error or hangup
		printf("Error or hangup on socket %d\n", conn_fd);
		close(conn_fd);
		epoll_ctl(epollfd, EPOLL_CTL_DEL, conn_fd, event);
	}
	return 0;
}

int handle_socket_accept(int epoll_fd, struct epoll_event *event)
{
	int listen_fd = event->data.fd;
	struct sockaddr_in peer_addr;

	socklen_t peer_addr_len = sizeof(peer_addr);
	int client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_addr_len);
	if (client_fd < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			// This can happen due to the nonblocking socket mode; in this
			// case don't do anything, but print a notice (since these events
			// are extremely rare and interesting to observe...)
			printf("accept returned EAGAIN or EWOULDBLOCK\n");
		}
		else
		{
			perror("accept");
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		make_socket_non_blocking(client_fd);

		struct epoll_event event = { 0 };
		event.data.fd = client_fd;
		event.events = EPOLLIN;

		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0)
		{
			perror("epoll_ctl EPOLL_CTL_ADD");
			exit(EXIT_FAILURE);
		}
	}
}

int main(int argc, char **argv)
{
	fswebcam_init();

	struct epoll_event event, events[EPOLL_MAX_EVENTS];
	char buffer[MAX_MESSAGE_SIZE];

	int sock_fd = init_socket();
	//int mq_fd = init_message_queue();
	int gpio_fd = init_gpio_btn();

	// Create epoll instance
	int epoll_fd = epoll_create1(0);
	if (epoll_fd < 0)
	{
		perror("Failed to create epoll instance");
		exit(EXIT_FAILURE);
	}

	// Register TCP socket with epoll
	event.data.fd = sock_fd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event) < 0)
	{
		perror("Failed to add socket to epoll instance");
		exit(EXIT_FAILURE);
	}
#if 0
	// Register message queue with epoll
	event.data.fd = mq_fd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, mq_fd, &event) < 0)
	{
		perror("Failed to add message queue to epoll instance");
		exit(EXIT_FAILURE);
	}
#endif
	// Register GPIO
	event.data.fd = gpio_fd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, gpio_fd, &event) < 0)
	{
		perror("Failed to add GPIO to epoll instance");
		exit(EXIT_FAILURE);
	}

	// Main loop
	int i = 0;
	int ret = 0;
	while (1)
	{
		ret = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1);
		if (ret < 0)
		{
			perror("Failed to wait for events");
			exit(EXIT_FAILURE);
		}

		// Handle events
		for (i = 0; i < ret; i++)
		{
#if 0
			if (events[i].events & EPOLLERR)
			{
				perror("epoll_wait returned EPOLLERR");
				exit(EXIT_FAILURE);
			}
#endif

			if (events[i].data.fd == sock_fd)
			{
				handle_socket_accept(epoll_fd, &(events[i]));
			}

			else if (events[i].data.fd == gpio_fd)
			{
				handle_gpio_btn(epoll_fd, &(events[i]));
			}

			else if (events[i].events & EPOLLIN)
			{
				handle_socket_event(epoll_fd, &(events[i]));
			}
			else {
				perror("EPOLL ERROR");
				exit(EXIT_FAILURE);
			}
		}
	}
}
