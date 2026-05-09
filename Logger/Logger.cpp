#include "Logger.h"

std::string Logger::level_to_str(Level level)
{
    switch(level)
    {
    case Level::DEBUG:
    {
        return "[DEBUG]";
    }
    case Level::INFO:
    {
        return "[INFO]";
    }
    case Level::WARN:
    {
        return "[WARN]";
    }
    case Level::ERROR:
    {
        return "[ERROR]";
    }
    case Level::FATAL:
    {
        return "[FATAL]";
    }
    default:
    {
        return "[UNKNOWN]";
    }
    }
}

std::string Logger::current_time()
{
    using namespace std::chrono;

    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::time_t t = system_clock::to_time_t(now);
    std::tm tm_time;
    localtime_r(&t, &tm_time);

    std::ostringstream oss;
    oss << std::put_time(&tm_time, "%Y-%m-%d %H:%M:%S") << "."
        << std::setw(3) << std::setfill('0') << ms.count();

    return oss.str();
}

int Logger::init(const std::string& log_dir, Level level)
{
    mkdir(log_dir.c_str(), 0755);

    std::ostringstream filename;

    std::string time = current_time();
    std::replace(time.begin(), time.end(), ' ', '_');
    std::replace(time.begin(), time.end(), ':', '-');
    size_t dot_pos = time.find('.');
    if (dot_pos != std::string::npos)
    {
        time.erase(dot_pos);
    }

    filename << log_dir << "/server_" << time << ".log";

    file.open(filename.str(), std::ios::app);

    if(!file.is_open())
    {
        std::cout << "open " << filename.str() << " failed.\n";
        return -1;
    }

    log_level = level;
    return 0;
}

void Logger::log(Level level, const std::string &filename, int line,
                 const std::string &msg)
{
    if (level < log_level)
    {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(_mutex);
        file << "[" << current_time() << "] ";
        file << "[" << filename << ":" << line << "] ";
        file << level_to_str(level) << " " << msg << "\n";
        file.flush();
    }
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    file.close();
}
