#include <iostream>

#include "Log.hpp"


Log* Log::get_instance()
{
  if (instance == nullptr)
  {
    instance = new Log();
  }
  return instance;
}

void Log::add(LogLevel level, std::string content)
{
  messages.emplace_back(std::pair(level, content));
}

std::string Log::to_string()
{
  std::ostringstream oss;
  for (auto [level, content] : messages)
  {
    
    switch (level) 
    {
      case LogLevel::DEBUG:
        oss << "[DEBUG] ";
        break;
      case LogLevel::WARNING:
        oss << "[WARNING] "; 
        break;
      case LogLevel::ERROR:
        oss << "[ERROR] "; 
        break;
      default: 
        oss << "[UNKNOWN] "; 
        break;
    }
    oss << content << "\n";
  }
  return oss.str();
}

void Log::print()
{
  std::cout << to_string();
}