#pragma once

#include <cstddef>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <dirent.h>

#include "Log.hpp"


namespace Utils 
{
inline bool ptrace_wrapper(int request, pid_t pid, void *address, void* data, size_t data_size, long* result = nullptr)
{
  long int ret = 0;

  // // 重置 errno
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

inline bool waitpid_wrapper(pid_t pid, int* status, int __options)
{
  int wpid = waitpid(pid, status, __options);
  if (wpid != pid) 
  {
    LOG_ERROR("等待线程 " + std::to_string(pid) + " 停止失败: " + strerror(errno));
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
}

