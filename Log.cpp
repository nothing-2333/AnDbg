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

void Log::add(LogLevel level, std::string content, bool is_exit)
{
  messages.push_back(std::pair(level, content));

  if (is_exit) this->exit();
}

void Log::exit()
{
  print();
  ::exit(1);
}

void Log::print()
{
  for (auto [level, content] : messages)
  {
    std::string line_content = "";
    switch (level) 
    {
      case DEBUG:
        line_content = "[DEBUG] "; 
        break;
      case WARNING:
        line_content = "[WARNING] "; 
        break;
      case ERROR:
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