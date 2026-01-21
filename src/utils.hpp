#pragma once

#include <cstddef>
#include <cstdio>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>

#include "log.hpp"


namespace Utils 
{

// ptrace 包装
inline bool ptrace_wrapper(int request, pid_t pid, void *address, void* data, size_t data_size, long* result = nullptr)
{
  // 参数检查
  if (pid == -1)
  {
    LOG_ERROR("传入无效的 pid");
    return false;
  }

  long int ret = 0;

  // 重置 errno
  errno = 0;

  if (request == PTRACE_GETREGSET || request == PTRACE_SETREGSET)
  {
    if (address == nullptr) 
    {
      LOG_ERROR("PTRACE_GETREGSET/SETREGSET 传入的 address 不能为空");
      ret = -1;
    }
    else  
    {
      ret = ptrace(request, static_cast<::pid_t>(pid), *reinterpret_cast<unsigned int*>(address), data);
    }
  }
  else  
    ret = ptrace(request, static_cast<::pid_t>(pid), address, data);

  // 记录日志
  LOG_DEBUG("ptrace(request: {}, pid: {}, address: {:p}, data: {:p}, data_size: {}, ret: 0x{:x})",
    request, pid, static_cast<const void*>(address), static_cast<const void*>(data), data_size, ret
  );

  // 如果有返回值保存返回值
  if (result != nullptr) *result = ret;

  
  if (ret == -1 && errno != 0) 
  {
    LOG_ERROR("ptrace 调用失败, errno: {}, 错误信息: {}", errno, strerror(errno));
    return false;
  }
  else return true;;
}

// waitpid 包装
inline bool waitpid_wrapper(pid_t pid, int* status, int __options)
{
  pid_t wpid = waitpid(pid, status, __options);

  LOG_DEBUG("等待进程 pid: {} 完成, 返回值: {}", pid, wpid);

  if (wpid == -1) 
  {
    LOG_ERROR("停止失败: {}", std::string(strerror(errno)));
    return false;
  }
  return true;
}

// 回去进程所有 pid
inline std::vector<pid_t> get_thread_ids(pid_t pid)
{
  std::vector<pid_t> tids;
  std::string task_path = "/proc/" + std::to_string(pid) + "/task";

  DIR* dir = opendir(task_path.c_str());
  if (!dir) return tids;

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) 
  {
    if (entry->d_type ==DT_DIR)
    {
      // 检查目录是否全为数字
      bool is_numeric = true;
      for (int i = 0; entry->d_name[i] != '\0'; ++i)
      {
        if (!isdigit(entry->d_name[i]))
        {
          is_numeric = false;
          break;
        }
      }

      if (is_numeric && strlen(entry->d_name) > 0)
      {
        tids.push_back(static_cast<pid_t>(std::stoi(entry->d_name)));
      }
    }
  }
  closedir(dir);

  // 编译器会自动进行 RVO(返回值优化), 加不加 std::move 都行
  return std::move(tids); 
}

inline long get_page_size()
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
inline uint64_t align_up(uint64_t value, uint64_t alignment)
{
  return (value + alignment - 1) & ~(alignment - 1);
}

inline uint64_t align_down(uint64_t value, uint64_t alignment)
{
  return value & ~(alignment - 1);
}

// 页对齐
inline uint64_t align_page_up(uint64_t value)
{
  return align_up(value, static_cast<uint64_t>(get_page_size()));
}

inline uint64_t align_page_down(uint64_t value)
{
  return align_down(value, static_cast<uint64_t>(get_page_size()));
}

// 判断是否是 sigtrap 信号, 在调试时, ptrace 相关操作通常会触发 SIGTRAP 信号
inline bool is_sigtrap(int status)
{
  return WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP;
}

// 等待 sigtrap 信号
inline bool waitpid_sigtrap(pid_t pid)
{
  int status = 0;
  if (!Utils::waitpid_wrapper(pid, &status, WUNTRACED))
    return false;
  return is_sigtrap(status);
}

// 用 ptrace 执行 syscall 指令
inline bool syscall_wrapper(pid_t pid)
{
  // 第一次 PTRACE_SYSCALL: 触发进程进入系统调用
  if (!Utils::ptrace_wrapper(PTRACE_SYSCALL, pid, nullptr, nullptr, 0))
  {
    LOG_ERROR("进程 {}: 第一次 PTRACE_SYSCALL 失败", pid);
    return false;
  }
  // 等待进程暂停
  if (!waitpid_sigtrap(pid))
  {
    LOG_ERROR("进程 {}: 等待第一次暂停失败", pid);
    return false;
  }

  // 第二次 PTRACE_SYSCALL: 触发进程退出系统调用
  if (!Utils::ptrace_wrapper(PTRACE_SYSCALL, pid, nullptr, nullptr, 0))
  {
    LOG_ERROR("进程 {}: 第二次 PTRACE_SYSCALL 失败", pid);
    return false;
  }
  // 等待进程暂停
  if (!waitpid_sigtrap(pid))
  {
    LOG_ERROR("进程 {}: 等待第二次暂停失败", pid);
    return false;
  }

  return true;
}

}

