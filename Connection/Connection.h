#pragma once

#include <string>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/uio.h>

enum class LineState
{
    LINE_OK,
    LINE_BAD,
    LINE_OPEN
};

enum class CheckState
{
    REQUEST_LINE,
    HEADERS,
    BODY,
    FINISH
};

enum class HttpCode
{
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    INTERNAL_ERROR,
    FORBIDDEN_REQUEST,
    NO_RESOURCE,
    FILE_REQUEST
};

enum class ConnectionState
{
    READ,
    WRITE,
    CLOSE
};

enum class WriteState
{
    WRITE_DONE,
    WRITE_AGAIN,
    WRITE_CLOSE,
    WRITE_ERROR
};

#define RESOURCE_PATH_MAXLEN 200

class Connection
{
private:
    int fd_;
    std::string read_buffer;
    std::string write_buffer;

    CheckState m_check_state;

    int check_index;
    int start_index;

    char *m_method;
    char *m_url;
    char *m_version;

    bool m_linger;
    int m_content_length;

    char *m_content;

    char resource_path[RESOURCE_PATH_MAXLEN];
    struct stat file_stat;
    char *file_address;

    iovec iovs[2];
    int bytes_to_send;
    int bytes_sent;

    ConnectionState connection_state_;

    int version_;

public:
    int prev_in_timerwheel;
    int next_in_timerwheel;
    int position_in_timerwheel;

    Connection();

    void init(int client_fd);

    inline int fd() { return fd_; }

    void append_to_read_buffer(char *buffer, size_t len);

    LineState parse_line();

    inline char *get_line() { return &read_buffer[start_index]; }

    HttpCode parse_request_line(char *text);

    HttpCode parse_headers(char *text);

    HttpCode parse_body(char *text);

    HttpCode process_read();

    void make_response();

    HttpCode do_request();

    WriteState process_write();

    void set_connection_state(ConnectionState state);

    inline ConnectionState state() { return connection_state_; }

    void reset_state();

    inline size_t read_buffer_len() { return read_buffer.length(); }

    inline int version() { return version_; }
};
