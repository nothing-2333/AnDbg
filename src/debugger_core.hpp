#pragma once

#include <string>
#include <vector>
#include "register_control.hpp"
#include "status.hpp"

#include <nlohmann/json.hpp>

namespace Core 
{

// 封装一些实现, 要求输入或输出类型必须容易转换
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

  // // 执行控制
  // Status resume();
  // Status step_into(pid_t tid = -1);
  // Status step_over(pid_t tid = -1);
  // Status step_out(pid_t tid = -1);
  // Status pause();

  // // 内存操作
  // Status read_memory(uint64_t address, size_t size, void *buf, size_t &bytes_read);
  // Status write_memory(uint64_t address, size_t size, const void *buf, size_t &bytes_written);
  // Status read_memory_tags(int32_t type, uint64_t address, size_t len, std::vector<uint8_t> &tags);
  // Status write_memory_tags(int32_t type, uint64_t address, size_t len, const std::vector<uint8_t> &tags);
  // Status allocate_memory(size_t size, uint32_t permissions);
  // Status deallocate_memory(size_t size);
  // Status get_memory_regions(uint64_t load_addr, std::vector<MemoryRegion>& result);
  // Status get_memory_map(std::vector<MemoryRegion>& result);

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

  // // 反汇编
  // Status disassemble(uint64_t address, size_t count, DisassemblyResult& result);
  // Status generate_cfg();

  // // 线程管理
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
