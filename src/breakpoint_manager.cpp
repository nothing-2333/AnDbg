#include <asm/ptrace.h>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>

#include "breakpoint_manager.hpp"
#include "log.hpp"
#include "memory_control.hpp"
#include "register_control.hpp"


int BreakpointManager::get_hardware_register_count(pid_t pid)
{
  // 加锁, 线程安全
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  int max_supported = 0;
  const uint64_t test_address = 0x10000000;
  const uint32_t test_control = DBGBCR_ENABLE | DBGBCR_TYPE_EXECUTION | DBGBCR_EL0 | DBGBCR_MATCH_FULL;
  auto& register_control = RegisterControl::get_instance();

  // 获取调试寄存器
  const auto orign_dgb_opt = register_control.get_all_dbg(pid);
  if (!orign_dgb_opt)
  {
    LOG_ERROR("获取调试寄存器原始状态失败");
    return -1;
  }
  const auto& orgin_dbg = orign_dgb_opt.value();
  struct user_hwdebug_state test_dbg = orgin_dbg;

  // 填写 16 个调试寄存器的测试值
  for (int index = 0; index < 16; ++index)
  {
    test_dbg.dbg_regs[index].addr = test_address;
    test_dbg.dbg_regs[index].ctrl = test_control;
  }

  // 提交调试寄存器测试值
  // 系统会自动忽略硬件不支持的寄存器索引, 仅对存在的寄存器生效
  if (!register_control.set_all_dbg(pid, test_dbg))
  {
    LOG_ERROR("提交调试寄存器测试值失败");
    // 复原, 避免污染目标进程
    register_control.set_all_dbg(pid, orgin_dbg);
    return -1;
  }

  // 验证
  const auto verify_dbg_opt = register_control.get_all_dbg(pid);
  if (!verify_dbg_opt)
  {
    LOG_ERROR("验证时, 获取调试寄存器失败");
    register_control.set_all_dbg(pid, orgin_dbg);
    return -1;
  }
  const auto& verify_dbg = verify_dbg_opt.value();
  for (int index = 0; index < 16; ++index)
  {
    // 地址和控制字都与测试值一致, 说明寄存器存在且可写
    if (verify_dbg.dbg_regs[index].addr == test_address && verify_dbg.dbg_regs[index].ctrl == test_control) 
    {
      max_supported++;
      LOG_DEBUG("✅ DBG%d 验证成功", index);
    } 
    else 
    {
      LOG_DEBUG("❌ DBG%d 验证失败", index);
      break;
    }
  }

  // 原所有寄存器到原始值
  if (!register_control.set_all_dbg(pid, orgin_dbg))
  {
    LOG_ERROR("⚠️ 复原调试寄存器原始状态失败! 目标进程调试寄存器可能被污染");
    return -1;
  }
  else  
  LOG_DEBUG("✅ 调试寄存器已复原到原始状态");

  if (max_supported == 0)
    LOG_WARNING("不支持硬件断定, 调试寄存器数量为 0");
  else  
    LOG_DEBUG("调试寄存器数量为 {}", max_supported);

  // 初始化空闲寄存器
  for (int i = 0; i < max_supported; ++i)
    m_free_hardware_registers_.insert(static_cast<DBRegister>(i));

  return max_supported;
}

int BreakpointManager::set_software_breakpoint(pid_t tid, uint64_t address, BreakpointCondition condition)
{
  // 加锁, 线程安全
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  auto& memory_control = MemoryControl::get_instance();

  // 入参合法性校验
  if ((address & 0x3) != 0) 
  {
    throw std::invalid_argument("地址 0x" + std::to_string(address) + " 未按 4 字节对齐");
  }

  // 检查是否已经存在断点, 避免重复设置
  if (check_duplicate_breakpoint(tid, address, BreakpointType::SOFTWARE)) return -1;

  // 读取原指令
  uint32_t original_instruction = 0;
  if (!memory_control.read_memory(tid, address, &original_instruction, 4))
  {
    LOG_ERROR("读取地址 0x{:x} 原指令失败", address);
    return -1;
  }

  // 写入断点指令
  if (!memory_control.write_memory(tid, address, &Breakpoint::BRK_OPCODE, 4))
  {
    LOG_ERROR("写入断点指令到地址 0x{:x} 失败", address);
    return -1;
  }

  // 创建断点, 返回 id
  return new_breakpoint(tid, address, BreakpointType::SOFTWARE, original_instruction, condition);

}

// 辅助函数, 转换类型
inline static BreakpointType convert_enum_type(HareWareBreakpointType type)
{
  switch (type) 
  {
    case HareWareBreakpointType::EXECUTION: return BreakpointType::HARDWARE_EXECUTION;
    case HareWareBreakpointType::READWRITE: return BreakpointType::HARDWARE_READWRITE;
    case HareWareBreakpointType::WRITE: return BreakpointType::HARDWARE_WRITE;
  }
}

int BreakpointManager::set_hardware_breakpoint(pid_t tid, uint64_t address, HareWareBreakpointType type, BreakpointCondition condition)
{
  // 线程安全
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  // 入参校验
  if ((address & 0x3) != 0) 
    throw std::invalid_argument("地址 0x" + std::to_string(address) + " 未按 4 字节对齐");

  // 检查重复断点
  if (check_duplicate_breakpoint(tid, address, convert_enum_type(type))) return -1;

  // 分配硬件寄存器
  static const int hardware_breakpoint_count = get_hardware_register_count(tid);
  if (m_free_hardware_registers_.empty())
  {
    LOG_ERROR("无空闲硬件断点寄存器");
    return -1;
  }

  // 写入地址寄存器和控制寄存器
  DBRegister reg = *m_free_hardware_registers_.begin();
  m_free_hardware_registers_.erase(reg);

  auto& register_control = RegisterControl::get_instance();
  uint64_t control = DBGBCR_ENABLE | DBGBCR_EL0 | DBGBCR_MATCH_FULL;
  switch (type) 
  {
    case HareWareBreakpointType::EXECUTION: control |= DBGBCR_TYPE_EXECUTION; break;
    case HareWareBreakpointType::WRITE: control |= DBGBCR_TYPE_WRITE; break;
    case HareWareBreakpointType::READWRITE: control |= DBGBCR_TYPE_READWRITE; break;
  }

  if (!register_control.set_dbg(tid, reg, address, control))
  {
    LOG_ERROR("配置硬件寄存器失败");
    m_free_hardware_registers_.insert(reg);  // 归还寄存器
    return -1;
  }

  // 创建断点, 返回 id
  int id = new_breakpoint(tid, address, convert_enum_type(type), 0, condition);
  m_breakpoints_[id].hardware_register = reg;

  return id;
}

bool BreakpointManager::remove_breakpoint(int id)
{
  // 线程安全
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  auto& memory_control = MemoryControl::get_instance();

  // 查找断点, 并检查
  auto breakpont_item = m_breakpoints_.find(id);
  if (breakpont_item == m_breakpoints_.end())
  {
    LOG_ERROR("未找到 ID: {} 的断点", id);
    return false;
  }
  Breakpoint& breakpoint = breakpont_item->second;

  // 软件断点
  if (breakpoint.type == BreakpointType::SOFTWARE)
  {
    // 恢复原指令
    if (!memory_control.write_memory(breakpoint.tid, breakpoint.address, &breakpoint.original_instruction, 4))
    {
      LOG_ERROR("恢复软件断点 [ID: {}] 原指令失败", id);
      return false;
    }
  }
  // 硬件断点
  else if (breakpoint.hardware_register != DBRegister::DBG_INVALID) 
  {
    auto& register_control = RegisterControl::get_instance();
    // 禁用断点, 清除 DBGBCR_ENABLE 位
    auto dbg_opt = register_control.get_dbg(breakpoint.tid, breakpoint.hardware_register);
    if (dbg_opt)
    {
      auto [address, control] = dbg_opt.value();
      control &= ~DBGBCR_ENABLE;
      register_control.set_dbg(breakpoint.tid, breakpoint.hardware_register, address, control);
    }
    // 归还寄存器到空闲集合
    m_free_hardware_registers_.insert(breakpoint.hardware_register);
  }

  // 清理断点元数据
  m_tid_breakpoints_[breakpoint.tid].erase(breakpoint.id);
  if (m_tid_breakpoints_[breakpoint.id].empty())
    m_tid_breakpoints_.erase(breakpoint.tid);
  m_breakpoints_.erase(breakpont_item);

  LOG_DEBUG("成功移除断点: ID = {}, TID = {}, 地址 = 0x{:x}", id, breakpoint.tid, breakpoint.address);
  return true;
}

bool BreakpointManager::check_breakpoint_condition(int breakpoint_id)
{
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  auto breakpoint_item = m_breakpoints_.find(breakpoint_id);
  // 断点不存在或未启用
  if (breakpoint_item == m_breakpoints_.end() || !breakpoint_item->second.enabled)
  {
    LOG_DEBUG("断点 [ID: {}] 不存在或未启用", breakpoint_id);
    return false;
  }
    
  const auto& breakpoint = breakpoint_item->second;
  // 无条件断点，直接满足
  if (!breakpoint.condition) return true;

  // 执行回调
  try 
  {
    auto& register_control = RegisterControl::get_instance();
    auto gpr_opt = register_control.get_all_gpr(breakpoint.tid);
    if (!gpr_opt)
    {
      LOG_DEBUG("获取寄存器失败");
      return false;
    }

    bool met = breakpoint.condition(breakpoint.tid, breakpoint.address, gpr_opt.value());
    LOG_DEBUG("断点 [ID: {}] 条件检查: {}", breakpoint_id, met ? "满足" : "不满足");
    return met;
  } 
  catch (const std::exception& error) 
  {
    LOG_ERROR("断点 [ID: {}] 条件回调异常: {}", breakpoint_id, error.what());
    return false;
  }
}

bool BreakpointManager::enable(int breakpoint_id)
{
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  // 查找断点
  auto breakpoint_item = m_breakpoints_.find(breakpoint_id);
  if (breakpoint_item == m_breakpoints_.end())
  {
    LOG_ERROR("启用断点失败: 未找到 ID = {} 的断点", breakpoint_id);
    return false;
  }
  Breakpoint& breakpoint = breakpoint_item->second;

  if (breakpoint.enabled)
  {
    LOG_DEBUG("断点 [ID: {}] 已处于启用状态, 无需重复操作", breakpoint_id);
    return true;
  }

  auto& memory_control = MemoryControl::get_instance();
  auto& register_control = RegisterControl::get_instance();

  // 软件断点, 重新写入断点指令
  if (breakpoint.type == BreakpointType::SOFTWARE)
  {
    if (!memory_control.write_memory(breakpoint.tid, breakpoint.address, &Breakpoint::BRK_OPCODE, 4))
    {
      LOG_ERROR("启用软件断点 [ID: {}] 失败: 写入断点指令失败", breakpoint_id);
      return false;
    }
  }
  else if (breakpoint.hardware_register != DBRegister::DBG_INVALID) 
  {
    auto dbg_opt = register_control.get_dbg(breakpoint.tid, breakpoint.hardware_register);
    if (!dbg_opt) 
    {
      LOG_ERROR("启用硬件断点 [ID: {}] 失败: 获取调试寄存器失败", breakpoint_id);
      return false;
    }

    // 置位启用断点
    auto [address, control] = dbg_opt.value();
    control |= DBGBCR_ENABLE; 
    if (!register_control.set_dbg(breakpoint.id, breakpoint.hardware_register, address, control))
    {
      LOG_ERROR("启用硬件断点 [ID: {}] 失败: 更新控制寄存器失败", breakpoint_id);
      return false;
    }
  }

  breakpoint.enabled = true;
  LOG_DEBUG("成功启用断点 [ID: {}, TID: {}, 地址: 0x{:x}]", breakpoint_id, breakpoint.tid, breakpoint.address);
  return true;
}

bool BreakpointManager::disable(int breakpoint_id)
{
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  // 查找断点
  auto breakpoint_item = m_breakpoints_.find(breakpoint_id);
  if (breakpoint_item == m_breakpoints_.end()) 
  {
    LOG_ERROR("禁用断点失败: 未找到 ID = {} 的断点", breakpoint_id);
    return false;
  }

  Breakpoint& breakpoint = breakpoint_item->second;
  if (!breakpoint.enabled) 
  {
    LOG_DEBUG("断点 [ID: {}] 已处于禁用状态, 无需重复操作", breakpoint_id);
    return true;
  }

  auto& memory_control = MemoryControl::get_instance();
  auto& register_control = RegisterControl::get_instance();

  // 软件断点, 恢复原指令
  if (breakpoint.type == BreakpointType::SOFTWARE)
  {
    if (!memory_control.write_memory(breakpoint.tid, breakpoint.address, &breakpoint.original_instruction, 4))
    {
      LOG_ERROR("禁用软件断点 [ID: {}] 失败: 恢复原指令失败", breakpoint_id);
      return false;
    }
  }
  else if (breakpoint.hardware_register != DBRegister::DBG_INVALID) 
  {
    auto dbg_opt = register_control.get_dbg(breakpoint.tid, breakpoint.hardware_register);
    if (!dbg_opt) 
    {
      LOG_ERROR("启用硬件断点 [ID: {}] 失败: 获取调试寄存器失败", breakpoint_id);
      return false;
    }

    // 清除启用位
    auto [address, control] = dbg_opt.value();
    control &= ~DBGBCR_ENABLE;
    if (!register_control.set_dbg(breakpoint.id, breakpoint.hardware_register, address, control))
    {
      LOG_ERROR("启用硬件断点 [ID: {}] 失败: 更新控制寄存器失败", breakpoint_id);
      return false;
    }
  }

  // 标记为禁用
  breakpoint.enabled = false;
  LOG_DEBUG("成功禁用断点 [ID: {}, TID: {}, 地址: 0x{:x}]", breakpoint_id, breakpoint.tid, breakpoint.address);
  return true;
}

std::vector<Breakpoint> BreakpointManager::get_breakpoints()
{
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  std::vector<Breakpoint> result;
  for (const auto& [id, breakpoint] : m_breakpoints_)
  {
    result.push_back(breakpoint);
  }

  LOG_DEBUG("获取所有断点成功, 共 {} 个断点, 返回副本, 外部修改不影响内部状态", result.size());
  return result;
}

std::vector<Breakpoint> BreakpointManager::get_breakpoints(pid_t pid)
{
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  std::vector<Breakpoint> result;

  // 现在 m_tid_breakpoints 中寻找
  if (m_tid_breakpoints_.find(pid) == m_tid_breakpoints_.end())
  {
    LOG_DEBUG("线程 {} 无关联断点", pid);
    return result;
  }

  // 再在 m_breakpoints 取元数据
  for (int breakpoint_id : m_tid_breakpoints_[pid])
  {
    auto breakpoint_it = m_breakpoints_.find(breakpoint_id);
    if (breakpoint_it != m_breakpoints_.end()) result.push_back(breakpoint_it->second);
    else LOG_WARNING("线程 {} 的断点 ID {} 不存在", pid, breakpoint_id);
  }

  LOG_DEBUG("获取线程 {} 的断点成功, 共 {} 个断点, 返回副本, 外部修改不影响内部状态", pid, result.size());
  return result;
}

std::optional<Breakpoint> BreakpointManager::get_breakpoint(int breakpoint_id)
{
  std::lock_guard<std::recursive_mutex> lock(m_mutex_);

  if (m_breakpoints_.find(breakpoint_id) != m_breakpoints_.end())
  {
    return m_breakpoints_[breakpoint_id];
  }
  return std::nullopt;
}

int BreakpointManager::new_breakpoint(pid_t tid, uint64_t address, BreakpointType type, 
  uint32_t original_instruction, BreakpointCondition condition)
{
  // 构建
  int breakpoint_id = m_next_breakpoint_id_++;

  Breakpoint breakpoint(breakpoint_id, tid, address, type, condition);
  breakpoint.enabled = true;
  breakpoint.original_instruction = original_instruction;

  // 加入管理
  m_breakpoints_.emplace(breakpoint_id, breakpoint);
  m_tid_breakpoints_[tid].insert(breakpoint_id);

  LOG_DEBUG("添加断点 [ID: {}, TID: {}, 地址: 0x{:x}]", breakpoint_id, tid, address);

  return breakpoint_id;
}

bool BreakpointManager::check_duplicate_breakpoint(pid_t tid, uint64_t address, BreakpointType type)
{
  for (const auto& [id, breakpoint] : m_breakpoints_)
  {
    if (breakpoint.tid == tid && breakpoint.address == address && breakpoint.type == type)
    {
      LOG_ERROR("线程 {} 地址 0x{:x} 已存在该类型硬件断点", tid, address);
      return true;
    }
  }

  return false;
}