#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <cstring>
#include <string>
#include "fmt/format.h"
#include <nlohmann/json.hpp>


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

  std::string to_string() { return is_success() ? "success| " + m_string : "fail| " + m_string; }

  template<typename... Args>
  static Status fail(const fmt::format_string<Args...>& format, Args&&... args) 
  {
    std::string formatted_content = fmt::format(format, std::forward<Args>(args)...);
    return std::move((Status(formatted_content, StatusType::FAIL)));
  }
  
  template<typename... Args>
  static Status success(const fmt::format_string<Args...>& format, Args&&... args) 
  {
    std::string formatted_content = fmt::format(format, std::forward<Args>(args)...);
    return std::move(Status(formatted_content, StatusType::SUCCESS));
  }
  static Status success(const nlohmann::json& json) { return std::move(Status(json.dump(), StatusType::SUCCESS)); }

  bool is_success() { return m_type == StatusType::SUCCESS; };
  bool is_fail() { return m_type == StatusType::FAIL; };

private:
  StatusType m_type;
  std::string m_string;
};

}

