#pragma once 

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

// 断点类型
enum class BreakpointType
{
  SOFTWARE,  // 软件断点
  HARDWARE   // 硬件断点
};

// 硬件断点类型
enum class HardwareBreakpointType
{
  EXECUTION = 0,  // 执行断点
  WRITE = 1,      // 写入断点  
  READWRITE = 2   // 读写断点
};

// 断点结构体
struct Breakpoint 
{
  pid_t tid;                            // 关联的线程
  uint64_t address;                     // 断点地址 
  BreakpointType type;                  // 断点类型
  bool enabled;                         // 是否启用
  uint8_t original_instruction;         // 保存被替换的原始指令字节
  int hardware_register_id;             // 硬件断点使用的寄存器索引
  HardwareBreakpointType hardware_type; // 硬件断点类型

  // ARM64 断点指令常量
  static constexpr uint32_t BRK_OPCODE = 0xD4200000;

  Breakpoint(pid_t tid_, uint64_t address_, BreakpointType type_, HardwareBreakpointType hardware_type_ = HardwareBreakpointType::EXECUTION)
    : tid(tid_), address(address_), type(type_), enabled(false), original_instruction(0), hardware_register_id(-1), hardware_type(hardware_type_) 
  {
    if (tid < 1)
      throw std::invalid_argument("tid 必须是一个正值");
    if ((address & 0x3) != 0)
      throw std::invalid_argument("address 必须四字节对齐");
  }

  bool operator==(const Breakpoint& other) const
  {
    return tid == other.tid && address == other.address && type == other.type;
  }
};

// 条件断点回调函数类型
using BreakpointCondition = std::function<bool(pid_t tid, uint64_t address, const user_regs_struct& regs)>;

// 条件断点
struct ConditionalBreakpoint : public Breakpoint
{
  BreakpointCondition condition;
  std::string description;  // 条件描述, 用于调试

  ConditionalBreakpoint(pid_t tid_, uint64_t address_, BreakpointType type_, 
  BreakpointCondition condition_, const std::string& description = "",
  HardwareBreakpointType hardware_type_ = HardwareBreakpointType::EXECUTION)
    : Breakpoint(tid_, address_, type_, hardware_type_), condition(std::move(condition_)), description(description) {}
};

class BreakpointManager 
{
private:
  std::vector<Breakpoint> m_breakpoints;  // 所有断点
  std::unordered_map<pid_t, std::unordered_set<Breakpoint*>> m_tid_breakpoints;  // 线程-断点地址映射
  
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
