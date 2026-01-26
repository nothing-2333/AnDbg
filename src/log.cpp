#include <iostream>
#include <string>

#include "log.hpp"

void Log::add(LogLevel level, std::string content)
{
  std::cout << to_string(level, content) << std::endl;
  messages.emplace_back(std::pair(level, content));
}

std::string Log::to_string(LogLevel level, std::string content)
{
  std::string result;
  switch (level) 
  {
    case LogLevel::DEBUG:
      result += "[DEBUG] ";
      break;
    case LogLevel::WARNING:
      result += "[WARNING] "; 
      break;
    case LogLevel::ERROR:
      result += "[ERROR] "; 
      break;
    default: 
      result += "[UNKNOWN] "; 
      break;
  }
  result += content;
  return result;
}

std::string Log::to_string()
{
  std::ostringstream oss;
  for (auto [level, content] : messages)
  {
    oss << to_string(level, content) << "\n";
  }
  return oss.str();
}