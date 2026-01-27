#include "proc_file.hpp"
#include <cstddef>
#include <unordered_set>


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

ProcFile::ProcFile(const std::string& path, bool is_directory) : m_path(path), m_is_directory(is_directory), m_dir_handle(nullptr, closedir)
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

ProcFile::~ProcFile() 
{ 
  // RAII: 自动关闭文件流和目录句柄
}

bool ProcFile::is_open() const
{
  if (m_is_directory)
    return m_dir_handle == nullptr;
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