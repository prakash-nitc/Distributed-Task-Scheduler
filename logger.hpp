/*
 * logger.hpp — Structured logger for Distributed Task Scheduler
 *
 * Stream-based API:  LOG_INFO("MASTER") << "Worker #" << i << " connected";
 * Output format:     [2026-05-17 14:32:01.234] [INFO ] [MASTER] Worker #2 connected
 */

#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <unistd.h>

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

class Logger {
public:
    static Logger& get() {
        static Logger instance;
        return instance;
    }

    void set_min_level(LogLevel level) { min_level_ = level; }

    void log(LogLevel level, const std::string& component, const std::string& msg) {
        if (level < min_level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << timestamp() << " " << level_tag(level)
                  << " [" << component << "] " << msg << "\n";
    }

private:
    LogLevel    min_level_ = LogLevel::DEBUG;
    std::mutex  mutex_;
    bool        colors_    = isatty(STDOUT_FILENO);

    static std::string timestamp() {
        using namespace std::chrono;
        auto now     = system_clock::now();
        auto ms      = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        std::time_t t = system_clock::to_time_t(now);
        struct tm tm_info{};
        localtime_r(&t, &tm_info);
        char buf[24];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        char result[32];
        std::snprintf(result, sizeof(result), "[%s.%03lld]", buf, (long long)ms.count());
        return result;
    }

    std::string level_tag(LogLevel level) const {
        if (colors_) {
            switch (level) {
                case LogLevel::DEBUG: return "\033[36m[DEBUG]\033[0m";
                case LogLevel::INFO:  return "\033[32m[INFO ]\033[0m";
                case LogLevel::WARN:  return "\033[33m[WARN ]\033[0m";
                case LogLevel::ERROR: return "\033[31m[ERROR]\033[0m";
            }
        } else {
            switch (level) {
                case LogLevel::DEBUG: return "[DEBUG]";
                case LogLevel::INFO:  return "[INFO ]";
                case LogLevel::WARN:  return "[WARN ]";
                case LogLevel::ERROR: return "[ERROR]";
            }
        }
        return "[?????]";
    }
};

// Temporary stream object — flushes to Logger on destruction (end of statement).
class LogStream {
public:
    LogStream(LogLevel level, std::string component)
        : level_(level), component_(std::move(component)) {}

    ~LogStream() {
        Logger::get().log(level_, component_, ss_.str());
    }

    template<typename T>
    LogStream& operator<<(const T& val) { ss_ << val; return *this; }

private:
    LogLevel          level_;
    std::string       component_;
    std::ostringstream ss_;
};

#define LOG_DEBUG(comp) LogStream(LogLevel::DEBUG, comp)
#define LOG_INFO(comp)  LogStream(LogLevel::INFO,  comp)
#define LOG_WARN(comp)  LogStream(LogLevel::WARN,  comp)
#define LOG_ERROR(comp) LogStream(LogLevel::ERROR, comp)

#endif // LOGGER_HPP
