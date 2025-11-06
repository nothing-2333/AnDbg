#pragma once

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <dirent.h>

#include "Log.hpp"


namespace Utils 
{
inline bool ptrace_wrapper(int request, pid_t pid, void *address=nullptr, void *data=nullptr)
{
  long int ret;

  if (request == PTRACE_GETREGSET || request == PTRACE_SETREGSET)
    ret = ptrace(request, static_cast<::pid_t>(pid), *(unsigned int *)address, data);
  else  
    ret = ptrace(request, static_cast<::pid_t>(pid), address, data);

  if (ret == -1) 
  {
    LOG_ERROR(std::string("ptrace 失败, request: ") + std::to_string(request));
    return false;
  }
  else  
  {
    LOG_DEBUG(std::string("ptrace 成功, request: ") + std::to_string(request));
    return true;
  }
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

