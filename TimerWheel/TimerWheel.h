#pragma once

#include <vector>
#include <sys/epoll.h>
#include <iostream>
#include "../Connection/Connection.h"

class TimerWheel
{
private:
    std::vector<int> wheel;
    int current_slot;
    Connection *connections;
    int timeout;

public:
    void init(int time, Connection *connection_arr);
    void add(int fd);
    void remove(int fd);
    void tick(int epoll_fd);
};
