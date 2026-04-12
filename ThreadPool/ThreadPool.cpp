#include "ThreadPool.h"

ThreadPool::ThreadPool(int threads_num, int event_fd) : stop(0), notify_fd(event_fd)
{
    for (int i = 0; i < threads_num; i += 1)
    {
        workers.push_back(std::thread([this]()
                                      {                    
                    for (;;)
                    {
                        Connection *curr_task;

                        {
                            std::unique_lock<std::mutex> lock(queue_mutex);

                            condition.wait(lock, [this]()
                                           { return !task_queue.empty() || stop; });

                            if (task_queue.empty() && stop)
                                break;

                            curr_task = task_queue.front();
                            task_queue.pop();
                        }

                        /****************/
                        /* process data */
                        curr_task->process();
                        /****************/

                        {
                            std::unique_lock<std::mutex> result_lock(result_mutex);

                            result_queue.push(curr_task);
                        }

                        uint64_t one = 1;
                        write(notify_fd, &one, sizeof(one));
                    } }));
    }
}

void ThreadPool::enqueue(Connection* new_task)
{
    {    
        std::unique_lock<std::mutex> lock(queue_mutex);
        task_queue.push(new_task);
    }

    condition.notify_one();
}

ThreadPool::~ThreadPool()
{
    stop = 1;
    condition.notify_all();
    for (auto &worker : workers)
        worker.join();
}