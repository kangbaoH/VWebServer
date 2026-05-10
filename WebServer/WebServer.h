#pragma once

#include <iostream>
#include <vector>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include "../ThreadPool/ThreadPool.h"
#include "../Connection/Connection.h"
#include "../TimerWheel/TimerWheel.h"
#include "../Logger/Logger.h"

#define MAX_EVENT_NUM 1024
#define MAX_CWD_LEN 200

class WebServer
{
private:
    std::vector<Connection> connections;
    int _max_connection_num;

    TimerWheel timer;

    ThreadPool pool;

    int epoll_fd;
    int listen_fd;
    int notify_fd;
    int timer_fd;

    epoll_event curr_event;

public:
    void epoll_init();
    void listen_init(int port);
    void threadpool_init(int thread_nums);
    void timer_init(int timeout);
    void logger_init(std::string dir, Level level, size_t file_size);

    void start(int port, int thread_nums, int timeout, int max_connection_num);

    void close_connection(int fd);

    void sign_epoll(int fd, int mode, uint32_t events);

    void handle_new_connection();

    void handle_threadpool_result();

    void handle_timer_tick();

    void handle_write();

    void handle_read();
};
