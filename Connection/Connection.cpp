#include "Connection.h"

void Connection::init(int client_fd)
{
    fd = client_fd;
}

int Connection::get_fd()
{
    return fd;
}

char *Connection::get_write_buffer_data()
{
    return write_buffer.data();
}

size_t Connection::get_write_buffer_len()
{
    return write_buffer.length();
}

void Connection::write2read_buffer(char *buffer)
{
    read_buffer.append(buffer);
}

void Connection::process()
{
    write_buffer = read_buffer;
    read_buffer.clear();
}

void Connection::clear_write_buffer()
{
    write_buffer.clear();
}
