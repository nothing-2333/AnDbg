#include <cstring>
#include <sched.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <vector>

#include "debugger_core.hpp"
#include "log.hpp"
#include "utils.hpp"


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
    Utils::ptrace_wrapper(PTRACE_TRACEME, 0, nullptr, nullptr, 0);
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
    if (!Utils::waitpid_wrapper(pid, &status, 0)) return false;
      
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
  auto tids = Utils::get_thread_ids(pid);
  if (tids.empty()) return false;

  std::vector<pid_t> attached_tids;

  for (pid_t tid : tids)
  {
    if (Utils::ptrace_wrapper(PTRACE_ATTACH, tid, nullptr, nullptr, 0))
    {
      // 等待线程停止
      int status;
      if (!Utils::waitpid_wrapper(tid, &status, __WALL)) continue;

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
    if (Utils::ptrace_wrapper(PTRACE_DETACH, tid, nullptr, nullptr, 0))
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

  if (Utils::ptrace_wrapper(PTRACE_SINGLESTEP, tid, nullptr, nullptr, 0))
  {
    int status;
    if (Utils::waitpid_wrapper(tid, &status, __WALL))
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
    if (!Utils::ptrace_wrapper(PTRACE_CONT, tid, nullptr, nullptr, 0))
      LOG_WARNING("继续线程 " + std::to_string(tid) + " 失败");
    else success = true;
  }

  return success;

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

  return Utils::ptrace_wrapper(PTRACE_SETOPTIONS, pid, nullptr, 
    reinterpret_cast<void*>(ptrace_options), sizeof(long));
}

