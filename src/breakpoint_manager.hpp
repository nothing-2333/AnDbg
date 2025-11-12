#pragma once 

#include <cstdint>
#include <unordered_map>
#include <vector>


// 断点类型
enum class BreakpointType
{
  SOFTWARE,  // 软件断点
  HARDWARE   // 硬件断点
};

// 断点结构体
struct Breakpoint 
{
  pid_t tid;                      // 关联的线程
  uint64_t address;               // 断点地址 
  BreakpointType type;            // 断点类型
  bool enabled;                   // 是否启用
  uint8_t original_instruction;   // 保存被替换的原始指令字节
  int hardware_register_id;       // 硬件断点使用的寄存器索引

  Breakpoint(pid_t tid_, uint64_t addr_, BreakpointType type_)
    : tid(tid_), address(addr_), type(type_), enabled(false), original_instruction(0), hardware_register_id(-1) {}
};

class BreakpointManager 
{
private:
  std::vector<Breakpoint> m_breakpoints;  // 所有断点
  std::unordered_map<pid_t, std::vector<uint64_t>> m_tid_breakpoints;  // 线程-断点地址映射
  
public:
  // 设置软件断点 
  bool set_software_breakpoint(pid_t pid, uint64_t address);

  // 设置硬件断点
  bool set_hardware_breakpoint(pid_t pid, uint64_t address);

  // 移除断点
  bool remove(pid_t pid);

  // 启用断点
  bool enable(pid_t pid, uint64_t address);

  // 禁用断点
  bool disable(pid_t pid, uint64_t address);

  // 获取指定进程/线程所有断点
  std::vector<uint64_t> get_breakpoints(pid_t pid);

  // 获取所有断点
  std::vector<Breakpoint> get_all_breakpoints();
  
  // 检查地址是否有启用的断点
  const Breakpoint* check_breakpoint(pid_t pid, uint64_t address);
};
