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


bool Debugger::launch(LaunchInfo& launch_info)
{
  Log* log = Log::get_instance();

  pid_t pid = fork();
  if (pid == -1) 
  {
    log->add(LogLevel::ERROR, std::string("fork 失败: ") + strerror(errno), true);
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

    log->add(LogLevel::ERROR, std::string("execve 失败: ") + strerror(errno), true);
    return false;
  }
  // 父进程
  else 
  {
    // 保存子进程 pid
    m_pid = pid;
    // 等待进程停止
    int status;
    int wpid = waitpid(pid, &status, 0);
    if (wpid == -1)
    {
      log->add(LogLevel::ERROR, std::string("waitpid 失败: ") + strerror(errno), true);
      return false;
    }
      
    if (WIFSTOPPED(status)) 
    {        
      // 设置跟踪选项
      return set_default_ptrace_options(pid);
    }
    return false;
  } 
}

bool Debugger::attach(pid_t pid)
{
  auto tids = get_thread_ids(pid);
  if (tids.empty()) return false;

  Log* log = Log::get_instance();
  std::vector<pid_t> attached_tids;

  for (pid_t tid : tids)
  {
    if (ptrace_wrapper(PTRACE_ATTACH, tid))
    {
      // 等待线程停止
      int status;
      int wpid = waitpid(tid, &status, __WALL);
      if (wpid == -1) 
      {
        log->add(LogLevel::WARNING, "等待线程 " + std::to_string(tid) + " 停止失败: " + strerror(errno));
        continue;
      }

      if (WIFSTOPPED(status))
      {
        if (set_default_ptrace_options(tid))
        {
          attached_tids.push_back(tid);
          log->add(LogLevel::DEBUG, "成功附加到线程 " + std::to_string(tid));
        }
        else  
          log->add(LogLevel::WARNING, "设置线程 " + std::to_string(tid) + " 的 ptrace 选项失败");
      }
      else  
        log->add(LogLevel::WARNING, "线程 " + std::to_string(tid) + " 未按预期停止");
    }
    else 
      log->add(LogLevel::WARNING, "附加到线程 " + std::to_string(tid) + " 失败");
  }

  return !attached_tids.empty();
}

bool Debugger::ptrace_wrapper(int request, pid_t pid, void *address, void *data)
{
  long int ret;
  Log* log = Log::get_instance();

  if (request == PTRACE_GETREGSET || request == PTRACE_SETREGSET)
    ret = ptrace(static_cast<__ptrace_request>(request), static_cast<::pid_t>(pid), *(unsigned int *)address, data);
  else  
    ret = ptrace(static_cast<__ptrace_request>(request), static_cast<::pid_t>(pid), address, data);

  if (ret == -1) 
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

std::vector<pid_t> Debugger::get_thread_ids(pid_t pid)
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