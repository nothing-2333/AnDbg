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
  messages.push_back(std::pair(level, content));
}

void Log::print()
{
  for (auto [level, content] : messages)
  {
    std::string line_content = "";
    switch (level) 
    {
      case LogLevel::DEBUG:
        line_content = "[DEBUG] "; 
        break;
      case LogLevel::WARNING:
        line_content = "[WARNING] "; 
        break;
      case LogLevel::ERROR:
        line_content = "[ERROR] "; 
        break;
      default: 
        line_content = "[UNKNOWN] "; 
        break;
    }
    line_content += (content + "\n");
    std::cout << line_content;
  }
}