#include "WebServer.h"

void WebServer::epoll_init()
{
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd < 0)
    {
        LOG_FATAL("Epoll initialization failed.");
        exit(1);
    }
}

void WebServer::listen_init(int port)
{
    // create socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd < 0)
    {
        LOG_FATAL("Listen initialization failed.");
        exit(1);
    }

    // bind
    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (bind(listen_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        LOG_FATAL("Bind to listen failed.");
        exit(1);
    }

    // listen
    if (listen(listen_fd, 1024) < 0)
    {
        LOG_FATAL("Start listen failed.");
        exit(1);
    }

    // set to non-blocking
    int old_flag = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, old_flag | O_NONBLOCK);

    // add to epoll
    // create an epoll event
    epoll_event listen_event;
    listen_event.data.fd = listen_fd;
    listen_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &listen_event);

    LOG_INFO("Server is listening on Port 8080.");
}

void WebServer::threadpool_init(int thread_nums)
{
    notify_fd = eventfd(0, EFD_NONBLOCK);
    
    epoll_event notify_event;
    notify_event.data.fd = notify_fd;
    notify_event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, notify_fd, &notify_event);

    pool.init(thread_nums, notify_fd);
}

void WebServer::timer_init(int timeout)
{
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    if (timer_fd < 0)
    {
        LOG_FATAL("Timer fd initialization failed.");
        exit(1);
    }

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_interval.tv_sec = 1;
    its.it_value.tv_sec = 1;
    timerfd_settime(timer_fd, 0, &its, nullptr);

    epoll_event timer_event;
    timer_event.data.fd = timer_fd;
    timer_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &timer_event);

    timer.init(timeout, connections.data());
}

void WebServer::logger_init(std::string dir, Level level,size_t file_size)
{
    if (Logger::instance().init(dir, level, file_size) < 0)
    {
        std::cout << "Failed to init logger!\n";
        exit(1);
    }
}

void WebServer::start(int port, int thread_nums, int timeout, int max_connection_num)
{
    logger_init("Logs", Level::DEBUG, 10 * 1024 * 1024);

    _max_connection_num = max_connection_num;
    connections.resize(_max_connection_num);

    epoll_init();
    listen_init(port);
    timer_init(timeout);
    threadpool_init(thread_nums);

    std::vector<epoll_event> events(MAX_EVENT_NUM);
    for (;;)
    {
        int request_num = epoll_wait(epoll_fd, events.data(), MAX_EVENT_NUM, -1);

        for (int i = 0; i < request_num; i += 1)
        {
            curr_event = events[i]; // current event

            if (curr_event.data.fd == listen_fd) // accept new connection
            {
                handle_new_connection();
            }
            else if (curr_event.data.fd == notify_fd) // get processed result
            {                                         // and send to client
                handle_threadpool_result();
            }
            else if (curr_event.data.fd == timer_fd)
            {
                handle_timer_tick();
            }
            else if (curr_event.events & EPOLLOUT) 
            {
                handle_write();
            }
            else if (curr_event.events & EPOLLIN) // read data and hand to worker thread
            {
                handle_read();
            }
        }
    }

    close(timer_fd);
    close(listen_fd);
    close(epoll_fd);
}

void WebServer::close_connection(int fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    timer.remove(fd);
}

void WebServer::sign_epoll(int fd, int mode, uint32_t events)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = events;
    epoll_ctl(epoll_fd, mode, fd, &event);
}

void WebServer::handle_new_connection()
{
    for (;;)
    {       
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int conn_fd = accept(listen_fd, (sockaddr *)&client_addr, &client_len);

        if (!(conn_fd < _max_connection_num))
        {
            close(conn_fd);
            break;
        }

        if (conn_fd != -1)
        {
            int old_flag = fcntl(conn_fd, F_GETFL, 0);
            fcntl(conn_fd, F_SETFL, old_flag | O_NONBLOCK);

            sign_epoll(conn_fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLONESHOT | EPOLLET);

            connections[conn_fd].init(conn_fd);

            timer.add(conn_fd);

            LOG_INFO(
                       "New client connected. fd: " + std::to_string(conn_fd));
        }
        else if (conn_fd == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            else
            {
                LOG_ERROR("Client connected error!");
                break;
            }
        }
    }
}

void WebServer::handle_threadpool_result()
{
    uint64_t result_num;
    if ((read(curr_event.data.fd, &result_num, sizeof(result_num))) < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            LOG_ERROR("Failed to get processed data.");
            result_num = 0;
        }
    }

    for (uint64_t i = 0; i < result_num; i += 1)
    {
        Connection *result_connection;
        {
            std::unique_lock<std::mutex> lock(pool.result_mutex);
            if (pool.result_queue.empty())
                break;
            result_connection = pool.result_queue.front();
            pool.result_queue.pop();
        }

        if (result_connection->state() == ConnectionState::READ)
        {
            sign_epoll(result_connection->fd(), EPOLL_CTL_MOD,
                       EPOLLIN | EPOLLONESHOT | EPOLLET);
            timer.add(result_connection->fd());
        }
        else if (result_connection->state() == ConnectionState::WRITE)
        {
            WriteState write_state = result_connection->process_write();
            if (write_state == WriteState::WRITE_DONE)
            {
                if (result_connection->read_buffer_len())
                { // 防止粘包
                    pool.enqueue(Task(result_connection,
                                      result_connection->version()));
                }
                else
                {
                    sign_epoll(result_connection->fd(), EPOLL_CTL_MOD,
                               EPOLLIN | EPOLLONESHOT | EPOLLET);
                    timer.add(result_connection->fd());
                }
            }
            else if (write_state == WriteState::WRITE_AGAIN)
            {
                sign_epoll(result_connection->fd(), EPOLL_CTL_MOD,
                           EPOLLOUT | EPOLLET);
            }
            else
            {
                close_connection(result_connection->fd());
                LOG_INFO("Write done, close connection. fd: " +
                         std::to_string(result_connection->fd()));
            }
        }
        else if (result_connection->state() == ConnectionState::CLOSE)
        {
            close_connection(result_connection->fd());
            LOG_ERROR("Request error, close connection. fd: " +
                      std::to_string(result_connection->fd()));
        }
    }
}

void WebServer::handle_timer_tick()
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

void WebServer::handle_write()
{
    WriteState write_state = connections[curr_event.data.fd].process_write();

    if (write_state == WriteState::WRITE_DONE)
    {
        if (connections[curr_event.data.fd].read_buffer_len())
        {
            pool.enqueue(Task(&connections[curr_event.data.fd],
                              connections[curr_event.data.fd].version()));
        }
        else
        {
            sign_epoll(curr_event.data.fd, EPOLL_CTL_MOD,
                       EPOLLIN | EPOLLONESHOT | EPOLLET);
            timer.add(curr_event.data.fd);
        }
    }
    else if (write_state == WriteState::WRITE_AGAIN)
    {
        sign_epoll(curr_event.data.fd, EPOLL_CTL_MOD, EPOLLOUT | EPOLLET);
    }
    else if (write_state == WriteState::WRITE_CLOSE)
    {
        close_connection(curr_event.data.fd);
        LOG_INFO("Write done, close connection. fd: " +
                 std::to_string(curr_event.data.fd));
    }
    else if (write_state == WriteState::WRITE_ERROR)
    {
        close_connection(curr_event.data.fd);
        LOG_ERROR("Write error, close connection. fd: " +
                  std::to_string(curr_event.data.fd));
    }
}

void WebServer::handle_read()
{
    char buffer[1024];

    ssize_t read_bytes;

    timer.remove(curr_event.data.fd);
    for (;;)
    {
        read_bytes = read(curr_event.data.fd, (void *)buffer, sizeof(buffer) - 1);

        if (read_bytes > 0)
        {

            connections[curr_event.data.fd].append_to_read_buffer(buffer, read_bytes);
        }
        else if (read_bytes == 0)
        {
            close_connection(curr_event.data.fd);
            LOG_INFO("Connection closed by peer. fd: " +
                     std::to_string(curr_event.data.fd));
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
                close_connection(curr_event.data.fd);
                LOG_WARN("Connection reset by peer. fd: " +
                         std::to_string(curr_event.data.fd));
                break;
            }
            else
            {
                close_connection(curr_event.data.fd);
                LOG_ERROR("Read error. fd: " + std::to_string(curr_event.data.fd));
                break;
            }
        }
    }
}
