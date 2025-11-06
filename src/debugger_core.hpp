#pragma once

#include <sched.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

class LaunchInfo;

class DebuggerCore
{
private:
  pid_t m_pid;
  std::vector<pid_t> m_tids;

  // 设置默认 ptrace 调试选项
  bool set_default_ptrace_options(pid_t pid);

public:
  DebuggerCore();

  // 启动
  bool launch(LaunchInfo& launch_info);

  // 附加
  bool attach(pid_t pid);

  // 分离
  bool detach();

  // 单步步入
  bool step_into(pid_t tid = -1);

  // 单步步过
  bool step_over(pid_t tid = -1);

  // 执行(直到断点或结束)
  bool run();

  // 设置断点
  bool set_breakpoint();

  // 移除断点
  bool remove_breakpoint();

  // 读取内存
  bool read_memory();

  // 写入内存
  bool write_memory();

  // 读取寄存器
  bool read_register();

  // 写入寄存器
  bool write_register();

  // 注入 so

  // 获取相关内存布局

  // 解析 elf 文件

};

// launch 方法的参数处理类
class LaunchInfo
{
  // 存储
  std::string path;               // 路径
  std::vector<std::string> args;  // 参数
  std::vector<std::string> env;   // 环境

    // 缓冲区: 存储转换后的 char* 指针(避免重复分配，mutable 允许 const 方法修改)
    mutable std::vector<char*> argv_buffer;  // 用于 get_argv()
    mutable std::vector<char*> envp_buffer;  // 用于 get_envp()
          
public:
  LaunchInfo(std::string&& path_, std::vector<std::string>&& args_) : path(std::move(path_)), args(std::move(args_)) {};
  LaunchInfo(std::string&& path_, std::vector<std::string>&& args_, std::vector<std::string>&& env_)
    : path(std::move(path_)), args(std::move(args_)), env(std::move(env_)) {};
  ~LaunchInfo() {};

  char const *get_path() const { return path.c_str(); };
  // 新程序的命令行参数 {"程序的路径", "参数1", "参数2", ..., NULL}
  char *const *get_argv() const
  { 
    argv_buffer.clear();
    for (const auto& arg : args)
    {
      argv_buffer.push_back(const_cast<char*>(arg.c_str()));
    }
    argv_buffer.push_back(nullptr);
    return argv_buffer.data();
  }        
  // 新程序的环境变量 {"KEY=VALUE", ..., NULL}
  char *const *get_envp() const
  {
    // 若未提供自定义环境变量, 使用当前进程的环境变量
    if (env.empty()) 
    {
        extern char** environ;  // 声明全局环境变量数组
        return environ;
    }

    envp_buffer.clear();
    for (const auto& e : env) 
    {
        envp_buffer.push_back(const_cast<char*>(e.c_str()));
    }
    envp_buffer.push_back(nullptr);
    return envp_buffer.data();
  }
};