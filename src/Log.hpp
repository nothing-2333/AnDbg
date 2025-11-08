#pragma once

#include <cstring>
#include <string>
#include <vector>
#include <sstream> 

#include "fmt/base.h"
#include "fmt/format.h"


enum class LogLevel
{
  DEBUG,
  WARNING,
  ERROR,
};

class Log
{
private:
  // 存储日志
  std::vector<std::pair<LogLevel, std::string>> messages;

  // 唯一实例
  static Log* instance;

  // 私有构造函数
  Log() = default;

  // 禁止拷贝和赋值
  Log(const Log&) = delete;
  Log& operator=(const Log&) = delete;

public:
  static Log* get_instance();

  void add(LogLevel level, std::string content);

  std::string to_string();

  void print();

  ~Log();
};

static std::string format_log(const char* file, int line, std::string content)
{
    const char* filename = file;
    const char* slash = strrchr(file, '/');
    if (slash) filename = slash + 1;
    
    std::ostringstream oss;
    oss << "[" << filename << ":" << line << "] " << content;
    return oss.str();
}

template<typename... Args>
static std::string format_log(const char* file, int line, const fmt::format_string<Args...>& format, Args&&... args)
{
    const char* filename = file;
    const char* slash = strrchr(file, '/');
    if (slash) filename = slash + 1;

    try {
        // 先通过 fmt 格式化内容, 再拼接文件名, 行号
        std::string formatted_content = fmt::format(format, std::forward<Args>(args)...);
        std::ostringstream oss;
        oss << "[" << filename << ":" << line << "] " << formatted_content;
        return oss.str();
    }
    catch (const fmt::format_error& e) {
        // 格式化错误处理
        return fmt::format("[{}:{}] [Format Error: {}] (Format: {})", filename, line, e.what(), format.get());
    }
}

#define LOG(level, ...) \
  Log::get_instance()->add(level, format_log(__FILE__, __LINE__, __VA_ARGS__))

#define LOG_DEBUG(...) LOG(LogLevel::DEBUG, __VA_ARGS__)
#define LOG_WARNING(...) LOG(LogLevel::WARNING, __VA_ARGS__)
#define LOG_ERROR(...) LOG(LogLevel::ERROR, __VA_ARGS__)