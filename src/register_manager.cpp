#include "Log.hpp"
#include "memory_manager.hpp"
#include "utils.hpp"
#include <asm/ptrace.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include "register_manager.hpp"


// 通用寄存器名称映射
const char* RegisterManager::gpr_names[] = {
  "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
  "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
  "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
  "x24", "x25", "x26", "x27", "x28", "x29", "x30",
  "sp",
  "pc",
  "pstate",
};

// 浮点寄存器名称映射
const char* RegisterManager::fpr_names[] = {
  "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", 
  "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
  "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
  "fpsr",
  "fpcr",
};

// 调试寄存器名称映射
const char* RegisterManager::dbg_names[] = {
    "dbg0", "dbg1", "dbg2", "dbg3", "dbg4", "dbg5", "dbg6", "dbg7",
    "dbg8", "dbg9", "dbg10", "dbg11", "dbg12", "dbg13", "dbg14", "dbg15",
    "dbg_info",
};

bool RegisterManager::ptrace_get_regset(pid_t pid, void* data, size_t size, RegisterType regset)
{
  struct iovec iov;
  iov.iov_base = data;
  iov.iov_len = size;

  return Utils::ptrace_wrapper(PTRACE_GETREGSET, pid, 
    reinterpret_cast<void*>(static_cast<unsigned int>(regset)), &iov, size);
}

bool RegisterManager::ptrace_set_regset(pid_t pid, const void* data, size_t size, RegisterType regset)
{
  struct iovec iov;
  iov.iov_base = const_cast<void*>(data);
  iov.iov_len = size;

  return Utils::ptrace_wrapper(PTRACE_SETREGSET, pid, 
    reinterpret_cast<void*>(static_cast<unsigned int>(regset)), &iov, size);
}

std::optional<struct user_pt_regs> RegisterManager::get_all_gpr(pid_t pid)
{
  struct user_pt_regs regs;
  if (ptrace_get_regset(pid, &regs, sizeof(regs), RegisterType::GPR))
  {
    return regs;
  }
  return std::nullopt;
}

bool RegisterManager::set_all_gpr(pid_t pid, const struct user_pt_regs& regs)
{
  return ptrace_set_regset(pid, &regs, sizeof(regs), RegisterType::GPR);
}


std::optional<struct user_fpsimd_state> RegisterManager::get_all_fpr(pid_t pid)
{
  struct user_fpsimd_state fpr;
  if (ptrace_get_regset(pid, &fpr, sizeof(fpr), RegisterType::FPR))
  {
    return fpr;
  }
  return std::nullopt;
}

bool RegisterManager::set_all_fpr(pid_t pid, const struct user_fpsimd_state& fpr)
{
  return ptrace_set_regset(pid, &fpr, sizeof(fpr), RegisterType::FPR);
}

std::optional<struct user_hwdebug_state> RegisterManager::get_all_dbg(pid_t pid)
{
  struct user_hwdebug_state dbg;
  if (ptrace_get_regset(pid, &dbg, sizeof(dbg), RegisterType::DBG)) 
  {
      return dbg;
  }
  return std::nullopt;
}

bool RegisterManager::set_all_dbg(pid_t pid, const struct user_hwdebug_state& dbg)
{
  return ptrace_set_regset(pid, &dbg, sizeof(dbg), RegisterType::DBG);
}

std::optional<uint64_t> RegisterManager::get_gpr(pid_t pid, GPRegister reg)
{
  auto gpr_opt = get_all_gpr(pid);
  if (!gpr_opt) return std::nullopt;
  const auto& gpr = gpr_opt.value();

  // 调用 const 版本
  const uint64_t* value_ptr = get_gpr_pointer(gpr, reg);
  if (value_ptr) return std::nullopt;

  return *value_ptr;
}

bool RegisterManager::set_gpr(pid_t pid, GPRegister reg, uint64_t value)
{
  auto gpr_opt = get_all_gpr(pid);
  if (!gpr_opt) return false;
  auto& gpr = gpr_opt.value();

  // 调用非 const 版本
  uint64_t* value_ptr = get_gpr_pointer(gpr, reg);
  if (!value_ptr) return false;

  *value_ptr = value;
  return set_all_gpr(pid, gpr);
}

std::optional<RegisterManager::FPRValue> RegisterManager::get_fpr(pid_t pid, FPRegister reg)
{
  auto fpr_opt = get_all_fpr(pid);
  if (!fpr_opt) return std::nullopt;
  const auto& fpr = fpr_opt.value();

  // 调用 const 版本 get_fpr_pointer, 获取只读指针
  auto ptr_opt = get_fpr_pointer(fpr, reg);
  if (!ptr_opt) return std::nullopt;
  const auto& ptr_var = ptr_opt.value();

  // 解析 variant 指针, 返回对应的值
  if (std::holds_alternative<const __uint128_t*>(ptr_var))
  {
    const __uint128_t* ptr = std::get<const __uint128_t*>(ptr_var);
    return FPRValue(*ptr);
  }
  else if (std::holds_alternative<const uint32_t*>(ptr_var)) 
  {
    const uint32_t* ptr = std::get<const uint32_t*>(ptr_var);
    return FPRValue(*ptr);
  }
  else return std::nullopt;
}

// 设置单个浮点寄存器值
bool RegisterManager::set_fpr(pid_t pid, FPRegister reg, const RegisterManager::FPRValue& value)
{
  auto fpr_opt = get_all_fpr(pid);
  if (!fpr_opt) return false;
  auto& fpr = fpr_opt.value(); 

  // 调用非 const 版本 get_fpr_pointer, 获取可修改指针
  auto ptr_opt = get_fpr_pointer(fpr, reg);
  if (!ptr_opt) return false;
  const auto& ptr_var = ptr_opt.value();

  // 解析指针类型, 与输入值类型匹配后赋值
  if (std::holds_alternative<__uint128_t*>(ptr_var) && std::holds_alternative<__uint128_t>(value))
  {
    __uint128_t* ptr = std::get<__uint128_t*>(ptr_var);
    *ptr = std::get<__uint128_t>(value);
  }
  else if (std::holds_alternative<uint32_t*>(ptr_var) && std::holds_alternative<uint32_t>(value)) 
  {
    uint32_t* ptr = std::get<uint32_t*>(ptr_var);
    *ptr = std::get<uint32_t>(value);
  }
  else return false;

  return set_all_fpr(pid, fpr);
}

// 获取单个调试寄存器
std::optional<std::pair<uint64_t, uint32_t>> RegisterManager::get_dbg(pid_t pid, DBRegister reg)
{
  auto dbg_opt = get_all_dbg(pid);
  if (!dbg_opt) return std::nullopt;
  const auto& dbg = dbg_opt.value();

  // 调用 const 版本
  const std::pair<const uint64_t*, const uint32_t*> ptr_pair = get_dbg_pointer(dbg, reg);

  if (ptr_pair.first && ptr_pair.second) 
    return std::make_pair(*ptr_pair.first, *ptr_pair.second);
  else if (ptr_pair.second && !ptr_pair.first)
    return std::make_pair(uint64_t(0), *ptr_pair.second);
  else return std::nullopt;
}

// 设置单个调试寄存器
bool RegisterManager::set_dbg(pid_t pid, DBRegister reg, uint64_t addr, uint32_t ctrl)
{
  auto dbg_opt = get_all_dbg(pid);
  if (!dbg_opt) return false;
  auto& dbg = dbg_opt.value();

  // 调用非 const 版本
  std::pair<uint64_t*, uint32_t*> ptr_pair = get_dbg_pointer(dbg, reg);

  if (ptr_pair.first && ptr_pair.second)
  {
    *ptr_pair.first = addr;
    *ptr_pair.second = ctrl;
  }
  else if (ptr_pair.second && !ptr_pair.first)
    *ptr_pair.second = ctrl;
  else return false;

  return set_all_dbg(pid, dbg);
}

std::optional<uint64_t> RegisterManager::get_pc(pid_t pid)
{
  return get_gpr(pid, GPRegister::PC);
}

// 设置程序计数器
bool RegisterManager::set_pc(pid_t pid, uint64_t value)
{
  return set_gpr(pid, GPRegister::PC, value);
}

// 获取栈指针
std::optional<uint64_t> RegisterManager::get_sp(pid_t pid)
{
  return get_gpr(pid, GPRegister::SP);
}

// 设置栈指针
bool RegisterManager::set_sp(pid_t pid, uint64_t value)
{
  return set_gpr(pid, GPRegister::SP, value);
}

const char* RegisterManager::get_gpr_name(GPRegister reg)
{
  int index = static_cast<int>(reg);
  if (index >= 0 && index < static_cast<int>(GPRegister::MAX_REGISTERS))
    return gpr_names[index];

  return "unknown";
}

const char* RegisterManager::get_fpr_name(FPRegister reg)
{
  int index = static_cast<int>(reg);
  if (index >= 0 && index < static_cast<int>(FPRegister::MAX_REGISTERS))
    return fpr_names[index];
  
  return "unknown";
}

const char* RegisterManager::get_dbg_name(DBRegister reg)
{
  int index = static_cast<int>(reg);
  if (index >= 0 && index < static_cast<int>(DBRegister::MAX_REGISTERS))
    return dbg_names[index];
  
  return "unknown";
}

uint64_t* RegisterManager::get_gpr_pointer(struct user_pt_regs& regs, GPRegister reg)
{
  if (reg >= GPRegister::X0 && reg <= GPRegister::X30)
  {
    int index = static_cast<int>(reg);
    return reinterpret_cast<uint64_t*>(&regs.regs[index]);
  }

  switch (reg) 
  {
    case GPRegister::SP: return reinterpret_cast<uint64_t*>(&regs.sp);
    case GPRegister::PC: return reinterpret_cast<uint64_t*>(&regs.pc);
    case GPRegister::PSTATE: return reinterpret_cast<uint64_t*>(&regs.pstate);
    default: return nullptr;
  }
}

const uint64_t* RegisterManager::get_gpr_pointer(const struct user_pt_regs& regs, GPRegister reg) const
{
  if (reg >= GPRegister::X0 && reg <= GPRegister::X30)
  {
    int index = static_cast<int>(reg);
    return reinterpret_cast<const uint64_t*>(&regs.regs[index]);
  }

  switch (reg) 
  {
    case GPRegister::SP: return reinterpret_cast<const uint64_t*>(&regs.sp);
    case GPRegister::PC: return reinterpret_cast<const uint64_t*>(&regs.pc);
    case GPRegister::PSTATE: return reinterpret_cast<const uint64_t*>(&regs.pstate);
    default: return nullptr;
  }
}

std::optional<RegisterManager::FPRMutablePtr> RegisterManager::get_fpr_pointer(struct user_fpsimd_state& fpr, FPRegister reg)
{
  if (reg >= FPRegister::V0 && reg <= FPRegister::V31)
  {
    int index = static_cast<int>(reg);
    return FPRMutablePtr(&fpr.vregs[index]);
  }

  switch (reg) 
  {
    case FPRegister::FPCR: return FPRMutablePtr(reinterpret_cast<uint32_t*>(&fpr.fpcr));
    case FPRegister::FPSR: return FPRMutablePtr(reinterpret_cast<uint32_t*>(&fpr.fpsr));
    default: 
      LOG_ERROR("获取浮点寄存器指针失败, 无效寄存器(reg = %d)", static_cast<int>(reg));
      return std::nullopt;
  }
}

std::optional<RegisterManager::FPRConstPtr> RegisterManager::get_fpr_pointer(const struct user_fpsimd_state& fpr, FPRegister reg) const
{
  if (reg >= FPRegister::V0 && reg <= FPRegister::V31)
  {
    int index = static_cast<int>(reg);
    return FPRConstPtr(&fpr.vregs[index]);
  }

  switch (reg) 
  {
    case FPRegister::FPCR: return FPRConstPtr(reinterpret_cast<const uint32_t*>(&fpr.fpcr));
    case FPRegister::FPSR: return FPRConstPtr(reinterpret_cast<const uint32_t*>(&fpr.fpsr));
    default: 
      LOG_ERROR("获取浮点寄存器指针失败, 无效寄存器(reg = %d)", static_cast<int>(reg));
      return std::nullopt;
  }
}

std::pair<uint64_t*, uint32_t*> RegisterManager::get_dbg_pointer(struct user_hwdebug_state& dbg, DBRegister reg)
{
  int index = static_cast<int>(reg);
  if (index >= static_cast<int>(DBRegister::DBG0) && index <= static_cast<int>(DBRegister::DBG15))
    return std::make_pair(reinterpret_cast<uint64_t*>(&dbg.dbg_regs[index].addr), &dbg.dbg_regs[index].ctrl);

  switch (reg) 
  {
    case DBRegister::DBG_INFO: return std::make_pair(nullptr, &dbg.dbg_info);
    default: return std::make_pair(nullptr, nullptr);
  }
}

const std::pair<const uint64_t*, const uint32_t*> RegisterManager::get_dbg_pointer(const struct user_hwdebug_state& dbg, DBRegister reg) const
{
  int index = static_cast<int>(reg);
  if (index >= static_cast<int>(DBRegister::DBG0) && index <= static_cast<int>(DBRegister::DBG15))
    return std::make_pair(reinterpret_cast<const uint64_t*>(&dbg.dbg_regs[index].addr), &dbg.dbg_regs[index].ctrl);

  switch (reg) 
  {
    case DBRegister::DBG_INFO: return std::make_pair(nullptr, &dbg.dbg_info);
    default: return std::make_pair(nullptr, nullptr);
  }
}