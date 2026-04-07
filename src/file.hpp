#pragma once

#include <dirent.h>
#include <fstream>
#include <optional>
#include <string>

namespace Base 
{

class File  
{

private:
  std::string m_path;      // 文件/目录路径
  bool m_is_directory;     // 是否是目录
  
  // 文件句柄
  std::ifstream m_file_stream;
  
  // 目录句柄
  std::unique_ptr<DIR, decltype(&closedir)> m_dir_handle;

public:
  // 禁止拷贝
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  
  // 允许移动
  File(File&& other) noexcept = default;
  File& operator=(File&& other) noexcept = default;

  // 用默认的就可以了, 资源会自动释放
  ~File() = default;

  // 打开文件或目录
  static std::optional<File> open(const std::string& path, bool is_directory);
  static std::optional<File> open(const std::string& path);

  // 检查是否是目录类型
  static bool check_directory_type(const std::string& path);

  // 检查是否是目录
  bool is_directory() const { return m_is_directory; };

  // 获取文件路径
  const std::string& path() const { return m_path; }

  // 获取原始文件流, 仅对文件有效
  std::ifstream& file_stream() { return m_file_stream; }

  // 获取原始目录句柄, 仅对目录有效
  DIR* directory_handle() { return m_dir_handle.get(); }

  // 检查文件/目录是否成功打开
  bool is_open() const;

  // 列出目录所有条目
  std::vector<dirent*> list_entries();

  // 读取文件全部内容
  std::vector<char> read_all();

  // 读取文件所有行
  std::vector<std::string> read_lines();

  // 读取一行
  std::string read_line();

private:
  File(const std::string& path);
  File(const std::string& path, bool is_directory);

  // 通过路径填充私有变量
  void open_path(const std::string& path, bool is_directory);
};

}
