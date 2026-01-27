#include <cstring>
#include <sched.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <vector>

#include "debugger_core.hpp"
#include "log.hpp"
#include "utils.hpp"
#include "proc_file.hpp"


DebuggerCore::DebuggerCore()
{
  m_pid = -1;
  m_tids.clear();
}

bool DebuggerCore::child_process_execute(LaunchInfo& launch_info)
{
  if (launch_info.mode == LaunchInfo::LaunchMode::BINARY)
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

    LOG_ERROR("execve 失败: {}", strerror(errno));
    return false;
  }
  else if (launch_info.mode == LaunchInfo::LaunchMode::APP) 
  {
    // 生成 am start --D ... 命令
    std::string am_cmd = launch_info.get_am_cmd();
    if (am_cmd.empty())
    {
      LOG_ERROR("生成 am 启动命令失败, 包名或 Activity 为空");
      return false;
    }
    LOG_DEBUG(std::string("子进程执行 am 命令: ") + am_cmd);
    execl("/system/bin/sh", "sh", "-c", am_cmd.c_str(), (char*)NULL);

    LOG_ERROR("execl 执行 am 命令失败: {}", strerror(errno) );
    return false;
  }
  else 
  {
    LOG_ERROR("未知的启动模式");
    return false;
  }
}

bool DebuggerCore::parent_process_execute(pid_t pid, LaunchInfo& launch_info)
{
  if (launch_info.mode == LaunchInfo::LaunchMode::BINARY)
  {
    // 等待进程停止
    int status;
    if (!Utils::waitpid_wrapper(pid, &status, 0))
    {
      LOG_ERROR("等待子进程失败");
      return false;
    }
    
    if (!WIFSTOPPED(status))
    {
      LOG_ERROR("子进程未按预期停止");
      return false;
    }
    LOG_DEBUG("子进程已停止，信号: {}", WSTOPSIG(status));

    // 设置ptrace选项
    if (!set_default_ptrace_options(pid))
    {
      LOG_ERROR("设置 ptrace 选项失败");
      return false;
    }

    // 获取所有线程
    auto tids = ProcHelper::get_thread_ids(pid);
    if (tids.empty())
    {
      LOG_WARNING("无法获取线程列表, 使用主线程");
      tids = { pid };
    }
    
    // 保存状态
    m_pid = pid;
    m_tids = tids;
    
    LOG_DEBUG("成功启动二进制调试, PID: {}, 线程数: {}", pid, tids.size());
    return true;
  }
  else if (launch_info.mode == LaunchInfo::LaunchMode::APP) 
  {
    int max_retries = 20;  // 最多尝试 20 次
    int retry_count = 0;

    // 非阻塞检查子进程是否退出
    int status;
    pid_t result;
    do 
    {
      usleep(100000);  // 100ms
      result = Utils::waitpid_wrapper(pid, &status, WNOHANG);
      retry_count++;
    } while (result != pid && retry_count < max_retries);

    if (result == 0 || result != pid)
    {
      LOG_WARNING("壳进程执行超时, 强制终止");
      kill(pid, SIGKILL);
      Utils::waitpid_wrapper(pid, &status, 0);
    }

    LOG_DEBUG("子进程处理完成");
  
    // 查找被 -D 暂停的目标应用进程
    std::string package_name = launch_info.get_package_name();
    if (package_name.empty())
    {
      LOG_ERROR("包名为空");
      return false;
    }

    // 查找目标 APP 进程
    pid_t app_pid = -1;
    max_retries = 10;
    retry_count = 0;

    while (retry_count < max_retries && app_pid == -1)
    {
      usleep(100000);  // 等待100ms
      
      // 通过包名查找进程
      std::vector<pid_t> maybe_pids = ProcHelper::find_app_process(package_name);
      for (const auto& maybe_pid : maybe_pids)
      {
        // 检查是否处于暂停状态（-D 的效果）
        if (ProcHelper::parse_process_state(maybe_pid) == ProcHelper::ProcessState::STOPPED)
        {
          LOG_DEBUG("找到被 -D 暂停的应用进程: {}", maybe_pid);
          app_pid = maybe_pid;
          break;
        }
        else
        {
          LOG_DEBUG("应用进程 {} 已启动但未暂停，可能 -D 未生效", maybe_pid);
        }
      }
      
      usleep(100000);  // 100 ms
      retry_count++;
    }

    if (app_pid == -1)
    {
      LOG_ERROR("内未找到应用进程: {}", package_name);
      return false;
    }
    LOG_DEBUG("找到应用进程, PID: {}", app_pid);

    // 附加到目标进程
    if (!attach(app_pid))
    {
      LOG_ERROR("附加到应用进程失败");
      return false;
    }

    LOG_DEBUG("成功附加到被 -D 暂停的应用, PID: {}, 包名: {}", app_pid, package_name);
    return true;
  }
  else  
  {
    LOG_ERROR("未知的启动模式");
    return false;
  }
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
    return child_process_execute(launch_info);
  }
  // 父进程
  else 
  {
    return parent_process_execute(pid, launch_info);
  } 
}

bool DebuggerCore::attach(pid_t pid)
{
  auto tids = ProcHelper::get_thread_ids(pid);
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

