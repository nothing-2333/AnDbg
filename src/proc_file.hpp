#pragma once

#include "fmt/format.h"
#include "log.hpp"
#include <dirent.h>
#include <fstream>
#include <sys/types.h>

enum class ProcFileType {
  // ==================== 进程基本信息 ====================
  STATUS,         // 进程状态信息
  CMDLINE,        // 命令行参数
  COMM,           // 进程名（短名称）
  EXE,            // 可执行文件符号链接
  CWD,            // 当前工作目录符号链接
  
  // ==================== 内存相关信息 ====================
  MAPS,           // 内存映射区域
  SMAPS,          // 详细内存映射（带统计）
  SMAPS_ROLLUP,   // 汇总内存映射统计
  STATM,          // 内存使用统计
  PAGEMAP,        // 虚拟页到物理页映射
  CLEAR_REFS,     // 清除引用位（可写文件）
  
  // ==================== 线程和进程信息 ====================
  TASK,       // 线程目录（需用目录方式打开）
  STAT,           // 进程统计信息
  
  // ==================== 文件系统信息 ====================
  FD,         // 文件描述符目录
  MOUNTS,         // 挂载点信息
  MOUNTINFO,      // 挂载详细信息
  MOUNTSTATS,     // 挂载统计信息
  
  // ==================== IO 和调度信息 ====================
  IO,             // IO 统计信息
  SCHED,          // 调度信息
  SCHEDSTAT,      // 调度统计
  
  // ==================== 系统调用和内核信息 ====================
  SYSCALL,        // 系统调用信息
  WCHAN,          // 进程等待的内核函数
  STACK,          // 内核栈信息
  PERSONALITY,    // 进程执行域
  
  // ==================== 资源限制和配置 ====================
  LIMITS,         // 资源限制
  OOM_SCORE,      // OOM 杀手评分
  OOM_ADJ,        // OOM 调整值
  OOM_SCORE_ADJ,  // OOM 评分调整
  
  // ==================== 命名空间和容器 ====================
  CGROUP,         // Cgroup 信息
  NS,             // 命名空间信息
  UID_MAP,        // UID 映射
  GID_MAP,        // GID 映射
  AUTOGROUP,      // 自动进程组
  
  // ==================== 其他信息 ====================
  ENVIRON,        // 环境变量
  AUXV,           // 辅助向量
  TIMERS,         // 定时器信息
  TIMERSLACK_NS,  // 定时器松弛纳秒值
  SESSIONID,      // 会话ID
  LOGINUID,       // 登录用户ID
  
  // ==================== 网络信息 ====================
  NET,        // 网络命名空间信息目录
};


class ProcFile
{
private:
  std::string m_path;      // 文件/目录路径
  bool m_is_directory;     // 是否是目录
  
  // 文件句柄
  std::ifstream m_file_stream;
  
  // 目录句柄
  std::unique_ptr<DIR, decltype(&closedir)> m_dir_handle;

public:
  // 禁止拷贝
  ProcFile(const ProcFile&) = delete;
  ProcFile& operator=(const ProcFile&) = delete;
  
  // 允许移动
  ProcFile(ProcFile&& other) noexcept;
  ProcFile& operator=(ProcFile&& other) noexcept;
  
  ~ProcFile();

  // 打开 /proc 文件
  static std::optional<ProcFile> open(pid_t pid, ProcFileType type);

  // 检查文件/目录是否成功打开
  bool is_open() const;

  // 检查是否是目录
  bool is_directory() const;

  // 获取文件路径
  const std::string& path() const { return m_path; }

  // 读取文件全部内容
  std::string read_all();

  // 读取文件所有行
  std::vector<std::string> read_lines();

  // 读取一行
  std::string read_line();

  // 获取原始文件流, 仅对文件有效
  std::ifstream& file_stream();

  // 列出目录所有条目
  std::vector<dirent*> list_entries();

  // 获取原始目录句柄, 仅对目录有效
  DIR* directory_handle();

private:
  ProcFile(const std::string& path, bool is_directory);
  
  // 获取文件类型对应的文件名
  static std::string get_filename(ProcFileType type);
  
  // 检查是否是目录类型
  static bool is_directory_type(ProcFileType type);
  
  // 构建完整路径
  static std::string build_path(pid_t pid, ProcFileType type);

};