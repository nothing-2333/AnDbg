#pragma once 

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "register_control.hpp"

// 条件断点回调函数类型
using BreakpointCondition = std::function<bool(pid_t tid, uint64_t address, const user_pt_regs& regs)>;

// 断点类型
enum class BreakpointType
{
  SOFTWARE,             // 软件断点
  HARDWARE_EXECUTION,   // 硬件执行断点
  HARDWARE_WRITE,       // 硬件写入断点
  HARDWARE_READWRITE    // 硬件读写断点
};

// 提供给用户使用
enum class HareWareBreakpointType
{
  EXECUTION,
  WRITE,
  READWRITE 
};

// 断点结构体
struct Breakpoint 
{
  int id;                               // 断点唯一标识
  pid_t tid;                            // 关联的线程
  uint64_t address;                     // 断点地址 
  BreakpointType type;                  // 断点类型
  bool enabled;                         // 是否启用
  uint32_t original_instruction;        // 保存被替换的原始指令字节
  DBRegister hardware_register;         // 硬件断点使用的寄存器
  BreakpointCondition condition;        // 条件断点回调函数

  // ARM64 断点指令常量
  static constexpr uint32_t BRK_OPCODE = 0xD4200000;

  Breakpoint(int id_, pid_t tid_, uint64_t address_, BreakpointType type_, BreakpointCondition condition_)
    : id(id_), tid(tid_), address(address_), type(type_), condition(condition_), 
    enabled(false), original_instruction(0), hardware_register(DBRegister::DBG_INVALID)
  {
    if (tid < 1)
      throw std::invalid_argument("tid 必须是一个正值");
    if ((address & 0x3) != 0)
      throw std::invalid_argument("address 必须四字节对齐");
  }

  bool operator==(const Breakpoint& other) const { return id == other.id; }

  Breakpoint() = default;
};

// 断点管理
class BreakpointManager 
{
private:

  // DBGBCR 控制寄存器的配置位(ARMv8 架构定义)
  static constexpr uint64_t DBGBCR_ENABLE = 1ULL << 0;          // 启用断点
  static constexpr uint64_t DBGBCR_TYPE_EXECUTION = 0ULL << 1;  // 执行断点(0b00)
  static constexpr uint64_t DBGBCR_TYPE_WRITE = 1ULL << 1;      // 写入断点(0b01)
  static constexpr uint64_t DBGBCR_TYPE_READWRITE = 2ULL << 1;  // 读写断点(0b10)
  static constexpr uint64_t DBGBCR_EL1 = 1ULL << 5;             // 仅在 EL1(内核态)生效
  static constexpr uint64_t DBGBCR_EL0 = 1ULL << 6;             // 仅在 EL0(用户态)生效
  static constexpr uint64_t DBGBCR_MASK = 0x3ULL << 12;         // 地址匹配模式(默认全匹配)
  static constexpr uint64_t DBGBCR_MATCH_FULL = 0x0ULL << 12;   // 全地址匹配

  // 所有断点, 占内存
  std::unordered_map<int, Breakpoint> m_breakpoints;                 
  
  // 通过 pid 找断点 ID
  std::unordered_map<pid_t, std::unordered_set<int>> m_tid_breakpoints;         
  
  // 空闲硬件断点寄存器
  std::unordered_set<DBRegister> m_free_hardware_registers;

  // 线程安全
  mutable std::recursive_mutex m_mutex;                                   
  
  // 下一个要分配的断点 ID
  int m_next_breakpoint_id = 1;                                                   
  
public:

  // 获取支持的硬件断点数量, 需要附加子进程后调用, 在这里会初始化 m_free_hardware_registers
  int get_hardware_register_count(pid_t pid);

  // 设置软件断点 
  int set_software_breakpoint(pid_t tid, uint64_t address, BreakpointCondition condition=nullptr);
  
  // 设置硬件断点
  int set_hardware_breakpoint(pid_t tid, uint64_t address, HareWareBreakpointType type, BreakpointCondition condition=nullptr);

  // 移除断点对象
  bool remove_breakpoint(int bid);

  // 检查断点条件
  bool check_breakpoint_condition(int breakpoint_id);

  // 启用断点
  bool enable(int breakpoint_id);

  // 禁用断点
  bool disable(int breakpoint_id);

  // 获取所有断点
  std::vector<Breakpoint> get_breakpoints();

  // 获取指定进程/线程所有断点
  std::vector<Breakpoint> get_breakpoints(pid_t pid);
  
  // 根据 id 获取断点对象
  std::optional<Breakpoint> get_breakpoint(int breakpoint_id);

private:

  // 新建断点对象
  int new_breakpoint(pid_t tid, uint64_t address, BreakpointType type, 
  uint32_t original_instruction, BreakpointCondition condition);

  // 检查重复断点
  bool check_duplicate_breakpoint(pid_t tid, uint64_t address, BreakpointType type);
};
