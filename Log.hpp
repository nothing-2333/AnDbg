#pragma once

#include <string>
#include <vector>


enum LogLevel
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

  void exit();
  void print();

public:
  static Log* get_instance();

  void add(LogLevel level, std::string content, bool is_exit=false);

  ~Log();
};