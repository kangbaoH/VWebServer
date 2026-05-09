#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <condition_variable>
#include <queue>
#include <thread>
#include <sys/stat.h>

enum class Level
{
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger
{
private:
    std::ofstream file;
    Level log_level;

    size_t max_file_size;
    size_t current_file_size;
    int roll_index;
    std::string log_dir;

    std::thread worker;
    std::queue<std::string> log_queue;
    std::condition_variable condition;
    bool stop = false;

    std::mutex queue_mutex;

    std::string level_to_str(Level level);
    std::string current_time();

public:
    int init(const std::string &log_dir, Level level, size_t file_size);
    void log(Level level, const std::string &filename, int line,
             const std::string &msg);
    static Logger &instance();

    ~Logger();

private:
    Logger() = default;   
    Logger(const Logger &logger) = delete;
    Logger &operator=(const Logger &logger) = delete;
};

#define LOG_DEBUG(msg) Logger::instance().log(Level::DEBUG, __FILE__, __LINE__, msg)
#define LOG_INFO(msg) Logger::instance().log(Level::INFO, __FILE__, __LINE__, msg)
#define LOG_WARN(msg) Logger::instance().log(Level::WARN, __FILE__, __LINE__, msg)
#define LOG_ERROR(msg) Logger::instance().log(Level::ERROR, __FILE__, __LINE__, msg)
#define LOG_FATAL(msg) Logger::instance().log(Level::FATAL, __FILE__, __LINE__, msg)
