#include "TimerWheel.h"

void TimerWheel::init(int time, Connection *connection_arr)
{
    current_slot = 0;
    timeout = time;
    wheel.resize(timeout, -1);
    connections = connection_arr;
}

void TimerWheel::add(int fd)
{
    int target_slot = (current_slot + timeout) % timeout;
    if (wheel[target_slot] == -1)
    {
        wheel[target_slot] = fd;
    }
    else
    {
        connections[fd].prev_in_timerwheel = -1;
        connections[fd].next_in_timerwheel = wheel[target_slot];
        connections[wheel[target_slot]].prev_in_timerwheel = fd;

        wheel[target_slot] = fd;
    }
    connections[fd].position_in_timerwheel = target_slot;
}

void TimerWheel::remove(int fd)
{
    if (connections[fd].position_in_timerwheel != -1 && 
            wheel[connections[fd].position_in_timerwheel] == fd)
    {
        wheel[connections[fd].position_in_timerwheel] =
            connections[fd].next_in_timerwheel;
    }

    if (connections[fd].prev_in_timerwheel != -1)
    {
        connections[connections[fd].prev_in_timerwheel].next_in_timerwheel =
            connections[fd].next_in_timerwheel;
    }
    if (connections[fd].next_in_timerwheel != -1)
    {
        connections[connections[fd].next_in_timerwheel].prev_in_timerwheel =
            connections[fd].prev_in_timerwheel;
    }
}

void TimerWheel::tick(int epoll_fd)
{
    current_slot = (current_slot + 1) % timeout;

    int fd = wheel[current_slot];
    while (fd != -1)
    {
        int next_fd = connections[fd].next_in_timerwheel;
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        std::cout << "Timeout, connection close. fd: " << fd << std::endl;
        fd = next_fd;
    }
    wheel[current_slot] = -1;
}
