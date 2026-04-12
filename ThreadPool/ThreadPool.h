#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <vector>
#include <sys/eventfd.h>
#include <unistd.h>
#include "../Connection/Connection.h"

struct ThreadPool
{

    std::vector<std::thread> workers;
    std::condition_variable condition;
    std::queue<Connection*> task_queue;
    std::queue<Connection*> result_queue;
    std::mutex queue_mutex;
    std::mutex result_mutex;
    int notify_fd;
    bool stop;
    
    ThreadPool(int threads_num, int event_fd);
    void enqueue(Connection* new_task);
    ~ThreadPool();
};
