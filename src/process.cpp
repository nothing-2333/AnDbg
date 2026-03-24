#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <sys/stat.h>
#include <filesystem>
#include <algorithm>

#include "process.hpp"
#include "log.hpp"
#include "utils.hpp"

namespace Process 
{

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
  struct stat st;
  if (stat(path.c_str(), &st) != 0) 
  {
    LOG_DEBUG("检查路径[{}]是否为目录失败: {}", path, strerror(errno));
    return false;
  }
  return S_ISDIR(st.st_mode);
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
  if (is_directory)
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
    
  std::string content;
  char ch;
  while (m_file_stream.get(ch)) 
    content.push_back(ch);
    
  // 恢复位置
  m_file_stream.seekg(original_pos);
    
  return content;
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

std::vector<pid_t> find_pid_by_package_name(const std::string& package_name)
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

    // 读取 /proc/[pid]/cmdline
    auto cmdline_file = ProcFile::open(pid, ProcFileType::CMDLINE);
    if (cmdline_file && cmdline_file->is_open()) 
    {
      std::string cmdline = cmdline_file->read_all();
      // cmdline 以 '\0' 分隔参数, 替换为空格方便匹配
      std::replace(cmdline.begin(), cmdline.end(), '\0', ' ');
      if (Utils::contains_string(cmdline, package_name, false)) 
      {
        match_pids.push_back(pid);
        continue;
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

std::optional<std::string> find_package_name_by_pid(pid_t pid)
{
  if (pid <= 0) 
  {
    LOG_ERROR("获取包名失败: 无效的PID({})", pid);
    return std::nullopt;
  }

  auto cmdline_file = ProcFile::open(pid, ProcFileType::CMDLINE);
  if (cmdline_file && cmdline_file->is_open()) 
  {
    std::string cmdline = cmdline_file->read_all();
    if (!cmdline.empty()) 
    {
      LOG_DEBUG("读取到原始的 cmdline: {}", cmdline);
      size_t null_pos = cmdline.find('\0');
      if (null_pos != std::string::npos) 
        cmdline = cmdline.substr(0, null_pos);

      return cmdline;
    }
    else  
    {
      LOG_ERROR("cmdline 数据为空");
      return std::nullopt;
    }
  }
  else  
  {
    LOG_ERROR("ProcFile::open 打开文件失败");
    return std::nullopt;
  }
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
    if (entry->d_type == DT_DIR)
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

const char process_state_to_char(ProcessState state) 
{
  auto it = STATE_TO_CHAR.find(state);
  return (it != STATE_TO_CHAR.end()) ? it->second : '?';
}

ProcessState char_to_process_state(const char state)
{
  auto it = CHAR_TO_STATE.find(state);
  return (it != CHAR_TO_STATE.end()) ? it->second : ProcessState::UNKNOWN;
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

    return char_to_process_state(state_char);
  }

  // 未找到 State 字段
  LOG_WARNING("解析进程状态失败: PID({})的 status 文件无 State 字段", pid);
  return ProcessState::UNKNOWN;
}

bool PSHelper::parse_single_line(std::string line, PSItem& item)
{
  auto tokens = Utils::split_by_space(line);
  if (tokens.size() < 9)
  {
    LOG_ERROR("要求有 9 个 字段, 但实际有 {} 个.", tokens.size());
    return false;
  }

  item.user = tokens[0];
  item.pid = std::stoi(tokens[1]);
  item.ppid = std::stoi(tokens[2]);
  item.vsz = std::stoull(tokens[3]);
  item.rss = std::stoull(tokens[4]);
  item.wchan = tokens[5];
  item.addr = tokens[6];
  item.state = char_to_process_state(tokens[7][0]);
  for (size_t i = 8; i < tokens.size(); ++i)
  {
    if (i > 8) item.name += " ";
    item.name += tokens[i];
  }
  
  return true;
}

std::vector<PSHelper::PSItem> PSHelper::get_items()
{
  std::vector<PSItem> result;

  const char* ps_cmd = "ps -eo user,pid,ppid,vsz,rss,wchan,addr,s,args";
  FILE* pipe = popen(ps_cmd, "r");
  if (!pipe) return result;

  char buffer[2048];
  bool is_first_line = true;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
  {
    std::string line(buffer);
    line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

    if (is_first_line)
    {
      is_first_line = false;
      continue;
    }

    PSItem item;
    if (parse_single_line(line, item))
    {
      result.push_back(item);
    }
  }

  pclose(pipe);
  return result;
}

std::vector<pid_t> PSHelper::find_pid_by_process_name(std::string name, MatchMode mode, bool is_sensitivity)
{
  std::vector<pid_t> result;
  auto all_items = get_items();
  if (all_items.empty()) return result;

  if (!is_sensitivity)
    name = Utils::to_lower(std::move(name)); 

  for (const auto& item : all_items)
  {
    std::string ps_name = item.name;

    if (!is_sensitivity)
      ps_name = Utils::to_lower(std::move(ps_name));

    bool is_match = false;
    switch (mode) 
    {
      case MatchMode::EXACT: is_match = (ps_name == name); break;
      case MatchMode::CONTAIN: is_match = (ps_name.find(name) != std::string::npos); break;
      default: is_match = false; break;
    }

    if (is_match && item.pid != -1)
    {
      result.push_back(item.pid);
    }
  }

  // 排序, 去重
  std::sort(result.begin(), result.end());
  auto last = std::unique(result.begin(), result.end());
  result.erase(last, result.end());

  return result;
}

std::optional<std::string> PSHelper::find_process_name_by_pid(pid_t pid)
{
  auto all_items = get_items();
  if (all_items.empty()) return std::nullopt;

  for (const auto& item : all_items)
  {
    if (item.pid == pid) return item.name;
  }

  return std::nullopt;
}

}