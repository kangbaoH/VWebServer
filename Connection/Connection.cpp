#include "Connection.h"

Connection::Connection() : fd_(-1), version_(0) {}

void Connection::init(int client_fd)
{
    fd_ = client_fd;
    read_buffer.clear();
    write_buffer.clear();

    m_check_state = CheckState::REQUEST_LINE;

    check_index = 0;
    start_index = 0;

    m_linger = true;

    memset(resource_path, '\0', RESOURCE_PATH_MAXLEN);
    strcpy(resource_path, "/home/verta/VServer/Resources");

    version_ += 1;

    prev_in_timerwheel = -1;
    next_in_timerwheel = -1;
    position_in_timerwheel = -1;
}

void Connection::append_to_read_buffer(char *buffer, size_t len)
{
    read_buffer.append(buffer, len);
}

LineState Connection::parse_line()
{
    for (; check_index < read_buffer.length(); check_index += 1)
    {
        if (read_buffer[check_index] == '\r')
        {
            if (check_index + 1 < read_buffer.length())
            {
                if (read_buffer[check_index + 1] == '\n')
                {
                    read_buffer[check_index++] = '\0';
                    read_buffer[check_index++] = '\0';
                    return LineState::LINE_OK;
                }
                else
                {
                    return LineState::LINE_BAD;
                }
            }
            else
            {
                return LineState::LINE_OPEN;
            }
        }
    }
    return LineState::LINE_OPEN;
}

HttpCode Connection::parse_request_line(char *text)
{
    char *space_ptr = text;
    char *start_ptr;

    // get method
    while (*space_ptr == ' ' || *space_ptr == '\t')
    {
        space_ptr++;
    }
    start_ptr = space_ptr;
    space_ptr = strpbrk(space_ptr, " \t");
    if (!space_ptr)
    {
        return HttpCode::BAD_REQUEST;
    }
    m_method.pos = start_ptr - &read_buffer[0];
    m_method.len = space_ptr - start_ptr;

    // get url
    while (*space_ptr == ' ' || *space_ptr == '\t')
    {
        space_ptr++;
    }
    start_ptr = space_ptr;
    space_ptr = strpbrk(space_ptr, " \t");
    if (!space_ptr)
    {
        return HttpCode::BAD_REQUEST;
    }
    m_url.pos = start_ptr - &read_buffer[0];
    m_url.len = space_ptr - start_ptr;

    // get version
    while (*space_ptr == ' ' || *space_ptr == '\t')
    {
        space_ptr++;
    }
    start_ptr = space_ptr;
    space_ptr = strpbrk(space_ptr, "\r");
    m_version.pos = start_ptr - &read_buffer[0];
    m_version.len = space_ptr - start_ptr;

    return HttpCode::NO_REQUEST;
}

HttpCode Connection::parse_headers(char *text)
{
    while (*text == ' ' || *text == '\t')
    {
        text++;
    }

    if (text[0] == '\0')
    {
        if (read_buffer.compare(m_method.pos, m_method.len, "GET") == 0)
        {
            return HttpCode::GET_REQUEST;
        }
        else if (read_buffer.compare(m_method.pos, m_method.len, "POST") == 0)
        {
            m_check_state = CheckState::BODY;
            return HttpCode::NO_REQUEST;
        }
        else
        {
            return HttpCode::BAD_REQUEST;
        }
    }
    else
    {
        char *key_ptr = text;
        char *value_ptr;

        value_ptr = strchr(text, ':');
        if (!value_ptr)
        {
            return HttpCode::BAD_REQUEST;
        }
        *value_ptr++ = '\0';

        while (*value_ptr == ' ' || *value_ptr == '\t')
        {
            value_ptr++;
        }

        if (strcasecmp(key_ptr, "Connection") == 0)
        {
            if (strcasecmp(value_ptr, "close") == 0)
            {
                m_linger = false;
            }
        }
        else if (strcasecmp(key_ptr, "Content-Length") == 0)
        {
            m_content_length = atoi(value_ptr);
        }

        return HttpCode::NO_REQUEST;
    }
}

HttpCode Connection::parse_body(char *text)
{
    if (read_buffer.length() - start_index < m_content_length)
    {
        return HttpCode::NO_REQUEST;
    }

    m_content.pos = text - &read_buffer[0];
    m_content.len = m_content_length;

    return HttpCode::GET_REQUEST;
}

HttpCode Connection::process_read()
{
    LineState line_state = LineState::LINE_OK;
    HttpCode ret = HttpCode::NO_REQUEST;
    char *text = nullptr;

    while ((m_check_state == CheckState::BODY) ||
           ((line_state = parse_line()) == LineState::LINE_OK))
    {
        text = get_line();
        start_index = check_index;

        switch (m_check_state)
        {
        case CheckState::REQUEST_LINE:
        {
            if ((ret = parse_request_line(text)) == HttpCode::BAD_REQUEST)
            {
                return ret;
            }
            m_check_state = CheckState::HEADERS;
            break;
        }
        case CheckState::HEADERS:
        {
            ret = parse_headers(text);
            if (ret == HttpCode::BAD_REQUEST)
            {
                return ret;
            }
            else if (ret == HttpCode::GET_REQUEST)
            {
                m_check_state = CheckState::FINISH;
                return do_request();
            }
            break;
        }
        case CheckState::BODY:
        {
            ret = parse_body(text);
            if (ret == HttpCode::BAD_REQUEST)
            {
                return ret;
            }
            else if (ret == HttpCode::GET_REQUEST)
            {
                m_check_state = CheckState::FINISH;
                return do_request();
            }
            line_state = LineState::LINE_OPEN;
            break;
        };
        default:
        {
            return HttpCode::INTERNAL_ERROR;
        }
        }
    }

    if (line_state == LineState::LINE_BAD)
    {
        return HttpCode::BAD_REQUEST;
    }
    return ret;
}

void Connection::make_response()
{
    write_buffer += "HTTP/1.1 200 OK\r\n";
    write_buffer += "Content-Type: text/html\r\n";
    write_buffer += "Content-Length: " + std::to_string(file_stat.st_size) + "\r\n";
    write_buffer += "Connection: ";
    if (m_linger)
    {
        write_buffer += "keep-alive\r\n";
    }
    else
    {
        write_buffer += "close\r\n";
    }
    write_buffer += "\r\n";

    iovs[0].iov_base = (void *)write_buffer.data();
    iovs[0].iov_len = write_buffer.length();
    iovs[1].iov_base = file_address;
    iovs[1].iov_len = file_stat.st_size;
}

HttpCode Connection::do_request()
{
    size_t len = strlen(resource_path);
    if (read_buffer.compare(m_url.pos, m_url.len, "/") == 0)
    {
        size_t copy_len = std::min(
            strlen("/index.html"),
            static_cast<size_t>(RESOURCE_PATH_MAXLEN - len - 1)
        );
        memcpy(resource_path + len, "/index.html", copy_len);
    }
    else
    {
        size_t copy_len = std::min(
            m_url.len,
            static_cast<size_t>(RESOURCE_PATH_MAXLEN - len - 1)
        );
        memcpy(resource_path + len, read_buffer.data() + m_url.pos, copy_len);
    }

    if (stat(resource_path, &file_stat) < 0)
    {
        return HttpCode::NO_RESOURCE;
    }
    if (S_ISDIR(file_stat.st_mode))
    {
        return HttpCode::BAD_REQUEST;
    }
    if (!(file_stat.st_mode & S_IROTH))
    {
        return HttpCode::FORBIDDEN_REQUEST;
    }

    int fd = open(resource_path, O_RDONLY);
    if (fd < 0)
    {
        return HttpCode::NO_RESOURCE;
    }

    file_address = (char *)
        mmap(nullptr, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);

    if (file_address == MAP_FAILED)
    {
        return HttpCode::INTERNAL_ERROR;
    }

    make_response();
    return HttpCode::FILE_REQUEST;
}

WriteState Connection::process_write()
{
    bytes_to_send = iovs[0].iov_len + iovs[1].iov_len;
    for (;;)
    {
        bytes_sent = writev(fd_, iovs, 2);

        if (bytes_sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return WriteState::WRITE_AGAIN;
            }
            return WriteState::WRITE_ERROR;
        }
        bytes_to_send -= bytes_sent;

        if (bytes_to_send == 0)
        {
            if (file_address)
            {
                munmap(file_address, file_stat.st_size);
                file_address = nullptr;
            }

            if (m_linger)
            {
                reset_state();
                return WriteState::WRITE_DONE;
            }
            return WriteState::WRITE_CLOSE;
        }
        else if (bytes_sent < iovs[0].iov_len)
        {
            iovs[0].iov_base = (char *)iovs[0].iov_base + bytes_sent;
            iovs[0].iov_len -= bytes_sent;
        }
        else if (bytes_sent >= iovs[0].iov_len)
        {
            iovs[1].iov_base = (char *)iovs[1].iov_base + bytes_sent - iovs[0].iov_len;
            iovs[1].iov_len -= bytes_sent - iovs[0].iov_len;
            iovs[0].iov_len = 0;
        }
    }
}

void Connection::set_connection_state(ConnectionState state)
{
    connection_state_ = state;
}

void Connection::reset_state()
{
    if (check_index > 0 && check_index < read_buffer.length())
    {
        read_buffer.erase(0, check_index);
    }
    else
    {
        read_buffer.clear();
    }

    write_buffer.clear();

    m_check_state = CheckState::REQUEST_LINE;

    check_index = 0;
    start_index = 0;

    m_linger = false;

    memset(resource_path, '\0', RESOURCE_PATH_MAXLEN);
    strcpy(resource_path, "/home/verta/VServer/Resources");
}
