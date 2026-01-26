#pragma once

#include "log.hpp"
#include <sched.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
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

  // 注入 ELF
  bool inject_elf();

  // 获取相关内存布局
  bool get_memory_layout();

  // 解析符号
  bool resolve_symbol();

};

// launch 方法的参数处理类
class LaunchInfo
{
private:
  // 存储
  std::string path_;                // 路径
  std::vector<std::string> args_;   // 参数
  std::vector<std::string> env_;    // 环境

  std::string package_name_;        // 包名
  std::string main_activity_;       // 主 Activity

  // 缓冲区: 存储转换后的 char* 指针(避免重复分配，mutable 允许 const 方法修改)
  mutable std::vector<char*> argv_buffer_;  // 用于 get_argv()
  mutable std::vector<char*> envp_buffer_;  // 用于 get_envp()
          
public:
  enum class LaunchMode
  {
    BINARY,   // 模式1：执行 Linux 二进制可执行文件
    APP       // 模式2：包名启动 APP
  };

  LaunchMode mode;

public:
  LaunchInfo(std::string&& path, std::vector<std::string>&& args, std::vector<std::string>&& env={})
    : path_(std::move(path)), args_(std::move(args)), env_(std::move(env)), mode(LaunchMode::BINARY) {};
  LaunchInfo(std::string&& package_name, std::string&& main_activity)
   : package_name_(std::move(package_name)), main_activity_(std::move(main_activity)), mode(LaunchMode::APP) {}
  ~LaunchInfo() = default;

  explicit LaunchInfo(std::string&& android_target)
  {
    mode = LaunchMode::APP;

    auto split_pos = android_target.find('/');
    if (split_pos != std::string::npos)
    {
      package_name_ = android_target.substr(0, split_pos);
      main_activity_ = android_target.substr(split_pos); // 保留开头的/, 适配am start格式
    }
    else
    {
      package_name_ = std::move(android_target);
      LOG_WARNING("未发现包名分隔符, Activity 留空");
    }
  }

  char const *get_path() const { return path_.c_str(); };
  
  // 新程序的命令行参数 {"程序的路径", "参数1", "参数2", ..., NULL}
  char *const *get_argv() const
  { 
    argv_buffer_.clear();
    for (const auto& arg : args_)
    {
      argv_buffer_.push_back(const_cast<char*>(arg.c_str()));
    }
    argv_buffer_.push_back(nullptr);
    return argv_buffer_.data();
  }        

  // 新程序的环境变量 {"KEY=VALUE", ..., NULL}
  char *const *get_envp() const
  {
    // 若未提供自定义环境变量, 使用当前进程的环境变量
    if (env_.empty()) 
    {
        extern char** environ;  // 声明全局环境变量数组
        return environ;
    }

    envp_buffer_.clear();
    for (const auto& e : env_) 
    {
        envp_buffer_.push_back(const_cast<char*>(e.c_str()));
    }
    envp_buffer_.push_back(nullptr);
    return envp_buffer_.data();
  }

  // 获取 APP 包名
  const std::string& get_package_name() const { return package_name_; }
  // 获取 APP 主 Activity
  const std::string& get_main_activity() const { return main_activity_; }

  std::string get_am_cmd(std::unordered_map<std::string, std::string> extra={}) const
  {
    if (package_name_.empty() || mode != LaunchMode::APP) return "";

    std::ostringstream oss;
    oss << "am start -D -n " << package_name_ << main_activity_
      << " -a android.intent.action.MAIN"
      << " -c android.intent.category.LAUNCHER";

    for (const auto& [ key, value ] : extra)
    {
      if (value.empty())
        oss << " " << key;
      else 
        oss << " " << key << " " << value;
    }

    return oss.str();
  }
};