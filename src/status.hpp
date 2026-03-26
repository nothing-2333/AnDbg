#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <cstring>
#include <string>
#include "fmt/format.h"

namespace Base
{

enum class StatusType
{
  SUCCESS, 
  FAIL,
};

class Status 
{
public:
  Status(std::string string, StatusType type) : m_string(string), m_type(type) {};

  const char* c_str() { return m_string.c_str(); }

  std::string to_string() { return m_string; }

  static Status fail(const std::string& string) { return std::move((Status(string, StatusType::FAIL))); }
  template<typename... Args>
  static Status fail(const fmt::format_string<Args...>& format, Args&&... args) 
  {
    std::string formatted_content = fmt::format(format, std::forward<Args>(args)...);
    return std::move((Status(formatted_content, StatusType::FAIL)));
  }
  
  static Status success(const std::string& string) { return std::move((Status(string, StatusType::SUCCESS))); }
  template<typename... Args>
  static Status success(const fmt::format_string<Args...>& format, Args&&... args) 
  {
    std::string formatted_content = fmt::format(format, std::forward<Args>(args)...);
    return std::move((Status(formatted_content, StatusType::SUCCESS)));
  }

  bool is_success() { return m_type == StatusType::SUCCESS; };
  bool is_fail() { return m_type == StatusType::FAIL; };

private:
  StatusType m_type;
  std::string m_string;
};

}

