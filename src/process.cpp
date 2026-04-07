#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sys/stat.h>
#include <filesystem>
#include <algorithm>
#include <vector>

#include "process.hpp"
#include "file.hpp"
#include "log.hpp"
#include "status.hpp"
#include "utils.hpp"


namespace Process 
{

namespace  
{

// 文件类型到文件名的映射
const std::unordered_map<ProcFileType, std::string> FILE_TYPE_TO_NAME = 
{
  {ProcFileType::ATTR, "attr"},
  {ProcFileType::AUTOGROUP, "autogroup"},
  {ProcFileType::CGROUP, "cgroup"},
  {ProcFileType::CLEAR_REFS, "clear_refs"},
  {ProcFileType::CMDLINE, "cmdline"},
  {ProcFileType::COMM, "comm"},
  {ProcFileType::COREDUMP_FILTER, "coredump_filter"},
  {ProcFileType::CPUSET, "cpuset"},
  {ProcFileType::CWD, "cwd"},
  {ProcFileType::ENVIRON, "environ"},
  {ProcFileType::EXE, "exe"},
  {ProcFileType::FD, "fd"},
  {ProcFileType::FDINFO, "fdinfo"},
  {ProcFileType::IO, "io"},
  {ProcFileType::LIMITS, "limits"},
  {ProcFileType::MAP_FILES, "map_files"},
  {ProcFileType::MAPS, "maps"},
  {ProcFileType::MEM, "mem"},
  {ProcFileType::MOUNTINFO, "mountinfo"},
  {ProcFileType::MOUNTS, "mounts"},
  {ProcFileType::MOUNTSTATS, "mountstats"},
  {ProcFileType::NET, "net"},
  {ProcFileType::NS, "ns"},
  {ProcFileType::NUMA_MAPS, "numa_maps"},
  {ProcFileType::OOM_SCORE, "oom_score"},
  {ProcFileType::OOM_SCORE_ADJ, "oom_score_adj"},
  {ProcFileType::PAGEMAP, "pagemap"},
  {ProcFileType::PERSONALITY, "personality"},
  {ProcFileType::PROJID_MAP, "projid_map"},
  {ProcFileType::ROOT, "root"},
  {ProcFileType::SECCOMP, "seccomp"},
  {ProcFileType::SETGROUPS, "setgroups"},
  {ProcFileType::SMAPS, "smaps"},
  {ProcFileType::STACK, "stack"},
  {ProcFileType::STAT, "stat"},
  {ProcFileType::STATM, "statm"},
  {ProcFileType::STATUS, "status"},
  {ProcFileType::SYSCALL, "syscall"},
  {ProcFileType::TASK, "task"},
  {ProcFileType::TIMERS, "timers"},
  {ProcFileType::TIMERSLACK_NS, "timerslack_ns"},
  {ProcFileType::UID_MAP, "uid_map"},
  {ProcFileType::WCHAN, "wchan"},
};

const std::unordered_map<char, ProcessState> CHAR_TO_STATE = 
{
  {'R', ProcessState::RUNNING},
  {'S', ProcessState::SLEEPING},
  {'D', ProcessState::DISK_SLEEP},
  {'T', ProcessState::STOPPED},
  {'t', ProcessState::TRACING_STOP},
  {'X', ProcessState::DEAD},
  {'Z', ProcessState::ZOMBIE},
  {'P', ProcessState::PARKED},
  {'I', ProcessState::IDLE}
};


const std::unordered_map<ProcessState, char> STATE_TO_CHAR = 
{
  {ProcessState::RUNNING,      'R'},
  {ProcessState::SLEEPING,     'S'},
  {ProcessState::DISK_SLEEP,   'D'},
  {ProcessState::STOPPED,      'T'},
  {ProcessState::TRACING_STOP, 't'},
  {ProcessState::DEAD,         'X'},
  {ProcessState::ZOMBIE,       'Z'},
  {ProcessState::PARKED,       'P'},
  {ProcessState::IDLE,         'I'}
};
}

const char PROCHelper::process_state_to_char(ProcessState state)
{
  auto it = STATE_TO_CHAR.find(state);
  return (it != STATE_TO_CHAR.end()) ? it->second : '?';
}

ProcessState PROCHelper::char_to_process_state(const char state)
{
  auto it = CHAR_TO_STATE.find(state);
  return (it != CHAR_TO_STATE.end()) ? it->second : ProcessState::UNKNOWN;
}

std::string PROCHelper::proc_file_type_to_string(ProcFileType type)
{
  auto it = FILE_TYPE_TO_NAME.find(type);
  if (it == FILE_TYPE_TO_NAME.end()) 
  {
    LOG_ERROR("FILE_TYPE_TO_NAME 没有将 ProcFileType 中的 {} 映射到文件名", static_cast<int>(type));
    return "";
  }
  return it->second;
}

std::vector<pid_t> PROCHelper::get_thread_ids(pid_t pid)
{
  std::optional<Base::File> task_file = Base::File::open(fmt::format("/proc/{}/task", pid));
  if (!task_file || !task_file->is_open()) 
  {
    LOG_ERROR("解析进程状态失败：无法打开/proc/{}/task", pid);
    return {};
  }

  std::vector<pid_t> tids;
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

std::vector<pid_t> PROCHelper::find_pid_by_package_name(const std::string& package_name)
{
  if (package_name.empty()) 
  {
    LOG_ERROR("包名不能为空");
    return {};
  }

  std::vector<pid_t> match_pids;

  // 遍历 /proc 下所有条目
  auto proc_root = Base::File::open("/proc");
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
    auto cmdline_file = Base::File::open(fmt::format("/proc/{}/cmdline", pid));
    if (cmdline_file && cmdline_file->is_open()) 
    {
      std::vector<char> cmdline_data = cmdline_file->read_all();
      // cmdline 以 '\0' 分隔参数, 替换为空格方便匹配
      std::string cmdline;
      for (char ch : cmdline_data)
      {
        if (ch == '\0')
          cmdline += ' ';
        else
          cmdline += ch;
      }
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

  LOG_DEBUG("找到 {} 个匹配包名 {} 的进程", match_pids.size(), package_name);
  return match_pids;
}

std::optional<std::string> PROCHelper::find_package_name_by_pid(pid_t pid)
{
  if (pid <= 0) 
  {
    LOG_ERROR("获取包名失败: 无效的PID({})", pid);
    return std::nullopt;
  }

  auto cmdline_file = Base::File::open(fmt::format("/proc/{}/cmdline", pid));
  if (cmdline_file && cmdline_file->is_open()) 
  {
    std::vector<char> cmdline_data = cmdline_file->read_all();
    // cmdline 以 '\0' 分隔参数, 替换为空格方便匹配
    std::string cmdline;
    for (char ch : cmdline_data)
    {
      if (ch == '\0')
        cmdline += ' ';
      else
        cmdline += ch;
    }
    LOG_DEBUG("读取到的 cmdline: {}", cmdline);

    if (!cmdline.empty()) 
    {
      // 直接返回 cmdline 第一项
      std::string package_name;
      for (char ch : cmdline_data)
      {
        if (ch == '\0')
          break;
        else
          package_name += ch;
      }
      return package_name;
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

std::unordered_map<std::string, std::string> PROCHelper::parse_status(pid_t pid)
{
  auto status_file = Base::File::open(fmt::format("/proc/{}/status", pid));
  if (!status_file || !status_file->is_open()) 
  {
    LOG_ERROR("解析进程状态失败：无法打开/proc/{}/status", pid);
    return {};
  }

  std::unordered_map<std::string, std::string> status_map;

  std::vector<std::string> lines = status_file->read_lines();
  for (const std::string& line : lines) 
  {
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) continue;

    std::string key = line.substr(0, colon_pos);
    std::string value = line.substr(colon_pos + 1);

    // 去除前缀和后缀空格
    key = Utils::trim(key);
    value = Utils::trim(value);
    status_map[key] = value;
  }

  return status_map;
}

char PROCHelper::get_process_state_char(pid_t pid)
{
  if (pid <= 0) 
  {
    LOG_ERROR("解析进程状态失败: 无效PID({})", pid);
    return '?';
  }

  auto status_map = parse_status(pid);
  auto it = status_map.find("State");
  if (it == status_map.end() || it->second.empty())
  {
    LOG_ERROR("解析进程状态失败: PID({})的 status 文件无 State 字段", pid);
    return '?'; 
  }

  return static_cast<unsigned char>(it->second[0]);
}

ProcessState PROCHelper::get_process_state(pid_t pid)
{
  return char_to_process_state(get_process_state_char(pid));
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
  item.state = PROCHelper::get_instance().char_to_process_state(tokens[7][0]);
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