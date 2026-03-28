#pragma once

#include <string>
#include <vector>
#include "register_control.hpp"
#include "status.hpp"
#include <nlohmann/json.hpp>
#include "assembly.hpp"
#include "memory_control.hpp"

namespace Core 
{

// 封装一些实现, 要求输入或输出类型必须容易转换

// todo: 为所有接口加上检查

// todo: 只有当前线程处于暂停状态才能执行一些接口, 需要做出检查
class DebuggerCore
{
public:
  DebuggerCore();
  ~DebuggerCore();

  // 进程控制
  Base::Status attach(pid_t pid);
  Base::Status attach(const std::string& package_name);
  Base::Status launch(const std::string& package_activity);
  Base::Status detach();
  Base::Status kill();

  // 执行控制
  Base::Status resume_thread(pid_t tid);
  Base::Status resume();
  Base::Status step_into(pid_t tid);
  Base::Status hardware_step_into(pid_t tid, intptr_t signo = 0); // 硬件单步 ARM32, RISC-V, 龙芯不支持硬件单单步 
  Base::Status software_step_into(pid_t tid);     // 软件单步
  Base::Status step_over(pid_t tid);
  Base::Status step_out(pid_t tid);
  Base::Status pause();

  // 内存操作
  Base::Status read_memory(uint64_t address, size_t size, void *buf, size_t &bytes_read);
  Base::Status write_memory(uint64_t address, size_t size, const void *buf, size_t &bytes_written);
  Base::Status read_memory_tags(int32_t type, uint64_t address, size_t len, std::vector<uint8_t> &tags);
  Base::Status write_memory_tags(int32_t type, uint64_t address, size_t len, const std::vector<uint8_t> &tags);
  Base::Status allocate_memory(size_t size, uint32_t permissions);
  Base::Status deallocate_memory(size_t size);
  Base::Status get_memory_map(std::vector<MemoryRegion>& result);

  // 寄存器操作
  /* 接受和返回数据举例
  {
    "GPR": {
      "x0": "0x123456"
    },
    "FPR": {
      "v0": "0x14511"
    }
  }

  {
    "GPR": "all",
    "FPR": ["v0", "v1", "v3"]
  }
  */
  Base::Status write_registers(nlohmann::json json_data);
  Base::Status read_registers(nlohmann::json json_data, nlohmann::json& result);

  // // 断点管理
  // Status set_breakpoint(uint64_t address, BreakpointCondition condition, int& breakpoint_id);
  // Status remove_breakpoint(int breakpoint_id);
  // Status enable_breakpoint(int breakpoint_id);
  // Status disable_breakpoint(int breakpoint_id);
  // Status get_breakpoints(std::vector<Breakpoint>& breakpoints);

  // 反汇编
  Base::Status disassemble(uint64_t address, size_t count, Assembly::Instruction& result);
  Base::Status generate_cfg();

  // 线程管理
  Base::Status get_threads(std::vector<pid_t>& threads);
  Base::Status switch_thread(pid_t tid);

  // 状态查询
  Base::Status get_pid(pid_t& pit);
  Base::Status get_current_tid(pid_t& tid);

  // // 符号解析
  // Status symbol_to_address(const std::string& symbol_name, std::optional<uint64_t>& address);
  // Status address_to_symbol(uint64_t address, std::optional<std::string>& symbol_name);

private:
  // 设置默认 ptrace 调试选项
  bool set_default_ptrace_options(pid_t pid);

  // 主线程 pid
  pid_t m_pid;
  // 所有 tids
  std::vector<pid_t> m_tids;
  // 当前 tid
  pid_t m_current_tid;
  // 寄存器控制
  RegisterControl& register_crl;
};

}
