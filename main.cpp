// epoll + ThreadPool

#include <iostream>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <vector>
#include <sys/timerfd.h>
#include "./ThreadPool/ThreadPool.h"
#include "./Connection/Connection.h"
#include "./TimerWheel/TimerWheel.h"

#define MAX_EVENT_NUM 1024

int main()
{
    std::vector<Connection> connections(65535);
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
    listen(listen_fd, 1024);

    //set to non-blocking
    int old_flag = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, old_flag | O_NONBLOCK);

    //add to epoll
        //create an epoll event
    epoll_event listen_event;
    listen_event.data.fd = listen_fd;
    listen_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &listen_event);
    std::vector<epoll_event> events(MAX_EVENT_NUM);

    std::cout << "Server is listening on Port 8080..." << std::endl;

    // ThreadPool
    int notify_fd = eventfd(0, EFD_NONBLOCK);
    ThreadPool pool(8, notify_fd);

    epoll_event notify_event;
    notify_event.data.fd = notify_fd;
    notify_event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, notify_fd, &notify_event);

    // TimerWheel
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_interval.tv_sec = 1;
    its.it_value.tv_sec = 1;
    timerfd_settime(timer_fd, 0, &its, nullptr);

    epoll_event timer_event;
    timer_event.data.fd = timer_fd;
    timer_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &timer_event);

    TimerWheel timer;
    timer.init(60, connections.data());

    for (;;)
    {
        int request_num = epoll_wait(epoll_fd, events.data(), MAX_EVENT_NUM, -1);

        for (int i = 0; i < request_num; i += 1)
        {
            epoll_event curr_event = events[i];    //current event

            if (curr_event.data.fd == listen_fd)   // accept new connection                     
            {   
                epoll_event conn_event;

                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                for (;;)
                {
                    int conn_fd = accept(listen_fd, (sockaddr *)&client_addr, &client_len);

                    if (conn_fd != -1)
                    {
                        int old_flag = fcntl(conn_fd, F_GETFL, 0);
                        fcntl(conn_fd, F_SETFL, old_flag | O_NONBLOCK);

                        conn_event.data.fd = conn_fd;
                        conn_event.events = EPOLLIN | EPOLLONESHOT | EPOLLET;

                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &conn_event);

                        connections[conn_fd].init(conn_fd);

                        timer.add(conn_fd);

                        std::cout << "new client connected! client fd: " << 
                            conn_fd << std::endl;
                    }
                    else if (conn_fd == -1)
                    {                 
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }
                        else
                        {
                            std::cout << "client connected error!" << std::endl;
                            break;
                        }
                    }
                }
            }
            else if (curr_event.data.fd == notify_fd)  // get processed result
            {                                          // and send to client
                uint64_t result_num;
                if ((read(curr_event.data.fd, &result_num, sizeof(result_num))) < 0)
                {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                    {    
                        std::cout << "Failed to get processed data!" << std::endl;
                        result_num = 0;
                    }
                }

                for (uint64_t i = 0; i < result_num; i += 1)
                {
                    Connection *result_connection;
                    {                      
                        std::unique_lock<std::mutex> lock(pool.result_mutex);
                        if(pool.result_queue.empty())
                            break;
                        result_connection = pool.result_queue.front();
                        pool.result_queue.pop();
                    }

                    if (result_connection->state() == ConnectionState::READ)
                    {
                        epoll_event rearm_conn_event;
                        rearm_conn_event.events = EPOLLIN | EPOLLONESHOT | EPOLLET;
                        rearm_conn_event.data.fd = result_connection->fd();
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, result_connection->fd(),
                                  &rearm_conn_event);
                        timer.add(result_connection->fd());
                    }
                    else if(result_connection->state() == ConnectionState::WRITE)
                    {
                        WriteState write_state = result_connection->process_write();
                        if (write_state == WriteState::WRITE_DONE)
                        {
                            if (result_connection->read_buffer_len())
                            {                                       //防止粘包
                                pool.enqueue(Task(result_connection,
                                                  result_connection->version()));
                            }
                            else
                            {
                                epoll_event rearm_conn_event;
                                rearm_conn_event.events = EPOLLIN | EPOLLONESHOT | EPOLLET;
                                rearm_conn_event.data.fd = result_connection->fd();
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, result_connection->fd(),
                                          &rearm_conn_event);
                                timer.add(result_connection->fd());
                            }
                        }
                        else if (write_state == WriteState::WRITE_AGAIN)
                        {
                            epoll_event write_again_event;
                            write_again_event.data.fd = result_connection->fd();
                            write_again_event.events = EPOLLOUT | EPOLLET;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD,
                                      result_connection->fd(), &write_again_event);
                        }
                        else
                        {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL,
                                      result_connection->fd(), NULL);
                            close(result_connection->fd());
                            timer.remove(result_connection->fd());
                            std::cout << "close. client fd: " << result_connection->fd() << std::endl;
                        }
                    }
                    else if(result_connection->state() == ConnectionState::CLOSE)
                    {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL,
                                  result_connection->fd(), NULL);
                        close(result_connection->fd());
                        timer.remove(result_connection->fd());
                        std::cout << "Request error, close connection. fd: " << 
                            result_connection->fd() << std::endl;
                    }
                }
            }
            else if (curr_event.data.fd == timer_fd)
            {
                uint64_t expirations;
                if (read(timer_fd, &expirations, sizeof(expirations)) < 0)
                {
                    expirations = 0;
                }
                while (expirations-- > 0)
                {
                    timer.tick(epoll_fd);
                }
            }
            else if (curr_event.events & EPOLLOUT)//上面的不会有epollout吗
            {
                WriteState write_state = connections[curr_event.data.fd].process_write();

                if (write_state == WriteState::WRITE_DONE)
                {
                    if(connections[curr_event.data.fd].read_buffer_len())
                    {
                        pool.enqueue(Task(&connections[curr_event.data.fd],
                                          connections[curr_event.data.fd].version()));
                    }
                    else                        
                    {
                        epoll_event rearm_conn_event;
                        rearm_conn_event.events = EPOLLIN | EPOLLONESHOT | EPOLLET;
                        rearm_conn_event.data.fd = curr_event.data.fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, curr_event.data.fd,
                                  &rearm_conn_event);
                        timer.add(curr_event.data.fd);
                    }
                }
                else if (write_state == WriteState::WRITE_CLOSE)
                {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_event.data.fd, NULL);
                    close(curr_event.data.fd);
                    timer.remove(curr_event.data.fd);
                    std::cout << "Write done, close connection. fd:" << 
                        curr_event.data.fd << std::endl;
                }
                else if (write_state == WriteState::WRITE_ERROR)
                {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_event.data.fd, NULL);
                    close(curr_event.data.fd);
                    timer.remove(curr_event.data.fd);
                    std::cout << "Write error, close connection. fd:" 
                        << curr_event.data.fd << std::endl;
                }
            }
            else if (curr_event.events & EPOLLIN)    // read data and hand to worker thread
            {
                char buffer[1024];

                ssize_t read_bytes;
                for (;;)
                {
                    timer.remove(curr_event.data.fd);

                    read_bytes = read(curr_event.data.fd, (void *)buffer, sizeof(buffer) - 1);

                    if (read_bytes > 0)
                    {
                        
                        connections[curr_event.data.fd].append_to_read_buffer(buffer, read_bytes);
                    }
                    else if (read_bytes == 0)
                    {
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_event.data.fd, NULL);
                        close(curr_event.data.fd);
                        timer.remove(curr_event.data.fd);
                        std::cout << "Connection closed by peer. client fd: " << 
                            curr_event.data.fd << std::endl;
                        break;
                    }
                    else if (read_bytes < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            pool.enqueue(Task(&connections[curr_event.data.fd],
                                              connections[curr_event.data.fd].version()));
                            break;
                        }
                        else if (errno == ECONNRESET)
                        {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_event.data.fd, NULL);
                            close(curr_event.data.fd);
                            timer.remove(curr_event.data.fd);
                            std::cout << "Connection closed by peer. client fd: " << 
                                curr_event.data.fd << std::endl;
                            break;
                        }
                        else
                        {
                            std::cout << "read error  client fd: " <<
                                curr_event.data.fd << std::endl;
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_event.data.fd, NULL);
                            close(curr_event.data.fd);
                            timer.remove(curr_event.data.fd);
                            break;
                        }
                    }
                }
            }
        }
    }

    close(listen_fd);
    close(epoll_fd);

    return 0;
}
