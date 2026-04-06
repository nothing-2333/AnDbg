#pragma once

#include <string>
#include <sys/types.h>
#include <vector>
#include "register_control.hpp"
#include "status.hpp"
#include <nlohmann/json.hpp>
#include "assembly.hpp"
#include "memory_control.hpp"
#include "breakpoint_manager.hpp"

namespace Core 
{

// 封装一些实现, 要求输入或输出类型必须容易转换

// todo: 为所有接口加上检查
// todo: 只有当前线程处于暂停状态才能执行一些接口, 需要做出检查
// todo: 添加状态的维护

class DebuggerCore
{
public:
  DebuggerCore();
  ~DebuggerCore();

  // 执行控制
  Base::Status attach(pid_t pid);
  Base::Status attach(const std::string& package_name);
  Base::Status launch(const std::string& package_activity);
  Base::Status detach();
  Base::Status kill();
  Base::Status resume_thread(pid_t tid);
  Base::Status resume();
  Base::Status step_into();
  Base::Status hardware_step_into();  // 硬件单步 ARM32, RISC-V, 龙芯不支持硬件单单步 
  Base::Status software_step_into();  // 软件单步
  Base::Status step_over();

  // 内存操作
  Base::Status read_memory(uint64_t address, void* buf, size_t size);
  Base::Status write_memory(uint64_t address, const void* buf, size_t size);
  Base::Status get_memory_regions(std::vector<MemoryRegion>& result);

  // 寄存器操作
  Base::Status write_registers(nlohmann::json json_data);
  Base::Status read_registers(nlohmann::json json_data, nlohmann::json& result);

  // 断点管理
  Base::Status set_breakpoint(BreakpointType type, uint64_t address, int& breakpoint_id);
  Base::Status remove_breakpoint(int breakpoint_id);
  Base::Status enable_breakpoint(int breakpoint_id);
  Base::Status disable_breakpoint(int breakpoint_id);
  Base::Status get_breakpoints(std::vector<Breakpoint>& breakpoints);
  Base::Status get_breakpoints(pid_t tid, std::vector<Breakpoint>& breakpoints);
  Base::Status get_breakpoint(int breakpoint_id, Breakpoint& breakpoint);  
  Base::Status get_breakpoint(uint64_t address, Breakpoint& breakpoint);  

  // 线程管理
  Base::Status get_threads(std::vector<pid_t>& threads);
  Base::Status switch_thread(pid_t tid);

  // 状态查询
  Base::Status get_pid(pid_t& pit);
  Base::Status get_current_tid(pid_t& tid);

  // // 符号解析
  // Base::Status symbol_to_address(const std::string& symbol_name, std::optional<uint64_t>& address);
  // Base::Status address_to_symbol(uint64_t address, std::optional<std::string>& symbol_name);

private:
  // 设置默认 ptrace 调试选项
  bool set_default_ptrace_options(pid_t pid);

  // 单步实现
  enum class SingleStepMode
  {
    STEP_INTO,
    STEP_OVER
  };
  Base::Status single_step_impl(SingleStepMode mode);

  // 主线程 pid
  pid_t m_pid;
  // 所有 tids
  std::vector<pid_t> m_tids;
  // 当前 tid
  pid_t m_current_tid;
  // 已经申请的内存地址
  std::unordered_map<uint64_t, size_t> g_allocated_memory;

  RegisterControl& register_crl;
  Assembly::DisassemblyControl& disassembly_crl;
  MemoryControl& memory_crl;

  BreakpointManager breakpoint_manager;
};

}
