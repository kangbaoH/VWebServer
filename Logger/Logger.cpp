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

int Logger::init(const std::string &dir, Level level, size_t file_size)
{   
    log_dir = dir;
    log_level = level;
    max_file_size = file_size;

    current_file_size = max_file_size + 1;
    roll_index = 0;

    mkdir(log_dir.c_str(), 0755);  

    worker = std::thread([this]()
                         {
        for(;;)
        {
            std::string str;

            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                condition.wait(lock, [this]()
                               { return !log_queue.empty() || stop; });

                if (log_queue.empty() && stop)
                {
                    break;
                }

                str = std::move(log_queue.front());
                log_queue.pop();
            }

            if (current_file_size + str.size() > max_file_size)
            {
                if(file.is_open())
                {
                    file.flush();
                    file.close();
                }
                std::ostringstream filename;

                std::string time = current_time();
                std::replace(time.begin(), time.end(), ' ', '_');
                std::replace(time.begin(), time.end(), ':', '-');
                std::replace(time.begin(), time.end(), '.', '-');

                filename << log_dir << "/server_" << time << "-" << roll_index++
                         << ".log";

                file.open(filename.str(), std::ios::out);

                if (!file.is_open())
                {
                    std::cout << "open " << filename.str() << " failed.\n";
                    break;
                }

                current_file_size = 0;
            }
            file << str;
            current_file_size += str.size();
        }

        file.flush(); 
    });

    return 0;
}

void Logger::log(Level level, const std::string &filename, int line,
                 const std::string &msg)
{
    if (level < log_level)
    {
        return;
    }

    std::ostringstream oss;
    oss << "[" << current_time() << "] ";
    oss << "[" << filename << ":" << line << "] ";
    oss << level_to_str(level) << " " << msg << "\n";
    std::string str = oss.str();

    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        log_queue.push(std::move(str));
    }

    condition.notify_one();
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    stop = true;
    condition.notify_all();
    worker.join();
    file.flush();
    file.close();
}
