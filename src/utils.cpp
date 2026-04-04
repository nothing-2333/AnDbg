#include <cstdint>
#include <dirent.h>
#include <vector>

#include "utils.hpp"
#include "log.hpp"

namespace Utils 
{

// ptrace 包装
bool ptrace_wrapper(int request, pid_t pid, void* address, void* data, size_t data_size, long* result)
{
  long int ret = 0;

  // 重置 errno
  errno = 0;
  if (request == PTRACE_GETREGSET || request == PTRACE_SETREGSET)
  {
    ret = ptrace(request, pid, *reinterpret_cast<unsigned int*>(address), data);
  }
  else  
    ret = ptrace(request, pid, address, data);

  // 记录日志
  LOG_DEBUG("ptrace(request: {}, pid: {}, address: {:p}, data: {:p}, data_size: {}, ret: {})",
    request, pid, static_cast<const void*>(address), static_cast<const void*>(data), data_size, ret
  );

  // 如果有返回值保存返回值
  if (result != nullptr) *result = ret;

  
  if (ret == -1 && errno != 0) 
  {
    LOG_ERROR("ptrace 调用失败, errno({}): {}", errno, strerror(errno));
    return false;
  }
  else return true;
}

pid_t waitpid_wrapper(pid_t pid, int* status, int __options)
{
  pid_t wpid = waitpid(pid, status, __options);

  // 按返回值分类打印日志, 更易排查问题
  if (wpid == -1) 
  {
    LOG_ERROR("waitpid 调用失败, 目标 PID: {}, errno({}): {}", pid, errno, strerror(errno));
  }
  else if (wpid == 0)
  {
    LOG_DEBUG("waitpid 非阻塞返回: 目标 PID: {} 仍在运行", pid);
  }
  else
  {
    // 解析状态, 补充更详细的日志
    std::string status_desc = "未知状态";
    if (status != nullptr)
    {
      if (WIFEXITED(*status)) 
      {
        status_desc = fmt::format("正常退出, 退出码: {}", WEXITSTATUS(*status));
      } 
      else if (WIFSIGNALED(*status)) 
      {
        status_desc = fmt::format("信号终止, 信号: {}", WTERMSIG(*status));
      } 
      else if (WIFSTOPPED(*status)) 
      {
        status_desc = fmt::format("进程暂停, 信号: {}", WSTOPSIG(*status));
      }
    }
    LOG_DEBUG("waitpid 完成, 目标 PID: {}, 返回 PID: {}, 状态: {}", pid, wpid, status_desc);
  }

  return wpid;
}

long get_page_size()
{
  static long page_size = sysconf(_SC_PAGE_SIZE);
  if (page_size <= 0) 
  {
    LOG_WARNING("获取系统页面大小失败, 使用默认大小 4096 字节");
    page_size = 4096;
  }
  return page_size;
}

// 对齐函数
uint64_t align_up(uint64_t value, uint64_t alignment)
{
  return (value + alignment - 1) & ~(alignment - 1);
}

uint64_t align_down(uint64_t value, uint64_t alignment)
{
  return value & ~(alignment - 1);
}

// 页对齐
uint64_t align_page_up(uint64_t value)
{
  return align_up(value, static_cast<uint64_t>(get_page_size()));
}

uint64_t align_page_down(uint64_t value)
{
  return align_down(value, static_cast<uint64_t>(get_page_size()));
}

bool contains_string(const std::string& src, const std::string& target, bool is_sensitivity)
{
  if (target.empty()) return false;

  if (!is_sensitivity)
  {
    return std::search(src.begin(), src.end(), target.begin(), target.end(),
        [](char a, char b) 
      {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); 
      }
    ) != src.end();
  }
  else  
  {
    return src.find(target) != std::string::npos;
  }
}

std::string to_lower(std::string str) 
{
  std::transform(str.begin(), str.end(), str.begin(),
  [](unsigned char c) { return std::tolower(c); });
  return str;
}


std::vector<std::string> split_by_space(const std::string& s)
{
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream iss(s);
  while (iss >> token)
  {
    tokens.push_back(token);
  }
  return tokens;
}

}
