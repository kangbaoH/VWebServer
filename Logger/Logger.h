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

    std::mutex _mutex;

    std::string level_to_str(Level level);
    std::string current_time();

public:
    int init(const std::string &log_dir, Level level);
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
