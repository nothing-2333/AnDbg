#include <asm/ptrace.h>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>

#include "breakpoint_manager.hpp"
#include "log.hpp"
#include "memory_control.hpp"
#include "register_control.hpp"
#include "status.hpp"


namespace Core 
{

BreakpointManager::BreakpointManager()
{
  m_next_breakpoint_id_ = 1;
}


int BreakpointManager::get_hardware_registers_count(pid_t tid)
{
  if (init_hardware_register(tid).is_success())
    return m_hardware_registers_count_[tid];

  return -1;
}

Base::Status BreakpointManager::init_hardware_register(pid_t tid)
{
  if (m_hardware_registers_count_.find(tid) != m_hardware_registers_count_.end()) 
    return Base::Status::success("已经初始化"); 

  m_hardware_registers_count_[tid] = 0;
  const uint64_t test_address = 0x10000000;
  const uint32_t test_control = DBGBCR_ENABLE | DBGBCR_TYPE_EXECUTION | DBGBCR_EL0 | DBGBCR_MATCH_FULL;
  auto& register_control = RegisterControl::get_instance();

  // 获取调试寄存器
  const auto orign_dgb_opt = register_control.get_all_dbg(tid);
  if (!orign_dgb_opt)
    return Base::Status::fail("获取调试寄存器原始状态失败");

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
  if (!register_control.set_all_dbg(tid, test_dbg))
  {
    // 复原, 避免污染目标进程
    register_control.set_all_dbg(tid, orgin_dbg);
    return Base::Status::fail("提交调试寄存器测试值失败");
  }

  // 验证
  const auto verify_dbg_opt = register_control.get_all_dbg(tid);
  if (!verify_dbg_opt)
  {
    register_control.set_all_dbg(tid, orgin_dbg);
    return Base::Status::fail("验证时, 获取调试寄存器失败");
  }
  const auto& verify_dbg = verify_dbg_opt.value();
  for (int index = 0; index < 16; ++index)
  {
    // 地址和控制字都与测试值一致, 说明寄存器存在且可写
    if (verify_dbg.dbg_regs[index].addr == test_address && verify_dbg.dbg_regs[index].ctrl == test_control) 
    {
      m_hardware_registers_count_[tid]++;
      LOG_DEBUG("✅ DBG%d 验证成功", index);
    } 
    else 
    {
      LOG_DEBUG("❌ DBG%d 验证失败", index);
      continue;
    }
  }

  // 原所有寄存器到原始值
  if (!register_control.set_all_dbg(tid, orgin_dbg))
    return Base::Status::fail("set_all_dbg 失败! 目标进程调试寄存器可能被污染");
  else  
    LOG_DEBUG("✅ 调试寄存器已复原到原始状态");

  if (m_hardware_registers_count_[tid] == 0)
    LOG_WARNING("不支持硬件断定, 调试寄存器数量为 0");
  else  
    LOG_DEBUG("调试寄存器数量为 {}", m_hardware_registers_count_[tid]);

  // 初始化空闲寄存器
  for (int i = 0; i < m_hardware_registers_count_[tid]; ++i)
    m_free_hardware_registers_[tid].insert(static_cast<DBRegister>(i));

  return Base::Status::success("init_hardware_register 成功");
}

int BreakpointManager::set_software_breakpoint(pid_t tid, uint64_t address)
{
  auto& memory_control = MemoryControl::get_instance();

  // 入参合法性校验
  if ((address & 0x3) != 0) 
  {
    throw std::invalid_argument("地址 0x" + std::to_string(address) + " 未按 4 字节对齐");
  }

  // 检查是否已经存在断点, 避免重复设置
  if (check_duplicate_breakpoint(address)) return -1;

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
  return new_breakpoint(tid, address, BreakpointType::SOFTWARE, original_instruction);
}

int BreakpointManager::set_hardware_breakpoint(pid_t tid, uint64_t address, BreakpointType type)
{
  // 入参校验
  if ((address & 0x3) != 0) 
    throw std::invalid_argument("地址 0x" + std::to_string(address) + " 未按 4 字节对齐");

  // 检查重复断点
  if (check_duplicate_breakpoint(address)) return -1;

  // 分配硬件寄存器
  if (m_free_hardware_registers_[tid].empty())
  {
    LOG_ERROR("无空闲硬件断点寄存器");
    return -1;
  }

  if (init_hardware_register(tid).is_fail()) return -1;

  // 写入地址寄存器和控制寄存器
  DBRegister reg = *m_free_hardware_registers_[tid].begin();
  m_free_hardware_registers_[tid].erase(reg);

  auto& register_control = RegisterControl::get_instance();
  uint64_t control = DBGBCR_ENABLE | DBGBCR_EL0 | DBGBCR_MATCH_FULL;
  switch (type) 
  {
    case BreakpointType::HARDWARE_EXECUTION: control |= DBGBCR_TYPE_EXECUTION; break;
    case BreakpointType::HARDWARE_WRITE: control |= DBGBCR_TYPE_WRITE; break;
    case BreakpointType::HARDWARE_READWRITE: control |= DBGBCR_TYPE_READWRITE; break;
    default: LOG_ERROR("传入断点类型与调用方法不符"); return -1;
  }

  if (!register_control.set_dbg(tid, reg, {address, control}))
  {
    LOG_ERROR("配置硬件寄存器失败");
    m_free_hardware_registers_[tid].insert(reg);  // 归还寄存器
    return -1;
  }

  // 创建断点, 返回 id
  int id = new_breakpoint(tid, address, type, 0);
  m_breakpoints_[id].hardware_register = reg;

  return id;
}

Base::Status BreakpointManager::remove_breakpoint(int breakpoint_id)
{
  auto& memory_control = MemoryControl::get_instance();

  // 查找断点, 并检查
  auto breakpont_item = m_breakpoints_.find(breakpoint_id);
  if (breakpont_item == m_breakpoints_.end())
    return Base::Status::fail("未找到 ID: {} 的断点", breakpoint_id);

  Breakpoint& breakpoint = breakpont_item->second;

  // 软件断点
  if (breakpoint.type == BreakpointType::SOFTWARE)
  {
    // 恢复原指令
    if (!memory_control.write_memory(breakpoint.tid, breakpoint.address, &breakpoint.original_instruction, 4))
      return Base::Status::fail("恢复软件断点 [ID: {}] 原指令失败", breakpoint_id);
  }
  // 硬件断点
  else if (breakpoint.hardware_register != DBRegister::INVALID) 
  {
    auto& register_control = RegisterControl::get_instance();
    // 禁用断点, 清除 DBGBCR_ENABLE 位
    auto dbg_opt = register_control.get_dbg(breakpoint.tid, breakpoint.hardware_register);
    if (dbg_opt)
    {
      auto [address, control] = dbg_opt.value();
      control &= ~DBGBCR_ENABLE;
      register_control.set_dbg(breakpoint.tid, breakpoint.hardware_register, {address, control});
    }
    // 归还寄存器到空闲集合
    m_free_hardware_registers_[breakpoint.tid].insert(breakpoint.hardware_register);
  }

  // 清理断点元数据
  m_tid_breakpoints_map_[breakpoint.tid].erase(breakpoint.id);
  if (m_tid_breakpoints_map_[breakpoint.tid].empty())
    m_tid_breakpoints_map_.erase(breakpoint.tid);
  m_breakpoints_.erase(breakpont_item);

  return Base::Status::success("成功移除断点: ID = {}, TID = {}, 地址 = 0x{:x}", breakpoint_id, breakpoint.tid, breakpoint.address);
}

Base::Status BreakpointManager::enable(int breakpoint_id)
{
  // 查找断点
  auto breakpoint_item = m_breakpoints_.find(breakpoint_id);
  if (breakpoint_item == m_breakpoints_.end())
    return Base::Status::fail("启用断点失败: 未找到 ID = {} 的断点", breakpoint_id);

  Breakpoint& breakpoint = breakpoint_item->second;

  if (breakpoint.enabled)
    return Base::Status::success("断点 [ID: {}] 已处于启用状态, 无需重复操作", breakpoint_id);

  auto& memory_control = MemoryControl::get_instance();
  auto& register_control = RegisterControl::get_instance();

  // 软件断点, 重新写入断点指令
  if (breakpoint.type == BreakpointType::SOFTWARE)
  {
    if (!memory_control.write_memory(breakpoint.tid, breakpoint.address, &Breakpoint::BRK_OPCODE, 4))
      return Base::Status::fail("写入断点指令失败");
  }
  else if (breakpoint.hardware_register != DBRegister::INVALID) 
  {
    auto dbg_opt = register_control.get_dbg(breakpoint.tid, breakpoint.hardware_register);
    if (!dbg_opt) 
      return Base::Status::fail("获取调试寄存器失败");

    // 置位启用断点
    auto [address, control] = dbg_opt.value();
    control |= DBGBCR_ENABLE; 
    if (!register_control.set_dbg(breakpoint.tid, breakpoint.hardware_register, {address, control}))
      return Base::Status::fail("更新控制寄存器失败");;
  }

  breakpoint.enabled = true;
  return  Base::Status::success("成功启用断点 [ID: {}, TID: {}, 地址: 0x{:x}]", breakpoint_id, breakpoint.tid, breakpoint.address);
}

Base::Status BreakpointManager::disable(int breakpoint_id)
{
  // 查找断点
  auto breakpoint_item = m_breakpoints_.find(breakpoint_id);
  if (breakpoint_item == m_breakpoints_.end()) 
    return Base::Status::fail("禁用断点失败: 未找到 ID = {} 的断点", breakpoint_id);

  Breakpoint& breakpoint = breakpoint_item->second;
  if (!breakpoint.enabled) 
    return Base::Status::success("断点 [ID: {}] 已处于禁用状态, 无需重复操作", breakpoint_id);

  auto& memory_control = MemoryControl::get_instance();
  auto& register_control = RegisterControl::get_instance();

  // 软件断点, 恢复原指令
  if (breakpoint.type == BreakpointType::SOFTWARE)
  {
    if (!memory_control.write_memory(breakpoint.tid, breakpoint.address, &breakpoint.original_instruction, 4))
      return Base::Status::fail("恢复原指令失败");
  }
  else if (breakpoint.hardware_register != DBRegister::INVALID) 
  {
    auto dbg_opt = register_control.get_dbg(breakpoint.tid, breakpoint.hardware_register);
    if (!dbg_opt) 
      return Base::Status::fail("get_dbg 失败");

    // 清除启用位
    auto [address, control] = dbg_opt.value();
    control &= ~DBGBCR_ENABLE;
    if (!register_control.set_dbg(breakpoint.tid, breakpoint.hardware_register, {address, control}))
      return Base::Status::fail("更新控制寄存器失败");
  }

  // 标记为禁用
  breakpoint.enabled = false;
  return Base::Status::success("成功禁用断点 [ID: {}, TID: {}, 地址: 0x{:x}]", breakpoint_id, breakpoint.tid, breakpoint.address);
}

std::vector<Breakpoint> BreakpointManager::get_breakpoints()
{
  std::vector<Breakpoint> result;
  for (const auto& [id, breakpoint] : m_breakpoints_)
  {
    result.push_back(breakpoint);
  }

  LOG_DEBUG("获取所有断点成功, 共 {} 个断点, 返回副本, 外部修改不影响内部状态", result.size());
  return result;
}

std::vector<Breakpoint> BreakpointManager::get_breakpoints(pid_t tid)
{
  std::vector<Breakpoint> result;

  // 现在 m_tid_breakpoints 中寻找
  if (m_tid_breakpoints_map_.find(tid) == m_tid_breakpoints_map_.end())
  {
    LOG_DEBUG("线程 {} 无关联断点", tid);
    return result;
  }

  // 再在 m_breakpoints 取元数据
  for (int breakpoint_id : m_tid_breakpoints_map_[tid])
  {
    auto breakpoint_it = m_breakpoints_.find(breakpoint_id);
    if (breakpoint_it != m_breakpoints_.end()) result.push_back(breakpoint_it->second);
    else LOG_WARNING("线程 {} 的断点 ID {} 不存在", tid, breakpoint_id);
  }

  LOG_DEBUG("获取线程 {} 的断点成功, 共 {} 个断点, 返回副本, 外部修改不影响内部状态", tid, result.size());
  return result;
}

std::optional<Breakpoint> BreakpointManager::get_breakpoint(int breakpoint_id)
{
  if (m_breakpoints_.find(breakpoint_id) != m_breakpoints_.end())
  {
    return m_breakpoints_[breakpoint_id];
  }
  return std::nullopt;
}

std::optional<Breakpoint> BreakpointManager::get_breakpoint(uint64_t address)
{
  auto target = m_address_breakpoint_map_.find(address);
  if (target == m_address_breakpoint_map_.end())
    return get_breakpoint(target->second);
  return std::nullopt;
}

int BreakpointManager::new_breakpoint(pid_t tid, uint64_t address, BreakpointType type, uint32_t original_instruction)
{
  // 构建
  int breakpoint_id = m_next_breakpoint_id_++;

  Breakpoint breakpoint(breakpoint_id, tid, address, type);
  breakpoint.enabled = true;
  breakpoint.original_instruction = original_instruction;

  // 加入管理
  m_breakpoints_.emplace(breakpoint_id, breakpoint);
  m_tid_breakpoints_map_[tid].insert(breakpoint_id);
  m_address_breakpoint_map_[address] = breakpoint_id;

  LOG_DEBUG("添加断点 [ID: {}, TID: {}, 地址: 0x{:x}]", breakpoint_id, tid, address);

  return breakpoint_id;
}

bool BreakpointManager::check_duplicate_breakpoint(uint64_t address)
{
  for (const auto& [id, breakpoint] : m_breakpoints_)
  {
    if (breakpoint.address == address)
      return true;
  }

  return false;
}

}