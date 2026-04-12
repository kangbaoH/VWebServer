#pragma once

#include <string>

class Connection
{
private:
    int fd;
    std::string read_buffer;
    std::string write_buffer;

public:
    Connection() : fd(-1) {}

    Connection(int client_fd) : fd(client_fd) {}

    void init(int client_fd);

    int get_fd();

    char *get_write_buffer_data();

    size_t get_write_buffer_len();

    void write2read_buffer(char *buffer);

    void process();

    void clear_write_buffer();
};
