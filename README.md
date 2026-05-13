# VServer

[简体中文](./README.zh-CN.md) | English

VServer is a high-performance HTTP/1.1 Web Server written in C++ on Linux.

It is built from scratch to understand the core mechanisms behind high-concurrency network servers, including `epoll`, non-blocking I/O, thread pool scheduling, HTTP parsing, timeout management, asynchronous logging, command line configuration, and graceful shutdown.

The project can serve static files from the `Resources/` directory and can also be used as a lightweight personal technical notes/blog server.

---

## Features

### Core Network Model

- Event-driven I/O based on `epoll`
- Non-blocking socket operations
- Multi-threaded Reactor-style architecture
- Main thread handles I/O events
- Worker threads handle HTTP parsing and business logic
- `eventfd` used for worker-to-main-thread notification
- `EPOLLONESHOT` used to avoid duplicated processing of the same connection
- Supports HTTP Keep-Alive
- Supports sticky packet, half packet and basic pipeline handling

### HTTP Support

- HTTP/1.1 request parsing
- Finite State Machine based HTTP parser
- Request line parsing
- Header parsing
- Body parsing with `Content-Length`
- Supported methods:
  - `GET`
  - `HEAD`
  - `POST /echo`
- MIME type detection
- Basic path traversal protection
- Common HTTP error responses:
  - `400 Bad Request`
  - `403 Forbidden`
  - `404 Not Found`
  - `500 Internal Server Error`

### Static File Service

- Serves files from the `Resources/` directory
- Uses `stat()` to check file metadata
- Uses `mmap()` + `writev()` for static file response
- Supports common MIME types:
  - `text/html`
  - `text/css`
  - `application/javascript`
  - `image/png`
  - `image/jpeg`
  - `image/x-icon`
  - `application/json`
  - `application/pdf`

### Timeout Management

- Uses `timerfd` to generate periodic timeout ticks
- Uses Timer Wheel to manage idle connections
- Automatically closes expired connections
- Cleans timer links after removing connections

### Logging System

- Custom asynchronous logger
- Log levels:
  - `DEBUG`
  - `INFO`
  - `WARN`
  - `ERROR`
  - `FATAL`
- Timestamped logs
- Source file and line number in logs
- Background log writing thread
- Log queue with `condition_variable`
- Log file rotation by size
- New log file generated for each server run

### Configuration

Supports command line configuration:

```bash
./server \
  --port 8080 \
  --thread-nums 8 \
  --timeout 60 \
  --max-conn 65535 \
  --log-dir Logs \
  --log-level INFO \
  --log-size 10485760
```

| Option | Description | Default |
|---|---|---|
| `--port` | Listening port | `8080` |
| `--thread-nums` | Number of worker threads | `8` |
| `--timeout` | Connection timeout in seconds | `60` |
| `--max-conn` | Maximum connection count | `65535` |
| `--log-dir` | Log directory | `Logs` |
| `--log-level` | Log level | `DEBUG` |
| `--log-size` | Max log file size in bytes | `10485760` |

### Graceful Shutdown

- Handles `SIGINT`
- Handles `SIGTERM`
- Uses `std::atomic<bool>` as stop flag
- Exits the epoll loop safely
- Flushes asynchronous logger before shutdown

---

## Project Structure

```text
.
├── main.cpp
├── Connection/
│   ├── Connection.h
│   └── Connection.cpp
├── WebServer/
│   ├── WebServer.h
│   └── WebServer.cpp
├── ThreadPool/
│   ├── ThreadPool.h
│   └── ThreadPool.cpp
├── TimerWheel/
│   ├── TimerWheel.h
│   └── TimerWheel.cpp
├── Logger/
│   ├── Logger.h
│   └── Logger.cpp
├── Config/
│   ├── Config.h
│   └── Config.cpp
├── Resources/
│   └── index.html
├── Logs/
└── Makefile
```

---

## Overall Architecture

```mermaid
flowchart TD
    Client[Client / Browser / ab] -->|TCP Connection| Main[Main Reactor Thread]

    Main -->|epoll_wait| Epoll[epoll]
    Epoll -->|EPOLLIN listen_fd| Accept[Accept New Connection]
    Epoll -->|EPOLLIN client_fd| Read[Non-blocking Read]
    Epoll -->|EPOLLOUT client_fd| Write[writev Response]
    Epoll -->|timerfd| Timer[TimerWheel Tick]
    Epoll -->|eventfd| Result[Handle Worker Result]

    Accept --> Conn[Connection Pool]
    Read --> Buffer[Append to Read Buffer]
    Buffer --> TaskQueue[ThreadPool Task Queue]

    TaskQueue --> Worker[Worker Threads]
    Worker --> FSM[HTTP FSM Parser]
    FSM --> Response[Build Response]
    Response --> ResultQueue[Result Queue]
    ResultQueue -->|eventfd notify| Result

    Result -->|READ| RearmRead[Rearm EPOLLIN]
    Result -->|WRITE| TryWrite[Try writev]
    TryWrite -->|Done| RearmRead
    TryWrite -->|EAGAIN| RearmWrite[Rearm EPOLLOUT]
    TryWrite -->|Close/Error| Close[Close Connection]

    Timer -->|expired fd| Close
```

---

## Request Lifecycle

```mermaid
sequenceDiagram
    participant C as Client
    participant M as Main Reactor
    participant E as epoll
    participant P as ThreadPool
    participant Conn as Connection
    participant L as Logger

    C->>M: TCP connect
    M->>E: accept + add EPOLLIN
    C->>M: HTTP request data
    E-->>M: EPOLLIN
    M->>Conn: non-blocking read
    Conn->>Conn: append read_buffer
    M->>P: enqueue parse task
    P->>Conn: process_read()
    Conn->>Conn: HTTP FSM parse
    Conn->>Conn: build response buffer
    P->>M: eventfd notify
    M->>Conn: process_write()
    Conn->>C: writev response
    M->>L: async log
```

---

## HTTP Parser FSM

```mermaid
stateDiagram-v2
    [*] --> REQUEST_LINE

    REQUEST_LINE --> HEADERS: parse request line OK
    REQUEST_LINE --> BAD_REQUEST: invalid request line

    HEADERS --> HEADERS: parse one header
    HEADERS --> BODY: POST with Content-Length
    HEADERS --> REQUEST_READY: empty line + GET/HEAD
    HEADERS --> BAD_REQUEST: invalid header

    BODY --> BODY: body incomplete
    BODY --> REQUEST_READY: body complete

    REQUEST_READY --> FILE_REQUEST: GET/HEAD static file
    REQUEST_READY --> ECHO_REQUEST: POST /echo
    REQUEST_READY --> NO_RESOURCE: file not found
    REQUEST_READY --> FORBIDDEN_REQUEST: forbidden path/file
    REQUEST_READY --> INTERNAL_ERROR: server internal error

    FILE_REQUEST --> WRITE
    ECHO_REQUEST --> WRITE
    BAD_REQUEST --> WRITE
    NO_RESOURCE --> WRITE
    FORBIDDEN_REQUEST --> WRITE
    INTERNAL_ERROR --> WRITE

    WRITE --> REQUEST_LINE: keep-alive
    WRITE --> [*]: close
```

---

## Async Logger

```mermaid
flowchart LR
    A[LOG_INFO / LOG_ERROR Macro] --> B[Format Log Message]
    B --> C[Push to Log Queue]
    C --> D[condition_variable notify_one]
    D --> E[Logger Worker Thread]
    E --> F[Pop Message]
    F --> G{File Size Exceeds Limit?}
    G -->|No| H[Write to Current Log File]
    G -->|Yes| I[Flush and Close Old File]
    I --> J[Open New Log File]
    J --> H
    H --> K[Update Current File Size]
```

---

## Timer Wheel

```mermaid
flowchart TD
    Add[Add Connection FD] --> Slot[Target Slot = current_slot + timeout]
    Slot --> List[Insert FD into Slot Linked List]

    Tick[timerfd Tick Every 1s] --> Move[Move current_slot]
    Move --> Expired[Get Expired FD List]
    Expired --> CloseFd[Close Expired Connections]
    CloseFd --> Clean[Reset prev / next / position]
```

---

## Build

```bash
make
```

Clean build files:

```bash
make clean
```

Clean logs:

```bash
make clean-logs
```

---

## Run

Run with default configuration:

```bash
./server
```

Run with custom configuration:

```bash
./server --port 8080 --thread-nums 8 --timeout 60 --log-level INFO
```

Then open:

```text
http://127.0.0.1:8080/
```

---

## HTTP Examples

### GET

```bash
curl -i http://127.0.0.1:8080/
```

### HEAD

```bash
curl -I http://127.0.0.1:8080/index.html
```

### POST /echo

```bash
curl -i -X POST http://127.0.0.1:8080/echo --data "hello world"
```

Expected response body:

```text
hello world
```

### 404 Not Found

```bash
curl -i http://127.0.0.1:8080/not_exist.html
```

### 403 Forbidden

```bash
curl -i http://127.0.0.1:8080/../../etc/passwd
```

---

## Benchmark

Example benchmark command:

```bash
ab -n 100000 -c 500 -k http://127.0.0.1:8080/
```

Recommended benchmark mode:

```bash
./server --log-level WARN
```

Using `WARN` or `ERROR` log level is recommended during benchmarking to avoid excessive log generation.

Example metrics to record:

```text
Requests per second:
Failed requests:
Concurrency level:
Keep-Alive requests:
CPU usage:
Memory usage:
```

---

## Main Modules

### WebServer

Responsible for:

- epoll initialization
- listening socket initialization
- new connection handling
- read/write event handling
- thread pool result handling
- timer tick handling
- connection closing
- graceful shutdown

### Connection

Responsible for:

- per-connection read/write buffers
- HTTP FSM parsing
- request processing
- response construction
- `writev()` response sending
- keep-alive state management
- MIME type detection
- static file response
- POST /echo response

### ThreadPool

Responsible for:

- worker thread management
- task queue
- HTTP parsing execution
- result queue
- notifying main thread through `eventfd`

### TimerWheel

Responsible for:

- idle connection timeout management
- adding and removing connections
- closing expired connections

### Logger

Responsible for:

- asynchronous log queue
- background log writing
- log level filtering
- timestamp formatting
- log file rotation

### Config

Responsible for:

- command line argument parsing
- startup configuration

---

## Roadmap

Planned improvements:

- More complete URL decoding
- More robust path normalization with `realpath`
- Static file cache
- `sendfile()` based file transmission
- Range request support
- More complete test scripts
- Personal notes/blog static site pages

---

## Notes

This project is mainly built for learning and demonstrating:

- Linux network programming
- high-concurrency server architecture
- HTTP protocol parsing
- non-blocking I/O
- multithreaded coordination
- practical C++ systems programming

It is not intended to be a full replacement for production web servers such as Nginx, but it aims to implement and explain the core mechanisms behind them.
