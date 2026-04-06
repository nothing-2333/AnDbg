#include "file.hpp"
#include "log.hpp"
#include <string>
#include <sys/stat.h>
#include <vector>


namespace Base 
{

File::File(const std::string& path) : m_path(path), m_is_directory(check_directory_type(path)), m_dir_handle(nullptr, closedir)
{
  open_path(path, m_is_directory);
}

File::File(const std::string& path, bool is_directory) : m_path(path), m_is_directory(is_directory), m_dir_handle(nullptr, closedir)
{
  open_path(path, m_is_directory);
}

void File::open_path(const std::string& path, bool is_directory)
{
  if (is_directory)
  {
    DIR* dir = opendir(path.c_str());
    if (!dir) 
    {
      LOG_ERROR("无法打开目录 {}: {}", path, strerror(errno));
      return;
    }
    m_dir_handle.reset(dir);
  }
  else 
  {
    m_file_stream.open(path);
    if (!m_file_stream.is_open())
    {
      LOG_ERROR("无法打开文件 {}: {}", path, strerror(errno));
      return;
    }
  }
}

bool File::check_directory_type(const std::string& path)
{
  struct stat st;
  if (stat(path.c_str(), &st) != 0) 
  {
    LOG_DEBUG("检查路径[{}]是否为目录失败: {}", path, strerror(errno));
    return false;
  }
  return S_ISDIR(st.st_mode);
}

bool File::is_open() const
{
  if (m_is_directory)
    return m_dir_handle != nullptr;
  else 
   return m_file_stream.is_open();
}

std::vector<dirent*> File::list_entries() 
{
  std::vector<dirent*> entries;

  if (!m_is_directory || !m_dir_handle) 
  {
    LOG_ERROR("无法从非目录或已关闭的目录句柄读取内容");
    return entries;
  }

  rewinddir(m_dir_handle.get());
    
  dirent* entry;
  while ((entry = readdir(m_dir_handle.get())) != nullptr) 
  {
    // 跳过 . 和 ..
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
    {
      continue;
    }
    entries.emplace_back(entry);
  }
    
  return entries;
}

std::vector<char> File::read_all() 
{
  if (m_is_directory || !m_file_stream.is_open()) 
  {
    LOG_ERROR("无法从目录或已关闭的文件流读取内容");
    return {};
  }
    
  // 保存当前位置
  std::streampos original_pos = m_file_stream.tellg();
  m_file_stream.seekg(0, std::ios::beg);
    
  std::vector<char> content;
  char ch;
  while (m_file_stream.get(ch)) 
    content.push_back(ch);
    
  // 恢复位置
  m_file_stream.seekg(original_pos);
    
  return content;
}

std::vector<std::string> File::read_lines() 
{
  if (m_is_directory || !m_file_stream.is_open()) 
  {
    LOG_ERROR("无法从目录或已关闭的文件流读取内容");
    return {};
  }

  std::vector<std::string> lines;

  std::streampos original_pos = m_file_stream.tellg();
  m_file_stream.seekg(0, std::ios::beg);
    
  std::string line;
  // 换行符 \n 是截止符号
  while (std::getline(m_file_stream, line)) 
  {
    lines.push_back(std::move(line));
  }
    
  m_file_stream.seekg(original_pos);
  return lines;
}

std::string File::read_line() 
{
  if (m_is_directory || !m_file_stream.is_open()) 
  {
    LOG_ERROR("无法从目录或已关闭的文件流读取内容");
    return "";
  }
    
  std::string line;
  if (std::getline(m_file_stream, line)) return line;
  else return "";
}

}

