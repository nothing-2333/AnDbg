#pragma once

#include "utils.hpp"
#include <dirent.h>
#include <fstream>
#include <optional>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace Process
{

enum class ProcFileType 
{
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

// 进程状态枚举
enum class ProcessState 
{
  UNKNOWN,    // 未知状态
  RUNNING,    // R: 运行中/就绪
  SLEEPING,   // S: 可中断睡眠
  IDLE,       // I: 空闲状态
  DISK_SLEEP, // D: 不可中断睡眠
  STOPPED,    // T: 暂停/跟踪停止
  ZOMBIE,     // Z: 僵尸进程
  DEAD,       // X: 死亡进程
  WAITING,    // W: 进程等待
  PARKED      // P: 进程驻留
};

const std::unordered_map<char, ProcessState> CHAR_TO_STATE = {
  {'R', ProcessState::RUNNING},
  {'r', ProcessState::RUNNING},
  {'S', ProcessState::SLEEPING},
  {'s', ProcessState::SLEEPING},
  {'I', ProcessState::IDLE},
  {'i', ProcessState::IDLE},
  {'D', ProcessState::DISK_SLEEP},
  {'d', ProcessState::DISK_SLEEP},
  {'T', ProcessState::STOPPED},
  {'t', ProcessState::STOPPED},
  {'Z', ProcessState::ZOMBIE},
  {'z', ProcessState::ZOMBIE},
  {'X', ProcessState::DEAD},
  {'x', ProcessState::DEAD},
  {'W', ProcessState::WAITING},
  {'w', ProcessState::WAITING},
  {'P', ProcessState::PARKED},
  {'p', ProcessState::PARKED},
};

const std::unordered_map<ProcessState, char> STATE_TO_CHAR = {
  {ProcessState::RUNNING,    'R'},
  {ProcessState::SLEEPING,   'S'},
  {ProcessState::IDLE,       'I'},
  {ProcessState::DISK_SLEEP, 'D'},
  {ProcessState::STOPPED,    'T'},
  {ProcessState::ZOMBIE,     'Z'},
  {ProcessState::DEAD,       'X'},
  {ProcessState::WAITING,    'W'},
  {ProcessState::PARKED,     'P'},
};

// 文件类型到文件名的映射
const std::unordered_map<ProcFileType, std::string> FILE_TYPE_TO_NAME = 
{
  // 进程基本信息
  {ProcFileType::STATUS, "status"},
  {ProcFileType::CMDLINE, "cmdline"},
  {ProcFileType::COMM, "comm"},
  {ProcFileType::EXE, "exe"},
  {ProcFileType::CWD, "cwd"},
  
  // 内存相关信息
  {ProcFileType::MAPS, "maps"},
  {ProcFileType::SMAPS, "smaps"},
  {ProcFileType::SMAPS_ROLLUP, "smaps_rollup"},
  {ProcFileType::STATM, "statm"},
  {ProcFileType::PAGEMAP, "pagemap"},
  {ProcFileType::CLEAR_REFS, "clear_refs"},
  
  // 线程和进程信息
  {ProcFileType::TASK, "task"},
  {ProcFileType::STAT, "stat"},
  
  // 文件系统信息
  {ProcFileType::FD, "fd"},
  {ProcFileType::MOUNTS, "mounts"},
  {ProcFileType::MOUNTINFO, "mountinfo"},
  {ProcFileType::MOUNTSTATS, "mountstats"},
  
  // IO 和调度信息
  {ProcFileType::IO, "io"},
  {ProcFileType::SCHED, "sched"},
  {ProcFileType::SCHEDSTAT, "schedstat"},
  
  // 系统调用和内核信息
  {ProcFileType::SYSCALL, "syscall"},
  {ProcFileType::WCHAN, "wchan"},
  {ProcFileType::STACK, "stack"},
  {ProcFileType::PERSONALITY, "personality"},
  
  // 资源限制和配置
  {ProcFileType::LIMITS, "limits"},
  {ProcFileType::OOM_SCORE, "oom_score"},
  {ProcFileType::OOM_ADJ, "oom_adj"},
  {ProcFileType::OOM_SCORE_ADJ, "oom_score_adj"},
  
  // 命名空间和容器
  {ProcFileType::CGROUP, "cgroup"},
  {ProcFileType::NS, "ns"},
  {ProcFileType::UID_MAP, "uid_map"},
  {ProcFileType::GID_MAP, "gid_map"},
  {ProcFileType::AUTOGROUP, "autogroup"},
  
  // 其他信息
  {ProcFileType::ENVIRON, "environ"},
  {ProcFileType::AUXV, "auxv"},
  {ProcFileType::TIMERS, "timers"},
  {ProcFileType::TIMERSLACK_NS, "timerslack_ns"},
  {ProcFileType::SESSIONID, "sessionid"},
  {ProcFileType::LOGINUID, "loginuid"},

  // 网络信息
  {ProcFileType::NET, "net"},
};

// 目录类型的集合
const std::unordered_set<ProcFileType> DIRECTORY_TYPES = 
{
  ProcFileType::TASK,
  ProcFileType::FD,
  ProcFileType::NET,
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

  ~ProcFile() = default;

  // 打开 /proc 文件
  static std::optional<ProcFile> open(pid_t pid, ProcFileType type);
  static std::optional<ProcFile> open(const std::string& path = "/proc/");

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
  ProcFile(const std::string& path);
  ProcFile(const std::string& path, bool is_directory);
  
  // 获取文件类型对应的文件名
  static std::string get_filename(ProcFileType type);
  
  // 检查是否是目录类型
  static bool is_directory_type(ProcFileType type);
  bool check_directory_type(const std::string& path);

  // 通过路径填充私有变量
  void open_path(const std::string& path, bool is_directory);
  
  // 构建完整路径
  static std::string build_path(pid_t pid, ProcFileType type);
};

// 回去进程所有 pid
std::vector<pid_t> get_thread_ids(pid_t pid);

// 查找 app 进程的 pid
std::vector<pid_t> find_app_process(const std::string& package_name);

// 枚举转换
const char process_state_to_char(ProcessState state);
ProcessState char_to_process_state(const char state);

// 解析 /proc/[pid]/status 的 state 字段
ProcessState parse_process_state(pid_t pid);

class PSHelper
{
public:
  struct PSItem
  {
    std::string user;           // USER: 进程所属用户
    pid_t pid = -1;             // PID: 进程 ID
    pid_t ppid = -1;            // PPID: 父进程 ID
    uint64_t vsz = 0;           // VSZ: 虚拟内存大小(KB)
    uint64_t rss = 0;           // RSS: 物理内存占用(KB)
    std::string wchan;          // WCHAN: 等待的内核函数
    std::string addr;           // ADDR: 进程内存地址
    ProcessState state;         // S: 状态
    std::string name;           // args: 进程名
  };

  enum class MatchMode 
  {
    CONTAIN,        // 包含匹配
    EXACT,          // 严格匹配
  };

  // 获取所有 ps 中的信息
  std::vector<PSItem> get_items();

  // 通过进程名查找 pid
  std::vector<pid_t> find_pid_by_name(std::string name, MatchMode mode, bool is_sensitivity);

private:
  // 解析单一行
  bool parse_single_line(std::string line, PSItem& item);
};


}