#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <vector>
#include <dirent.h>

#include "debugger_core.hpp"
#include "Log.hpp"


DebuggerCore::DebuggerCore()
{
  m_pid = -1;
  m_tids.clear();
}

bool DebuggerCore::launch(LaunchInfo& launch_info)
{
  pid_t pid = fork();
  if (pid == -1) 
  {
    LOG_ERROR(std::string("fork 失败: ") + strerror(errno));
    return false;
  }
  // 子进程
  else if (pid == 0)
  {
    ptrace_wrapper(PTRACE_TRACEME, 0);
    // 若执行成功, 当前进程会被完全替换, 后续代码不会执行; 若失败则进入下方错误处理
    execve(launch_info.get_path(), launch_info.get_argv(), launch_info.get_envp());

    if (errno == ETXTBSY)
    {
      // 可执行文件被其他进程占用, 等待 50ms, 再次尝试执行
      usleep(50000);
      execve(launch_info.get_path(), launch_info.get_argv(), launch_info.get_envp());
    }

    LOG_ERROR(std::string("execve 失败: ") + strerror(errno));
    return false;
  }
  // 父进程
  else 
  {
    // 等待进程停止
    int status;
    if (!waitpid_wrapper(pid, &status, 0)) return false;
      
    if (WIFSTOPPED(status)) 
    {        
      // 设置跟踪选项
      if (set_default_ptrace_options(pid))
      {
        // 保存
        m_pid = pid;
        m_tids = { pid };
        return true;
      }
      else return false;
    }
    return false;
  } 
}

bool DebuggerCore::attach(pid_t pid)
{
  auto tids = get_thread_ids(pid);
  if (tids.empty()) return false;

  std::vector<pid_t> attached_tids;

  for (pid_t tid : tids)
  {
    if (ptrace_wrapper(PTRACE_ATTACH, tid))
    {
      // 等待线程停止
      int status;
      if (!waitpid_wrapper(tid, &status, __WALL)) continue;

      if (WIFSTOPPED(status))
      {
        if (set_default_ptrace_options(tid))
        {
          attached_tids.push_back(tid);
          LOG_DEBUG("成功附加到线程 " + std::to_string(tid));
        }
      }
    }
    else 
      LOG_WARNING("附加到线程 " + std::to_string(tid) + " 失败");
  }

  // 有一个附加成功就返回成功
  if (!attached_tids.empty()) 
  {
    // 保存
    m_pid = pid;
    m_tids = attached_tids;
    return true;
  }
  else return false;
}

bool DebuggerCore::detach()
{
  LOG_DEBUG("开始分离调试器, PID: " + std::to_string(m_pid) + ", 线程数: " + std::to_string(m_tids.size()));

  bool all_success = true;
  int success_count = 0;

  for (pid_t tid : m_tids)
  {
    if (ptrace_wrapper(PTRACE_DETACH, tid, nullptr, (void*)0))
      success_count++;
    else  
    {
      LOG_WARNING("分离线程 " + std::to_string(tid) + " 失败");
      all_success = false;
    }
  }

  if (all_success)
  {
    LOG_DEBUG("成功分离所有线程");
    m_pid = -1;
    m_tids.clear();
  }
  else 
    LOG_WARNING("部分线程分离失败, 成功: " + std::to_string(success_count) + "/" + std::to_string(m_tids.size()));

  return all_success;
}

bool DebuggerCore::step_into(pid_t tid)
{
  if (m_pid == -1) 
  {
    LOG_ERROR("没有被调试的程序");
    return false;
  }

  // 默认运行主线程
  if (tid == -1) tid = m_pid;

  if (ptrace_wrapper(PTRACE_SINGLESTEP, tid, nullptr, nullptr))
  {
    int status;
    if (waitpid_wrapper(tid, &status, __WALL))
    {
      if (WIFSTOPPED(status))
      {
        return true;
      }
    }
  }

  return false;
}

bool DebuggerCore::step_over(pid_t tid)
{

}

bool DebuggerCore::run()
{
  if (m_tids.empty()) 
  {
    LOG_ERROR("没有被调试的进程");
    return false;
  }

  bool success = false;

  for (pid_t tid : m_tids) 
  {
    if (!ptrace_wrapper(PTRACE_CONT, tid))
      LOG_WARNING("继续线程 " + std::to_string(tid) + " 失败");
    else success = true;
  }

  return success;

}

bool DebuggerCore::ptrace_wrapper(int request, pid_t pid, void *address, void *data)
{
  long int ret;

  if (request == PTRACE_GETREGSET || request == PTRACE_SETREGSET)
    ret = ptrace(static_cast<__ptrace_request>(request), static_cast<::pid_t>(pid), *(unsigned int *)address, data);
  else  
    ret = ptrace(static_cast<__ptrace_request>(request), static_cast<::pid_t>(pid), address, data);

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

bool DebuggerCore::waitpid_wrapper(pid_t pid, int* status, int __options)
{
  int wpid = waitpid(pid, status, __options);
  if (wpid != pid) 
  {
    LOG_ERROR("等待线程 " + std::to_string(pid) + " 停止失败: " + strerror(errno));
    return false;
  }
  return true;
}

bool DebuggerCore::set_default_ptrace_options(pid_t pid)
{
  long ptrace_options = 0;
  // 跟踪进程退出事件: 被调试进程退出时会暂停, 调试器可获取返回码, 信号等
  ptrace_options |= PTRACE_O_TRACEEXIT;
  // 跟踪 clone() 事件, 被调试进程调用 clone() 创建线程或轻量级进程时会暂停, 调试器可获取新线程/进程的 pid
  ptrace_options |= PTRACE_O_TRACECLONE;
  // 跟踪 execve() 事件, 被调试进程执行 execve() 替换程序时会暂停, 新程序加载后但未执行前
  ptrace_options |= PTRACE_O_TRACEEXEC;
  // 跟踪 fork() 事件, 被调试进程调用 fork() 时会暂停, 调试器可通过 PTRACE_GETEVENTMSG 获取新子进程的 pid
  ptrace_options |= PTRACE_O_TRACEFORK;
  // 跟踪 vfork() 事件, 类似 TRACEFORK, 但针对 vfork(), 相比 fork(), vfork() 会暂停父进程直到子进程 exec 或退出
  ptrace_options |= PTRACE_O_TRACEVFORK;
  // 跟踪 vfork() 完成事件, vfork() 创建的子进程执行 exec 或退出后, 父进程恢复前会暂停
  ptrace_options |= PTRACE_O_TRACEVFORKDONE;

  return ptrace_wrapper(PTRACE_SETOPTIONS, pid, nullptr, (void*)ptrace_options);
}

std::vector<pid_t> DebuggerCore::get_thread_ids(pid_t pid)
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