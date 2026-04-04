#include "debugger_core.hpp"
#include "assembly.hpp"
#include "breakpoint_manager.hpp"
#include "process.hpp"
#include "register_control.hpp"
#include "status.hpp"
#include "utils.hpp"
#include "log.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <linux/wait.h>
#include <optional>
#include <string>
#include <sys/syscall.h>  
#include <sys/types.h>
#include <thread>
#include <variant>


using namespace Base;

namespace Core 
{

DebuggerCore::DebuggerCore() : register_crl(RegisterControl::get_instance()), 
disassembly_crl(Assembly::DisassemblyControl::get_instance()),
memory_crl(MemoryControl::get_instance()),
breakpoint_manager(BreakpointManager())
{
  m_pid = -1;
  m_current_tid = -1;
}

DebuggerCore::~DebuggerCore()
{

}

Status DebuggerCore::get_threads(std::vector<pid_t>& threads)
{
  threads = m_tids;
  return Status::success("get_threads 成功");
}

Status DebuggerCore::switch_thread(pid_t tid)
{
  if (std::find(m_tids.begin(), m_tids.end(), tid) != m_tids.end())
  {
    m_current_tid = tid;
    return Status::success("switch_thread 成功");
  }
  else  
    return Status::fail("线程 {} 不在 m_tids 中", tid);
}

Status DebuggerCore::get_pid(pid_t& pid)
{
  pid = m_pid;
  return Status::success("get_pid 成功");
}

Status DebuggerCore::get_current_tid(pid_t& tid)
{
  tid = m_current_tid;
  return Status::success("get_current_tid 成功");
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

Status DebuggerCore::attach(pid_t pid)
{
  auto tids = Process::get_thread_ids(pid);
  if (tids.empty()) return Status::fail("获取线程 id 有误");

  std::vector<pid_t> attached_tids;

  for (const auto& tid : tids)
  {
    if (!Utils::ptrace_wrapper(PTRACE_ATTACH, tid, nullptr, nullptr, 0))
    {
      LOG_WARNING("附加到线程 " + std::to_string(tid) + " 失败");
      continue;
    }

    int status;
    pid_t wpid = Utils::waitpid_wrapper(tid, &status, __WALL);
    if (wpid < 0)
    {
      LOG_WARNING("线程 " + std::to_string(tid) + " 未停止");
      continue;
    }

    if (!set_default_ptrace_options(tid))
    {
      LOG_WARNING("线程 " + std::to_string(tid) + " 设置选项失败");
      continue;
    }

    attached_tids.push_back(tid);
  }

  if (attached_tids.empty())
    return Status::fail("没有附加到任何线程");

  // 特殊处理, 主线程可能在调用 attach 前就已经被附加了, 如 launch 中 fork 的子进程 status 会继承父进程的 status
  if (std::find(attached_tids.begin(), attached_tids.end(), pid) == attached_tids.end())
  {
    if (Process::parse_process_state(pid) != Process::ProcessState::STOPPED)
      return Status::fail("主线程没有被附加");
    else  
      attached_tids.push_back(pid);
  }
    

  m_pid = pid;
  m_current_tid = pid;
  m_tids = attached_tids;

  return Status::success("attach 成功");
}

Status DebuggerCore::attach(const std::string& package_name)
{
  const auto& match_pids = Process::find_pid_by_package_name(package_name);
  if (match_pids.size() > 1)
    LOG_WARNING("attach 是发现报名对应多个 pid");

  return attach(match_pids[0]);
}

Status DebuggerCore::launch(const std::string& package_activity)
{
  // todo: 如果 app 已经开启就先关闭在开启

  // 找到 zygote 进程
  Process::PSHelper ps_helper;
  std::vector<pid_t> zygote_pids = ps_helper.find_pid_by_process_name("zygote64", Process::PSHelper::MatchMode::EXACT, true);
  if (zygote_pids.empty()) 
    zygote_pids = ps_helper.find_pid_by_process_name("zygote", Process::PSHelper::MatchMode::EXACT, true);
  if (zygote_pids.empty())
    return Status("未找到 zygote 进程", StatusType::FAIL);

  pid_t zygote_pid = zygote_pids[0];
  LOG_DEBUG("找到 zygote 进程 PID: " + std::to_string(zygote_pid));

  // 附加到 zygote 进程
  if (!Utils::ptrace_wrapper(PTRACE_ATTACH, zygote_pid, nullptr, nullptr, 0))
  {
    return Status("附加到 zygote 进程失败", StatusType::FAIL);
  }

  // 等待 zygote 停止
  int status;
  pid_t wpid = Utils::waitpid_wrapper(zygote_pid, &status, __WALL);
  if (wpid < 0 || !WIFSTOPPED(status)) 
  {
    Utils::ptrace_wrapper(PTRACE_DETACH, zygote_pid, nullptr, nullptr, 0);
    return Status::fail("等待 zygote 停止失败");
  }
  
  // 设置选项, 监控 fork
  if (!Utils::ptrace_wrapper(PTRACE_SETOPTIONS, zygote_pid, nullptr, 
  (void*)(PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK), 0)) 
  {
    Utils::ptrace_wrapper(PTRACE_DETACH, zygote_pid, nullptr, nullptr, 0);
    return Status::fail("设置 zygote ptrace 选项失败");
  }
  LOG_DEBUG("已附加到 zygote 进程, 开始监控 fork 调用");

  // 恢复 zygote 运行
  Utils::ptrace_wrapper(PTRACE_CONT, zygote_pid, nullptr, nullptr, 0);
  LOG_DEBUG("已恢复 zygote 运行, 等待 app 启动触发 fork 事件");

  // 启动目标 app
  std::string start_cmd = "am start -n " + package_activity;
  LOG_DEBUG("执行启动 app 命令: " + start_cmd);
  int ret = system(start_cmd.c_str());
  if (ret != 0) 
  {
    Utils::ptrace_wrapper(PTRACE_DETACH, zygote_pid, nullptr, nullptr, 0);
    return Status::fail("启动 app 失败, 命令返回值: {}", ret);
  }

  // 拦截 zygote fork 出的 app 进程 PID
  pid_t app_pid = -1;
  const int TIMEOUT_SEC = 5;
  auto start_time = std::chrono::steady_clock::now();
  while (true) 
  {
    // 检查超时
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time).count();
    if (elapsed > TIMEOUT_SEC) 
    {
      Utils::ptrace_wrapper(PTRACE_DETACH, zygote_pid, nullptr, nullptr, 0);
      return Status("拦截 app PID 超时", StatusType::FAIL);
    }

    // 等待 zygote 或其子进程的事件
    wpid = Utils::waitpid_wrapper(-1, &status, __WALL | WNOHANG); // 非阻塞等待
    if (wpid < 0)
    {
      Utils::ptrace_wrapper(PTRACE_DETACH, zygote_pid, nullptr, nullptr, 0);
      return Status::fail("等待进程事件失败,  errno = {}", strerror(errno));
    }
    else if (wpid == 0)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    else if (wpid == zygote_pid)
    {
      if (WIFSTOPPED(status))
      {
        int stop_signal = WSTOPSIG(status);
        if ((status >> 16) == PTRACE_EVENT_FORK)
        {
          LOG_DEBUG("捕获 zygote fork 事件, status: {}", status);
          pid_t fork_pid = 0;
          if (Utils::ptrace_wrapper(PTRACE_GETEVENTMSG, zygote_pid, nullptr, &fork_pid, sizeof(fork_pid)))
          {
            // 等待包名更新
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            LOG_DEBUG("捕获到 zygote fork 出的子进程 PID: {}", fork_pid);

            std::optional<std::string> child_name = Process::find_package_name_by_pid(fork_pid);
            if (!child_name) 
            {
              Utils::ptrace_wrapper(PTRACE_CONT, zygote_pid, nullptr, nullptr, 0);
              continue;
            }
            LOG_DEBUG("fork 出的进程名称: {}", child_name.value());

            if (!Utils::contains_string(package_activity, child_name.value(), true))
            {
              Utils::ptrace_wrapper(PTRACE_CONT, zygote_pid, nullptr, nullptr, 0);
              continue;
            }
            else  
            {
              app_pid = fork_pid;
              LOG_DEBUG("匹配到目标 app pid: {}", app_pid);

              // 此时 app 会继承 zygote 的 status 处于附加状态
              // LOG_DEBUG("app status: {}", Process::process_state_to_char(Process::parse_process_state(app_pid)));

              // 下面会直接 detach
              // Utils::ptrace_wrapper(PTRACE_CONT, zygote_pid, nullptr, nullptr, 0);
              break;
            }
          }
          else 
          {
            LOG_ERROR("获取 fork 事件 PID 失败");
            Utils::ptrace_wrapper(PTRACE_CONT, zygote_pid, nullptr, nullptr, 0);
            continue;
          }
        }
        // 非 fork 系统调用事件, 继续运行, 使用 PTRACE_SYSCALL
        else if (stop_signal == (SIGTRAP | 0x80))
          Utils::ptrace_wrapper(PTRACE_SYSCALL, zygote_pid, nullptr, nullptr, 0);
        // 其他停止信号，继续运行
        else
          Utils::ptrace_wrapper(PTRACE_CONT, zygote_pid, nullptr, nullptr, 0);
      }
      else if (WIFEXITED(status) || WIFSIGNALED(status))
      {
        LOG_ERROR("zygote 进程退出, status: {}", status);
        Utils::ptrace_wrapper(PTRACE_DETACH, zygote_pid, nullptr, nullptr, 0);
        return Status::fail("zygote 进程异常退出, 无法捕获 app pid");
      }
    }
    else  
    {
      Utils::ptrace_wrapper(PTRACE_CONT, wpid, nullptr, nullptr, 0);
    }
  }

  Utils::ptrace_wrapper(PTRACE_DETACH, zygote_pid, nullptr, nullptr, 0);

  // 附加 app
  if (app_pid <= 0) 
    return Status::fail("未拦截到 app 进程 PID");

  Status attach_status = attach(app_pid);
  if (attach_status.is_success())
    return Status::success("启动并附加到 app 成功, PID: {}", app_pid);
  else  
    return Status::fail("附加到 app 进程失败: {}", attach_status.c_str());
}

Status DebuggerCore::detach()
{
  if (m_pid < 0) Status::fail("m_pid 无效");

  bool all_ok = true;
  int success_count = 0;

  ::kill(m_pid, SIGCONT);

  for (const auto& tid : m_tids)
  {
    if (Utils::ptrace_wrapper(PTRACE_DETACH, tid, nullptr, nullptr, 0))
      success_count++;
    else
    {
      LOG_WARNING("分离线程 " + std::to_string(tid) + " 失败");
      all_ok = false;
    }
  }

  if (!all_ok)
    LOG_WARNING("部分线程分离失败, 成功: " + std::to_string(success_count) + "/" + std::to_string(m_tids.size()));

  return all_ok ? Status("detach 成功", StatusType::SUCCESS) : Status("detach 失败", StatusType::FAIL);

}

Status DebuggerCore::kill()
{
  if (m_pid < 0) Status::fail("m_pid 无效");

  if (::kill(m_pid, SIGKILL) != 0)
    return Status::fail("kill 失败, errno: {}", strerror(errno));

  m_pid = -1;
  m_tids.clear();
  return Status::success("kill 成功");
}

Status DebuggerCore::write_registers(nlohmann::json json_data)
{
  // todo: 需要加上 DBG 吗?

  if (json_data.contains("GPR") && !json_data["GPR"].is_null())
  {
    const nlohmann::json& gpr_json = json_data["GPR"];
    if (!gpr_json.is_object())
      return Status::fail("GPR 的 json 格式不对");

    // 遍历键值对
    for (auto it = gpr_json.begin(); it != gpr_json.end(); ++it)
    {
      const std::string reg_name = it.key();
      const nlohmann::json& reg_value = it.value();

      // 校验值类型
      if (!reg_value.is_string())
        return Status::fail("value 必须是 0x 开头的字符串: {}", reg_value.get<std::string>());
      auto value_opt  = Utils::hex_str_to_num<RegisterControl::GPRValue>(reg_value.get<std::string>());
      if (!value_opt)
        return Status::fail("value 不是合法的 16 进制字符串: {}", reg_value.get<std::string>());

      // 字符串转枚举
      GPRegister reg = register_crl.str2gpr(reg_name);
      if (reg == GPRegister::INVALID)
        return Status::fail("寄存器名称不对, 错误名称: {}", reg_name);

      RegisterControl::GPRValue value = value_opt.value();
      if (!register_crl.set_gpr(m_current_tid, reg, value))
        return Status::fail("set_gpr 失败: {}", reg_name);
    }
  }

  if (json_data.contains("FPR") && !json_data["FPR"].is_null()) 
  {
    const nlohmann::json& fpr_json = json_data["FPR"];
    if (!fpr_json.is_object())
      return Status::fail("FPR 的 json 格式不对");

    for (auto it = fpr_json.begin(); it != fpr_json.end(); ++it)
    {
      const std::string reg_name = it.key();
      const nlohmann::json& reg_value = it.value();

      if (!reg_value.is_string())
        return Status::fail("value 必须是 0x 开头的字符串: {}", reg_value.get<std::string>());

      FPRegister reg = register_crl.str2fpr(reg_name);
      if (reg == FPRegister::INVALID)
        return Status::fail("寄存器名称不对, 错误名称: {}", reg_name);

      RegisterControl::FPRValue fpr_val;
      if (reg >= FPRegister::V0 && reg <= FPRegister::V31)
      {
        auto value_opt  = Utils::hex_str_to_num<__uint128_t>(reg_value.get<std::string>());
        if (!value_opt)
          return Status::fail("value 不是合法的 16 进制字符串: {}", reg_value.get<std::string>());
        fpr_val = value_opt.value();
      }
      else if (reg == FPRegister::FPSR || reg == FPRegister::FPCR)
      {
        auto value_opt  = Utils::hex_str_to_num<uint32_t>(reg_value.get<std::string>());
        if (!value_opt)
          return Status::fail("value 不是合法的 16 进制字符串: {}", reg_value.get<std::string>());
        fpr_val = value_opt.value();
      }
      else  
        return Status::fail("不支持的寄存器: {}", reg_name);

      if (!register_crl.set_fpr(m_current_tid, reg, fpr_val))
        return Status::fail("set_fpr 失败: {}", reg_name);
    }
  }

  return Status::success("write_registers 成功");
}

Status DebuggerCore::read_registers(nlohmann::json json_data, nlohmann::json& result)
{
  // todo: 需要加上 DBG 吗?

  if (json_data.contains("GPR") && !json_data["GPR"].is_null())
  {
    const nlohmann::json& gpr_req = json_data["GPR"];
    nlohmann::json gpr_result = nlohmann::json::object();

    // 特殊字段
    if (gpr_req.is_string())
    {
      std::string req_str = gpr_req.get<std::string>();

      if (req_str == "all")
      {
        auto all_gpr_opt = register_crl.get_all_gpr(m_current_tid);
        if (!all_gpr_opt)
          return Status::fail("get_all_gpr 失败");

        const auto& all_gpr = all_gpr_opt.value();
        // 解析 user_pt_regs 结构体
        for (int i = static_cast<int>(GPRegister::X0); i < static_cast<int>(GPRegister::MAX_REGISTERS); ++i)
        {
          GPRegister reg = static_cast<GPRegister>(i);
          std::string reg_name = register_crl.gpr2str(reg);
          assert(reg_name != "unknown");

          auto ptr_opt = register_crl.get_gpr_pointer(const_cast<user_pt_regs&>(all_gpr), reg);
          if (!ptr_opt)
            return Status::fail("从结构体中读取 {} 指针失败", reg_name);

          gpr_result[reg_name] = Utils::num_to_hex_str<RegisterControl::GPRValue>(*ptr_opt.value());  
        }
      }
      else  
        return Status::fail("不支持的字段: {}", req_str);
      
    }
    // 正常数组
    else if (gpr_req.is_array())
    {
      for (const auto& reg_name_json : gpr_req)
      {
        if (!reg_name_json.is_string())
          return Status::fail("期待一个字符串, 而实际是: {}", reg_name_json.dump());

        std::string reg_name = reg_name_json.get<std::string>();
        GPRegister reg = register_crl.str2gpr(reg_name);
        if (reg == GPRegister::INVALID)
          return Status::fail("GPR 寄存器名称错误, 无效名称: {}", reg_name);

        auto val_opt = register_crl.get_gpr(m_current_tid, reg);
        if (!val_opt) return Status::fail("读取 GPR 失败: {}", reg_name);

        gpr_result[reg_name] = Utils::num_to_hex_str<RegisterControl::GPRValue>(val_opt.value());
      }
    }
    else  
      return Status::fail("不受支持的格式");

    result["GPR"] = gpr_result;
  }

  LOG_DEBUG("2");

  if (json_data.contains("FPR") && !json_data["FPR"].is_null())
  {
    const nlohmann::json& fpr_req = json_data["FPR"];
    nlohmann::json fpr_result = nlohmann::json::object();

    // 特殊字段
    if (fpr_req.is_string())
    {
      std::string req_str = fpr_req.get<std::string>();
      if (req_str == "all")
      {
        auto all_fpr_opt = register_crl.get_all_fpr(m_current_tid);
        if (!all_fpr_opt)
          return Status::fail("批量读取所有 FPR 寄存器失败");

        const auto& all_fpr = all_fpr_opt.value();
        for (int i = static_cast<int>(FPRegister::V0); i < static_cast<int>(FPRegister::MAX_REGISTERS); ++i)
        {
          FPRegister reg = static_cast<FPRegister>(i);
          std::string reg_name = register_crl.fpr2str(reg);

          auto ptr_opt = register_crl.get_fpr_pointer(const_cast<user_fpsimd_state&>(all_fpr), reg);
          if (!ptr_opt)
            return Status::fail("从结构体中读取 {} 指针失败", reg_name);

          auto ptr_val = ptr_opt.value(); // 很难起一个好名字
          if (std::holds_alternative<__uint128_t*>(ptr_val))
          {
            __uint128_t* val_ptr = std::get<__uint128_t*>(ptr_val);
            fpr_result[reg_name] = Utils::num_to_hex_str<__uint128_t>(*val_ptr);
          }
          else if (std::holds_alternative<uint32_t*>(ptr_val))
          {
            uint32_t* val_ptr = std::get<uint32_t*>(ptr_val);
            fpr_result[reg_name] = Utils::num_to_hex_str<uint32_t>(*val_ptr);
          }
        }
      }
      else  
        return Status::fail("不支持的字段: {}", req_str);
    }
    // 正常数组
    else if (fpr_req.is_array())
    {
      for (const auto& reg_name_json : fpr_req)
      {
        if (!reg_name_json.is_string())
          return Status::fail("期待一个字符串, 而实际是: {}", reg_name_json.dump());

        std::string reg_name = reg_name_json.get<std::string>();
        FPRegister reg = register_crl.str2fpr(reg_name);
        if (reg == FPRegister::INVALID)
          return Status::fail("FPR 寄存器名称错误, 无效名称: {}", reg_name);

        auto val_opt = register_crl.get_fpr(m_current_tid, reg);
        if (!val_opt) return Status::fail("读取 FPR 失败: {}", reg_name);

        const auto& fpr_val = val_opt.value();
        if (std::holds_alternative<__uint128_t>(fpr_val))
        {
          __uint128_t val = std::get<__uint128_t>(fpr_val);
          fpr_result[reg_name] = Utils::num_to_hex_str<__uint128_t>(val);
        }
        else if (std::holds_alternative<uint32_t>(fpr_val))
        {
          uint32_t val = std::get<uint32_t>(fpr_val);
          fpr_result[reg_name] = Utils::num_to_hex_str<uint32_t>(val);
        }
      }
    }
    else  
      return Status::fail("不受支持的格式");

    result["FPR"] = fpr_result;
  }

  return Status::success("read_registers 成功");
}

Status DebuggerCore::set_breakpoint(BreakpointType type, uint64_t address, int& breakpoint_id)
{

  if (type == BreakpointType::SOFTWARE)
  {
    breakpoint_id = breakpoint_manager.set_software_breakpoint(m_current_tid, address);
  }
  else if (type == BreakpointType::HARDWARE_EXECUTION || 
  type == BreakpointType::HARDWARE_READWRITE ||
  type == BreakpointType::HARDWARE_WRITE)
  {
    breakpoint_id = breakpoint_manager.set_hardware_breakpoint(m_current_tid, address, type);
  }
  else 
  {
    breakpoint_id = -1;
    return Status::fail("未知类型");
  }

  if (breakpoint_id == -1)
    return Status::fail("set_breakpoint 失败");
  else  
    return Status::success("set_breakpoint 成功");
}

Status DebuggerCore::remove_breakpoint(int breakpoint_id)
{
  return breakpoint_manager.remove_breakpoint(breakpoint_id);
}

Status DebuggerCore::enable_breakpoint(int breakpoint_id)
{
  return breakpoint_manager.enable(breakpoint_id);
}

Status DebuggerCore::disable_breakpoint(int breakpoint_id)
{
  return breakpoint_manager.disable(breakpoint_id);
}

Status DebuggerCore::get_breakpoint(int breakpoint_id, Breakpoint& breakpoint)
{
  auto breakpoint_opt = breakpoint_manager.get_breakpoint(breakpoint_id);
  if (!breakpoint_opt)
    return Status::fail("没有 id: {} 对应的断点", breakpoint_id);
  else  
  {
    breakpoint = breakpoint_opt.value();
    return Status::success("get_breakpoint 成功");
  }
}

Status DebuggerCore::get_breakpoint(uint64_t address, Breakpoint& breakpoint)
{
  auto breakpoint_opt = breakpoint_manager.get_breakpoint(address);
  if (!breakpoint_opt)
    return Status::fail("没有 address: {} 对应的断点", address);
  else  
  {
    breakpoint = breakpoint_opt.value();
    return Status::success("get_breakpoint 成功");
  }
}

Status DebuggerCore::get_breakpoints(std::vector<Breakpoint>& breakpoints)
{
  breakpoints = breakpoint_manager.get_breakpoints();
  if (breakpoints.empty())
    return Status::fail("get_breakpoints 失败");
  else 
    return Status::success("get_breakpoints 成功");
}

Status DebuggerCore::get_breakpoints(pid_t tid, std::vector<Breakpoint>& breakpoints)
{
  breakpoints = breakpoint_manager.get_breakpoints(tid);
  if (breakpoints.empty())
    return Status::fail("get_breakpoints 失败");
  else 
    return Status::success("get_breakpoints 成功");
}

Status DebuggerCore::resume_thread(pid_t tid)
{
  // 检查线程是否存在
  if (std::find(m_tids.begin(), m_tids.end(), tid) == m_tids.end())
    return Status::fail("resume_thread: 线程 {} 不存在", tid);

  int signo = 0;
  if (!Utils::ptrace_wrapper(PTRACE_CONT, tid, nullptr,
  reinterpret_cast<void*>(signo), sizeof(signo)))
    return Status::fail("resume_thread: PTRACE_CONT 失败 tid={}", tid);

  LOG_DEBUG("恢复线程成功: tid={}", tid);
  return Status::success("resume_thread 成功");
}

Status DebuggerCore::resume()
{
  if (m_pid <= 0 || m_tids.empty())
    return Status::fail("resume: 未附加任何进程");

  bool all_ok = true;
  std::vector<pid_t> failed_tids;

  for (const pid_t tid : m_tids)
  {
    Status s = resume_thread(tid);
    if (s.is_fail())
    {
      all_ok = false;
      failed_tids.push_back(tid);
      LOG_ERROR("恢复线程失败 tid={}: {}", tid, s.c_str());
    }
  }
  if (!all_ok) 
    return Status::fail("resume: 部分线程恢复失败, 失败数量: {}", (int)failed_tids.size());
    
  LOG_DEBUG("恢复所有线程成功, pid={}", m_pid);
  return Status::success("resume 所有线程成功");
}

Status DebuggerCore::step_into()
{
  // 优先使用硬件单步
  Status hw_status = hardware_step_into();
  if (hw_status.is_success())
  {
    LOG_DEBUG("硬件单步执行成功 tid={}", m_current_tid);
    return hw_status;
  }

  // 硬件不支持 → 降级为软件单步
  LOG_WARNING("硬件单步不支持, 使用软件单步 tid={}", m_current_tid);
  Status sw_status = software_step_into();
  if (sw_status.is_success())
  {
    LOG_DEBUG("软件单步执行成功 tid={}", m_current_tid);
    return sw_status;
  }

  return Status::fail("单步执行失败: {}", sw_status.c_str());
}

Status DebuggerCore::hardware_step_into(intptr_t signo)
{
  if (!Utils::ptrace_wrapper(PTRACE_SINGLESTEP, m_current_tid, nullptr,
  reinterpret_cast<void*>((signo)), sizeof(intptr_t)))
  {
    return Status::fail("hardware_step_into: PTRACE_SINGLESTEP 失败 tid = {} errno = {}", m_current_tid, strerror(errno));
  }

  // 等待线程停止
  int status = 0;
  pid_t wpid = Utils::waitpid_wrapper(m_current_tid, &status, __WALL);
  if (wpid != m_current_tid && !WIFSTOPPED(status))
  {
    return Status::fail("hardware_step_into: waitpid 失败 tid={}", m_current_tid);
  }

  return Status::success("hardware_step_into 成功 tid={}", m_current_tid);
}

Status DebuggerCore::single_step_impl(SingleStepMode mode)
{
  // 获取当前 PC
  uint64_t pc = 0;
  auto pc_opt = register_crl.get_gpr(m_current_tid, GPRegister::PC);
  if (!pc_opt) return Status::fail("读取PC失败");
  pc = pc_opt.value();

  // 下一条指令地址
  uint64_t next_pc;
  if (mode == SingleStepMode::STEP_OVER)
    next_pc = pc + 4;
  else
    return Status::fail("暂时不支持软件单步步入");

  uint32_t original_insn = 0;
  if (!memory_crl.read_memory(m_pid, next_pc, &original_insn, 4))
    return Status::fail("读取下一条指令失败");

  auto bid = breakpoint_manager.set_software_breakpoint(m_current_tid, next_pc);
  if (bid == -1)
    return Status::fail("设置临时断点失败");

  // 恢复执行
  resume_thread(m_current_tid);

  // 等待断点触发
  int status = 0;
  pid_t wpid = Utils::waitpid_wrapper(m_current_tid, &status, __WALL);
  
  // 触发后先把断点指令恢复了, 避免影响后续执行
  Status s = breakpoint_manager.remove_breakpoint(bid);
  if (s.is_fail()) return s;

  if (wpid != m_current_tid || !WIFSTOPPED(status)) 
    return Status::fail("等待暂停失败");

  return Status::success("软件单步完成");
}

Status DebuggerCore::software_step_into()
{
  return single_step_impl(SingleStepMode::STEP_INTO);
}

Status DebuggerCore::step_over()
{
  return single_step_impl(SingleStepMode::STEP_OVER);
}

Status DebuggerCore::pause()
{
  if (m_pid <= 0) 
    return Status::fail("pause: 未附加进程");

  if (::kill(m_pid, SIGSTOP) == -1) 
    return Status::fail("pause: 发送 SIGSTOP 失败");

  return Status::success("pause 成功");
}

Status DebuggerCore::disassemble(uint64_t address, size_t count, std::vector<Assembly::Instruction>& result)
{
  if (address <= 0)
    return Status::fail("无效的地址");
  if (count <= 0)
    return Status::fail("无效的数量");

  std::vector<char> codes(count * 4);
  if (!memory_crl.read_memory(m_pid, address, codes.data(), codes.size()))
    return Status::fail("read_memory 失败");

  
  auto r_opt = disassembly_crl.disassemble(codes);
  if (!r_opt)
    return Status::fail("反汇编失败");
  result = r_opt.value();

  return Status::success("disassemble 成功");
}

Status DebuggerCore::read_memory(uint64_t address, void* buf, size_t size)
{
  if (address <= 0)
    return Status::fail("无效的地址");
  if (size <= 0)
    return Status::fail("无效的大小");

  if (memory_crl.read_memory(m_pid, address, buf, size))
    return Status::success("read_memory 成功");
  else return Status::success("read_memory 失败, errno: {}", strerror(errno));
}

Status DebuggerCore::write_memory(uint64_t address, const void* buf, size_t size)
{
  if (address <= 0)
    return Status::fail("无效的地址");
  if (size <= 0)
    return Status::fail("无效的大小");

  if (memory_crl.write_memory(m_pid, address, buf, size))
    return Status::success("write_memory 成功");
  else return Status::fail("write_memory 失败, errno: {}", strerror(errno));
}

Status DebuggerCore::syscall(std::vector<uint64_t> args, uint64_t& ret)
{
  // 必须处于暂停状态

  if (args.size() < 1) 
    return Status::fail("args.size() 为 0");

  // 找到一个可执行, 私有的内存区域
  auto memory_regions = memory_crl.get_memory_regions(m_pid);
  uint64_t exe_addr = 0;
  for (const auto& region : memory_regions) 
  {
    if (region.is_executable() && region.is_private()) 
    {
      exe_addr = region.start_address;
      break;
    }
  }
  if (exe_addr == 0) 
    return Status::fail("未找到可用的内存区域");

  // 获取并备份原始寄存器
  auto gpr_opt = register_crl.get_all_gpr(m_current_tid);
  if (!gpr_opt) 
    return Status::fail("get_all_gpr 失败");
  user_pt_regs backup = gpr_opt.value();

  // 构造系统调用参数, ARM64 的系统调用约定是 x8 存放 syscall number, x0-x7 存放参数
  gpr_opt.value().regs[8] = args[0];
  for (int i = 1; i < args.size() && i < 8; ++i)
    gpr_opt.value().regs[i - 1] = args[i];

  if (!register_crl.set_all_gpr(m_current_tid, gpr_opt.value()))
    return Status::fail("set_all_gpr 失败");

  // 保存原始 exe_addr 处的指令 + 写入 SVC 指令
  uint32_t orig_insn;
  if (!memory_crl.read_memory(m_pid, exe_addr, &orig_insn, 4))
    return Status::fail("读取原始指令失败");

  // ARM64 svc 0 指令
  const uint32_t SVC_INSN = 0xD4000001;
  if (!memory_crl.write_memory(m_pid, exe_addr, &SVC_INSN, 4))
    return Status::fail("写入 SVC 指令失败");

  // 设置 pc 寄存器指向 exe_addr
  if (!register_crl.set_gpr(m_current_tid, GPRegister::PC, exe_addr))
    return Status::fail("设置 PC 寄存器失败");

  // 单步执行 SVC 指令
  Status s = step_over();
  if (s.is_fail()) return s;

  // 获取返回值
  auto x0_opt = register_crl.get_gpr(m_current_tid, GPRegister::X0);
  if (!x0_opt)
    return Status::fail("get_gpr 失败");

  ret = x0_opt.value();

  // 恢复
  memory_crl.write_memory(m_pid, exe_addr, &orig_insn, 4);
  register_crl.set_all_gpr(m_current_tid, backup);

  return Status::success("syscall 成功");
}

/*
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
*/
Status DebuggerCore::allocate_memory(size_t size, int permissions, uint64_t& allocated_address)
{
  uint64_t addr;
  Status s = syscall({
    (uint64_t)SYS_mmap,
    0,
    size,
    (uint64_t)permissions,
    (uint64_t)(MAP_ANONYMOUS | MAP_PRIVATE),
    (uint64_t)-1,
    0
  }, addr);
  if (s.is_fail()) return s;

  if (addr > (uint64_t)-1)
    return Status::fail("mmap 失败");

  g_allocated_memory[addr] = size;
  allocated_address = addr;
  return Status::success("allocate_memory 成功, 地址: 0x{:x}", addr);
}

Status DebuggerCore::deallocate_memory(uint64_t address)
{
  auto it = g_allocated_memory.find(address);
  if (it == g_allocated_memory.end())
    return Status::fail("地址未分配: 0x{:x}", address);

  size_t size = it->second;
  uint64_t ret;
  Status s = syscall({(uint64_t)SYS_munmap, address, size}, ret);
  if (s.is_fail()) return s;
  if (ret != 0)
    return Status::fail("munmap 失败");

  g_allocated_memory.erase(it);
  return Status::success("释放内存成功");
}

Status DebuggerCore::get_memory_regions(std::vector<MemoryRegion>& result)
{
  auto r = memory_crl.get_memory_regions(m_pid);
  if (r.empty())
    return Status::fail("get_memory_regions 失败");
  else 
  {
    result = r; 
    return Status::success("get_memory_regions 成功");
  }
}


}


