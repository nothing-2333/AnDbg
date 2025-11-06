#pragma once

#include <cstring>
#include <string>
#include <vector>
#include <sstream> 


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

  void print();

  ~Log();
};

static std::string format_log(std::string content, const char* file, int line)
{
    const char* filename = file;
    const char* slash = strrchr(file, '/');
    if (slash) filename = slash + 1;
    
    std::ostringstream oss;
    oss << "[" << filename << ":" << line << "] " << content;
    return oss.str();
}

#define LOG(level, content) \
  Log::get_instance()->add(level, content)

// 带文件名和行号的版本
#define LOG_P(level, content) \
    LOG(level, format_log(content, __FILE__, __LINE__))

#define LOG_DEBUG(content) LOG_P(LogLevel::DEBUG, content)
#define LOG_WARNING(content) LOG_P(LogLevel::WARNING, content)
#define LOG_ERROR(content) LOG_P(LogLevel::ERROR, content)