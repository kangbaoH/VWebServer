#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <vector>
#include <sys/eventfd.h>
#include <unistd.h>

struct Task
{
    int fd;
    std::string data;

    Task(int task_fd, const std::string &task_data);
};

struct Result
{
    int fd;
    std::string data;

    Result(int task_fd, const std::string &task_data);
};

struct ThreadPool
{

    std::vector<std::thread> workers;
    std::condition_variable condition;
    std::queue<Task> task_queue;
    std::queue<Result> result_queue;
    std::mutex queue_mutex;
    std::mutex result_mutex;
    int notify_fd;
    bool stop;
    
    ThreadPool(int threads_num, int event_fd);
    void enqueue(Task new_task);
    ~ThreadPool();
};
