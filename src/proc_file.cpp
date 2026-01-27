#include <cstddef>
#include <unordered_set>
#include <sys/stat.h>
#include <filesystem>
#include <algorithm>

#include "proc_file.hpp"
#include "log.hpp"
#include "utils.hpp"

namespace 
{
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
}

std::string ProcFile::get_filename(ProcFileType type) 
{
  auto it = FILE_TYPE_TO_NAME.find(type);
  if (it == FILE_TYPE_TO_NAME.end()) 
  {
    LOG_ERROR("未知的 /proc 文件类型: {}", static_cast<int>(type));
    return "";
  }
  return it->second;
}

bool ProcFile::is_directory_type(ProcFileType type) 
{
  return DIRECTORY_TYPES.find(type) != DIRECTORY_TYPES.end();
}

bool ProcFile::check_directory_type(const std::string& path)
{
  std::error_code ec;
  return std::filesystem::is_directory(path, ec);
}

std::string ProcFile::build_path(pid_t pid, ProcFileType type) 
{
  if (pid <= 0) 
  {
    LOG_ERROR("无效的进程ID: {}", pid);
    return "";
  }
    
  std::string filename = get_filename(type);
  if (filename.empty()) return "";

  return fmt::format("/proc/{}/{}", pid, filename);
}

std::optional<ProcFile> ProcFile::open(pid_t pid, ProcFileType type) 
{
  std::string path = build_path(pid, type);
  if (path.empty()) return std::nullopt;
  
  ProcFile proc_file(path, is_directory_type(type));
  if (!proc_file.is_open()) return std::nullopt;
  return proc_file;
}

std::optional<ProcFile> ProcFile::open(const std::string& path)
{
  ProcFile proc_file(path);
  if (!proc_file.is_open()) return std::nullopt;
  return proc_file;
}

void ProcFile::open_path(const std::string& path, bool is_directory)
{
  if (m_is_directory)
  {
    DIR* dir = opendir(path.c_str());
    if (!dir) 
    {
        LOG_ERROR("无法打开目录 {}: {}", path, strerror(errno));
        return;
    }
    m_dir_handle.reset(dir);
  }
  else 
  {
    m_file_stream.open(path);
    if (!m_file_stream.is_open())
    {
      LOG_ERROR("无法打开文件 {}: {}", path, strerror(errno));
      return;
    }
  }
}

ProcFile::ProcFile(const std::string& path, bool is_directory) : m_path(path), m_is_directory(is_directory), m_dir_handle(nullptr, closedir)
{
  open_path(path, is_directory);
}

ProcFile::ProcFile(const std::string& path) : m_path(path), m_is_directory(check_directory_type(path)), m_dir_handle(nullptr, closedir)
{
  open_path(path, m_is_directory);
}

ProcFile::ProcFile(ProcFile&& other) noexcept
  : m_path(std::move(other.m_path))
  , m_is_directory(other.m_is_directory)
  , m_file_stream(std::move(other.m_file_stream))
  , m_dir_handle(std::move(other.m_dir_handle))
{
  other.m_is_directory = false;
}

ProcFile& ProcFile::operator=(ProcFile&& other) noexcept 
{
  if (this != &other) 
  {
    m_path = std::move(other.m_path);
    m_is_directory = other.m_is_directory;
    m_file_stream = std::move(other.m_file_stream);
    m_dir_handle = std::move(other.m_dir_handle);
    other.m_is_directory = false;
  }
  return *this;
}

bool ProcFile::is_open() const
{
  if (m_is_directory)
    return m_dir_handle != nullptr;
  else 
   return m_file_stream.is_open();
}

bool ProcFile::is_directory() const 
{
  return m_is_directory;
}

std::string ProcFile::read_all() {
  if (m_is_directory || !m_file_stream.is_open()) 
  {
    LOG_ERROR("无法从目录或已关闭的文件流读取内容");
    return "";
  }
    
  // 保存当前位置
  std::streampos original_pos = m_file_stream.tellg();
  m_file_stream.seekg(0, std::ios::beg);
    
  std::stringstream buffer;
  buffer << m_file_stream.rdbuf();
    
  // 恢复位置
  m_file_stream.seekg(original_pos);
    
  return buffer.str();
}

std::vector<std::string> ProcFile::read_lines() {
  std::vector<std::string> lines;
  if (m_is_directory || !m_file_stream.is_open()) 
  {
    LOG_ERROR("无法从目录或已关闭的文件流读取内容");
    return lines;
  }

  std::streampos original_pos = m_file_stream.tellg();
  m_file_stream.seekg(0, std::ios::beg);
    
  std::string line;
  while (std::getline(m_file_stream, line)) 
  {
    lines.push_back(std::move(line));
  }
    
  m_file_stream.seekg(original_pos);
  return lines;
}

std::string ProcFile::read_line() {
  if (m_is_directory || !m_file_stream.is_open()) 
  {
    LOG_ERROR("无法从目录或已关闭的文件流读取内容");
    return "";
  }
    
  std::string line;
  if (std::getline(m_file_stream, line)) return line;
  else return "";
}

std::ifstream& ProcFile::file_stream() 
{
    return m_file_stream;
}


std::vector<dirent*> ProcFile::list_entries() {
  std::vector<dirent*> entries;
  if (!m_is_directory || !m_dir_handle) 
  {
    LOG_ERROR("无法从非目录或已关闭的目录句柄读取内容");
    return entries;
  }

  rewinddir(m_dir_handle.get());
    
  dirent* entry;
  while ((entry = readdir(m_dir_handle.get())) != nullptr) 
  {
    // 跳过 . 和 ..
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
    {
      continue;
    }
    entries.emplace_back(entry);
  }
    
  return entries;
}

DIR* ProcFile::directory_handle() 
{
  return m_dir_handle.get();
}

namespace ProcHelper 
{

std::vector<pid_t> find_app_process(const std::string& package_name)
{
  std::vector<pid_t> match_pids;
  if (package_name.empty()) 
  {
    LOG_ERROR("包名不能为空");
    return match_pids;
  }

  // 遍历 /proc 下所有条目
  auto proc_root = ProcFile::open("/proc");
  if (!proc_root || !proc_root->is_open() || !proc_root->is_directory()) 
  {
    LOG_ERROR("打开 /proc 目录失败: {}", strerror(errno));
    return match_pids;
  }

  auto entries = proc_root->list_entries();
  for (auto* entry : entries)
  {
    if (!entry) continue;
    const char* name = entry->d_name;

    // 仅处理数字组成的目录名
    bool is_pid = true;
    for (const char* c = name; *c != '\0'; ++c) 
    {
      if (!std::isdigit(static_cast<unsigned char>(*c))) 
      {
        is_pid = false;
        break;
      }
    }
    if (!is_pid) continue;

    // 转换为 PID 数值
    pid_t pid = static_cast<pid_t>(std::atoi(name));
    if (pid <= 0) continue;

    // 读取 /proc/[pid]/cmdline（优先，包含完整包名/命令行）
    auto cmdline_file = ProcFile::open(pid, ProcFileType::CMDLINE);
    if (cmdline_file && cmdline_file->is_open()) 
    {
      std::string cmdline = cmdline_file->read_all();
      // cmdline 以 '\0' 分隔参数，替换为空格方便匹配
      std::replace(cmdline.begin(), cmdline.end(), '\0', ' ');
      if (Utils::contains_string(cmdline, package_name, Utils::MatchMode::INSENSITIVE)) 
      {
        match_pids.push_back(pid);
        continue;
      }
    }

    // 读取 /proc/[pid]/comm（备用，进程短名，适合简单匹配）
    auto comm_file = ProcFile::open(pid, ProcFileType::COMM);
    if (comm_file && comm_file->is_open()) 
    {
      std::string comm = comm_file->read_all();
      // 去除换行符（comm文件末尾通常有\n）
      comm.erase(std::remove(comm.begin(), comm.end(), '\n'), comm.end());
      if (Utils::contains_string(comm, package_name, Utils::MatchMode::INSENSITIVE)) 
      {
        match_pids.push_back(pid);
      }
    }
  }

  // 去重
  std::sort(match_pids.begin(), match_pids.end());
  auto last = std::unique(match_pids.begin(), match_pids.end());
  match_pids.erase(last, match_pids.end());

  LOG_DEBUG("找到 {} 个匹配包名 [{}] 的进程", match_pids.size(), package_name);
  return match_pids;
}


// 回去进程所有 pid
std::vector<pid_t> get_thread_ids(pid_t pid)
{
  std::vector<pid_t> tids;

  std::optional<ProcFile> task_file = ProcFile::open(pid, ProcFileType::TASK);
  if (!task_file || !task_file->is_open()) 
  {
    LOG_ERROR("解析进程状态失败：无法打开/proc/{}/task", pid);
    return tids;
  }

  std::vector<dirent*> entrys = task_file.value().list_entries();

  for (const auto entry : entrys)
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

  // 编译器会自动进行 RVO(返回值优化), 加不加 std::move 都行
  return std::move(tids); 
}

std::string process_state_to_string(ProcessState state) 
{
  switch (state) 
  {
    case ProcessState::RUNNING:    return "R (running)";
    case ProcessState::SLEEPING:   return "S (sleeping)";
    case ProcessState::DISK_SLEEP: return "D (disk sleep)";
    case ProcessState::STOPPED:    return "T (stopped)";
    case ProcessState::ZOMBIE:     return "Z (zombie)";
    case ProcessState::DEAD:       return "X (dead)";
    case ProcessState::WAITING:    return "W (waiting)";
    case ProcessState::PARKED:     return "P (parked)";
    case ProcessState::UNKNOWN:    return "Unknown";
    default:                       return "Invalid";
  }
}

ProcessState parse_process_state(pid_t pid)
{
  // 校验PID有效性
  if (pid <= 0) 
  {
    LOG_WARNING("解析进程状态失败: 无效PID({})", pid);
    return ProcessState::UNKNOWN;
  }

  auto status_file = ProcFile::open(pid, ProcFileType::STATUS);
  if (!status_file || !status_file->is_open()) 
  {
    LOG_WARNING("解析进程状态失败：无法打开/proc/{}/status", pid);
    return ProcessState::UNKNOWN;
  }

  // 按行读取 status 文件内容, 逐行匹配 State 字段
  std::vector<std::string> lines = status_file->read_lines();
  for (const std::string& line : lines) 
  {
    size_t state_pos = line.find("State:");
    if (state_pos == std::string::npos) continue;

    std::string state_part = line.substr(state_pos + 6); // "State:"共6个字符
    state_part.erase(0, state_part.find_first_not_of(" \t")); // 去除前缀空格

    if (state_part.empty()) 
    {
      LOG_WARNING("解析进程状态失败: PID({})的 State 字段无有效内容", pid);
      return ProcessState::UNKNOWN;
    }
    char state_char = std::toupper(static_cast<unsigned char>(state_part[0]));

    switch (state_char) 
    {
      case 'R': return ProcessState::RUNNING;
      case 'S': return ProcessState::SLEEPING;
      case 'D': return ProcessState::DISK_SLEEP;
      case 'T': return ProcessState::STOPPED;
      case 'Z': return ProcessState::ZOMBIE;
      case 'X': return ProcessState::DEAD;
      case 'W': return ProcessState::WAITING;
      case 'P': return ProcessState::PARKED;
      default: 
      {
        LOG_WARNING("解析进程状态失败: PID({})发现未知状态字符({})", pid, state_char);
        return ProcessState::UNKNOWN;
      }
    }
  }

  // 未找到 State 字段
  LOG_WARNING("解析进程状态失败: PID({})的 status 文件无 State 字段", pid);
  return ProcessState::UNKNOWN;
}

}