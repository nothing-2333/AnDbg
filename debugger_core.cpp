#include <string>
#include <sys/types.h>

#include "debugger_core.hpp"
#include "Log.hpp"



bool Debugger::launch(LaunchInfo& launch_info)
{
  pid_t pid = fork();

  // 子进程
  if (pid == 0)
  {
    ptrace_wrapper(PTRACE_TRACEME, 0, nullptr, nullptr);
    // 若执行成功, 当前进程会被完全替换, 后续代码不会执行; 若失败则进入下方错误处理
    execve(launch_info.get_path(), launch_info.get_argv(), launch_info.get_envp());

    if (errno == ETXTBSY)
    {
      // 可执行文件被其他进程占用, 等待 50ms, 再次尝试执行
      usleep(50000);
      execve(launch_info.get_path(), launch_info.get_argv(), launch_info.get_envp());
    }

    Log::get_instance()->add(LogLevel::ERROR, "execve 失败", true);
  }
  // 父进程
  else 
  {
    // 保存子进程 pid
    m_pid = pid;
    // 等待进程停止
    int status;
    waitpid(pid, &status, 0);
    if (WIFSTOPPED(status)) 
    {        
      // 设置跟踪选项
      return set_default_ptrace_options(pid);
    }
    return false;
  } 
}

bool Debugger::ptrace_wrapper(int request, pid_t pid, void *address, void *data)
{
  long int ret;
  Log* log = Log::get_instance();

  if (request == PTRACE_GETREGSET || request == PTRACE_SETREGSET)
    ret = ptrace(static_cast<__ptrace_request>(request), static_cast<::pid_t>(pid), *(unsigned int *)address, data);
  else  
    ret = ptrace(static_cast<__ptrace_request>(request), static_cast<::pid_t>(pid), address, data);

  if (ret) 
  {
    log->add(LogLevel::WARNING, std::string("ptrace 失败, request: ") + std::to_string(request));
    return false;
  }
  else  
  {
    log->add(LogLevel::DEBUG, std::string("ptrace 成功, request: ") + std::to_string(request));
    return true;
  }
}

bool Debugger::set_default_ptrace_options(pid_t pid)
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
