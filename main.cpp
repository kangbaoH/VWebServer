// epoll + ThreadPool

#include <iostream>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <vector>
#include "ThreadPool.h"

#define MAX_EVENT_NUM 1024

int main()
{
    //create epoll
    int epoll_fd = epoll_create1(0);

    // create socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    // bind
    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    bind(listen_fd, (sockaddr *)&server_addr, sizeof(server_addr));

    // listen
    listen(listen_fd, 10);

    //set to non-blocking
    int flag = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flag | O_NONBLOCK);

    //add to epoll
        //create an epoll event
    epoll_event temp_event;
    temp_event.data.fd = listen_fd;
    temp_event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &temp_event);
    std::vector<epoll_event> events(MAX_EVENT_NUM);

    std::cout << "Server is listening..." << std::endl;

    // ThreadPool
    int notify_fd = eventfd(0, EFD_NONBLOCK);
    ThreadPool pool(8, notify_fd);

    epoll_event notify_event;
    notify_event.data.fd = notify_fd;
    notify_event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, notify_fd, &notify_event);

    for (;;)
    {
        int request_num = epoll_wait(epoll_fd, events.data(), MAX_EVENT_NUM, -1);

        for (int i = 0; i < request_num; i += 1)
        {
            int curr_fd = events[i].data.fd;

            if (curr_fd == listen_fd)
            {
                epoll_event new_connection_event;

                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int conn_fd = accept(listen_fd, (sockaddr *)&client_addr, &client_len);

                int flag = fcntl(conn_fd, F_GETFL, 0);
                fcntl(conn_fd, F_SETFL, flag | O_NONBLOCK);

                new_connection_event.data.fd = conn_fd;
                new_connection_event.events = EPOLLIN;

                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &new_connection_event);
                
                std::cout << "new client connected!" << std::endl;
            }
            else if (curr_fd == notify_fd)
            {
                uint64_t result_num;
                read(curr_fd, &result_num, sizeof(result_num));

                for (uint64_t i = 0; i < result_num; i += 1)
                {
                    std::unique_lock<std::mutex> lock(pool.result_mutex);
                    
                    if(pool.result_queue.empty())
                    {
                        lock.unlock();
                        break;
                    }

                    Result result = pool.result_queue.front();
                    pool.result_queue.pop();
                    lock.unlock();
                    send(result.fd, result.data.data(), result.data.length(), 0);

                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, result.fd, NULL);
                    close(result.fd);
                }
            }
            else if (events[i].events & EPOLLIN)
            {
                char buffer[64] = {0};
                ssize_t buf_stat = read(curr_fd, (void *)buffer, sizeof(buffer) - 1);

                if (buf_stat > 0)
                {
                    std::cout << "receive: " << buffer;
                    Task task(curr_fd, buffer);

                    pool.enqueue(task);  
                }
                else if (buf_stat == 0)
                {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL);
                    close(curr_fd);
                    std::cout << "Connection closed by foreign host." << std::endl;
                }
                else
                {
                    if (errno != EAGAIN &&errno != EWOULDBLOCK)
                    {
                        std::cout << "read error" << std::endl;
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL);
                        close(curr_fd);
                    }
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);

    return 0;
}