#include "ThreadPool.h"

Task::Task(Connection *conn, int ver) : connection(conn), version(ver) {}

void ThreadPool::init(int threads_num, int event_fd)
{
    stop = 0;
    notify_fd = event_fd;
    for (int i = 0; i < threads_num; i += 1)
    {
        workers.push_back(std::thread([this]()
                                      {                    
                    for (;;)
                    {
                        Task curr_task(nullptr, 0);

                        {
                            std::unique_lock<std::mutex> lock(queue_mutex);

                            condition.wait(lock, [this]()
                                           { return !task_queue.empty() || stop; });

                            if (task_queue.empty() && stop)
                                break;

                            curr_task = task_queue.front();
                            task_queue.pop();
                        }

                        if (curr_task.connection->version() != curr_task.version)
                        {
                            continue;
                        }

                        // process data
                        HttpCode http_code = curr_task.connection->process_read();
                        switch (http_code)
                        {
                            case HttpCode::NO_REQUEST:
                                curr_task.connection->set_connection_state(ConnectionState::READ);
                                break;

                            case HttpCode::FILE_REQUEST:
                            case HttpCode::NO_RESOURCE:
                            case HttpCode::FORBIDDEN_REQUEST:
                            case HttpCode::INTERNAL_ERROR:
                            case HttpCode::BAD_REQUEST:
                                curr_task.connection->set_connection_state(ConnectionState::WRITE);
                                break;

                            default:
                                curr_task.connection->set_connection_state(ConnectionState::CLOSE);
                        }
                

                        {
                            std::unique_lock<std::mutex> result_lock(result_mutex);

                            result_queue.push(curr_task.connection);
                        }

                        uint64_t one = 1;
                        if ((write(notify_fd, &one, sizeof(one))) == -1)
                        {
                            if (errno != EAGAIN && errno != EWOULDBLOCK)
                            {
                                LOG_ERROR("Failed to write on notify_fd.");
                            }
                        }
                    } }));
    }
}

void ThreadPool::enqueue(Task new_task)
{
    {    
        std::unique_lock<std::mutex> lock(queue_mutex);
        task_queue.push(new_task);
    }

    condition.notify_one();
}

ThreadPool::~ThreadPool()
{
    stop = true;
    
    condition.notify_all();
    for (auto &worker : workers)
    {
        worker.join();
    }
    close(notify_fd);
}