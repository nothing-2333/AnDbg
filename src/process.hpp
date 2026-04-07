#pragma once

#include "utils.hpp"
#include <dirent.h>
#include <fstream>
#include <optional>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>
#include "file.hpp"
#include <vector>
#include "singleton_base.hpp"


namespace Process
{

// /proc 文件类型枚举, 依据官方手册 https://www.man7.org/linux/man-pages/man5/proc.5.html
// 可能在 Android 不同的厂商上会有不同的实现, 或者在编译 linux 内核时未开启某些选项
// 这里的枚举只确保覆盖 linux 标准的 /proc 文件类型, 提供接口传入额外的字符串
enum class ProcFileType 
{
  ATTR, // 进程安全相关属性
  AUTOGROUP, // 内核自动分组相关
  CGROUP, // 显示该进程所属的 cgroup 控制组路径
  CLEAR_REFS, // 用于内存管理，标记是否需要 清除页引用计数
  CMDLINE, // 进程启动时的完整命令行参数
  COMM, // 进程的短名称
  COREDUMP_FILTER, // 控制 core dump 生成时包含哪些内存区域
  CPUSET, // 该进程绑定的 CPU 核心集合
  CWD, // 进程的当前工作目录的符号链接
  ENVIRON, // 进程的环境变量列表
  EXE, // 进程的可执行文件的符号链接
  FD, // 目录, 进程打开的文件描述符目录, 0 标准输入、1 标准输出、2 标准错误等
  FDINFO, // 进程打开的文件描述符的详细信息
  IO, // 进程的 I/O 统计信息
  LIMITS, // 进程的资源限制信息
  MAP_FILES, // 目录, 内存映射对应的文件列表, 每个文件对应一个内存映射区域, 文件内容是该内存区域的字节
  MAPS, // 进程的内存映射区域列表
  MEM, // 可直接读写进程的虚拟地址空间, 需要权限且通常只有调试器使用
  MOUNTINFO, // 详细挂载信息，比 mounts 更完整
  MOUNTS, // 进程的挂载信息
  MOUNTSTATS, // 进程的挂载统计信息
  NET, // 目录, 进程的网络连接信息, 展示进程的网络信息
  NS, // 目录, 进程所属的命名空间信息
  NUMA_MAPS, // NUMA 架构下的内存分布
  OOM_SCORE, // 进程被 OOM killer 选中杀死的分数，分数越高越容易被杀
  OOM_SCORE_ADJ, // 进程的 OOM score 调整值，范围 -1000 到 1000，值越低越不容易被杀
  PAGEMAP, // 用户态查看物理页帧号
  PERSONALITY, // 进程的执行域信息
  PROJID_MAP, // 项目 ID 映射
  ROOT, // 指向进程的根目录
  SECCOMP, // 进程的 seccomp 安全计算模式
  SETGROUPS, // 控制是否允许设置辅助组 ID,
  SMAPS, // 内存统计
  STACK, // 进程的内核栈信息
  STAT, // 进程核心状态信息, 包含 PID、状态、内存使用等
  STATM, // 更精简的内存状态
  STATUS, // 更易读的进程状态信息
  SYSCALL, // 当前正在执行 / 被阻塞的系统调用信息
  TASK, // 目录, 进程的线程信息, 每个线程一个子目录, 子目录名为线程 ID
  TIMERS, // 进程的定时器信息
  TIMERSLACK_NS, // 进程的定时器松弛时间
  UID_MAP, // 用户 ID 映射
  WCHAN, // 进程正在等待的内核函数
};

// 进程状态枚举
// 根据官方源码 https://elixir.bootlin.com/linux/v6.19.11/source/fs/proc/array.c (man7 手册有问题)
enum class ProcessState 
{
  UNKNOWN,        // 未知状态
  RUNNING,        // R (running)       运行/就绪
  SLEEPING,       // S (sleeping)      可中断睡眠
  DISK_SLEEP,     // D (disk sleep)    不可中断睡眠
  STOPPED,        // T (stopped)       停止
  TRACING_STOP,   // t (tracing stop)  跟踪停止
  DEAD,           // X (dead)          进程已死亡
  ZOMBIE,         // Z (zombie)        僵尸进程
  PARKED,         // P (parked)        内核暂停状态
  IDLE            // I (idle)          空闲内核线程
};

class PROCHelper : public SingletonBase<PROCHelper>
{
public:
  // 枚举转换
  const char process_state_to_char(ProcessState state);
  ProcessState char_to_process_state(const char state);
  std::string proc_file_type_to_string(ProcFileType type);

  // 回去进程所有 pid
  std::vector<pid_t> get_thread_ids(pid_t pid);

  // 用 uid 配合 /data/system/packages.list 重写
  // 通过包名查找 pid
  std::vector<pid_t> find_pid_by_package_name(const std::string& package_name);

  // 通过 pid 查找包名
  std::optional<std::string> find_package_name_by_pid(pid_t pid);

  // 解析 /proc/[pid]/status, 简单的用 ':' 分割, 然后去掉前后空格, 存入 unordered_map 返回
  std::unordered_map<std::string, std::string> parse_status(pid_t pid);

  // 解析 /proc/[pid]/status 的 state 字段
  char get_process_state_char(pid_t pid);
  ProcessState get_process_state(pid_t pid);
  

private:
  // 友元声明, 允许基类访问子类的私有构造函数
  friend class SingletonBase<PROCHelper>;
  PROCHelper() = default;
  ~PROCHelper() = default;

};

class PSHelper : public SingletonBase<PSHelper>
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
  std::vector<pid_t> find_pid_by_process_name(std::string name, MatchMode mode, bool is_sensitivity);

  // 通过 pid 查找进程名
  std::optional<std::string> find_process_name_by_pid(pid_t pid);

private:
  // 友元声明, 允许基类访问子类的私有构造函数
  friend class SingletonBase<PSHelper>;
  PSHelper() = default;
  ~PSHelper() = default;

  // 解析单一行
  bool parse_single_line(std::string line, PSItem& item);
};


}