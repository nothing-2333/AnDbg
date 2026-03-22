#include "log.hpp"
#include "utils.hpp"
#include <asm/ptrace.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

#include "register_control.hpp"


namespace Core 
{

// 通用寄存器名称映射
const char* RegisterControl::gpr_names[] = {
  "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
  "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
  "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
  "x24", "x25", "x26", "x27", "x28", "x29", "x30",
  "sp",
  "pc",
  "pstate",
};

// 浮点寄存器名称映射
const char* RegisterControl::fpr_names[] = {
  "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", 
  "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
  "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
  "fpsr",
  "fpcr",
};

// 调试寄存器名称映射
const char* RegisterControl::dbg_names[] = {
    "dbg0", "dbg1", "dbg2", "dbg3", "dbg4", "dbg5", "dbg6", "dbg7",
    "dbg8", "dbg9", "dbg10", "dbg11", "dbg12", "dbg13", "dbg14", "dbg15",
    "dbg_info",
};

bool RegisterControl::ptrace_get_regset(pid_t tid, void* data, size_t size, RegisterType type)
{
  struct iovec iov;
  iov.iov_base = data;
  iov.iov_len = size;

  return Utils::ptrace_wrapper(PTRACE_GETREGSET, tid, 
    reinterpret_cast<void*>(static_cast<unsigned int>(type)), &iov, size);
}

bool RegisterControl::ptrace_set_regset(pid_t tid, const void* data, size_t size, RegisterType type)
{
  struct iovec iov;
  iov.iov_base = const_cast<void*>(data);
  iov.iov_len = size;

  return Utils::ptrace_wrapper(PTRACE_SETREGSET, tid, 
    reinterpret_cast<void*>(static_cast<unsigned int>(type)), &iov, size);
}

std::optional<struct user_pt_regs> RegisterControl::get_all_gpr(pid_t tid)
{
  struct user_pt_regs regs;
  if (ptrace_get_regset(tid, &regs, sizeof(regs), RegisterType::GPR))
  {
    return regs;
  }
  return std::nullopt;
}

bool RegisterControl::set_all_gpr(pid_t tid, const struct user_pt_regs& regs)
{
  return ptrace_set_regset(tid, &regs, sizeof(regs), RegisterType::GPR);
}


std::optional<struct user_fpsimd_state> RegisterControl::get_all_fpr(pid_t tid)
{
  struct user_fpsimd_state fpr;
  if (ptrace_get_regset(tid, &fpr, sizeof(fpr), RegisterType::FPR))
  {
    return fpr;
  }
  return std::nullopt;
}

bool RegisterControl::set_all_fpr(pid_t tid, const struct user_fpsimd_state& fpr)
{
  return ptrace_set_regset(tid, &fpr, sizeof(fpr), RegisterType::FPR);
}

std::optional<struct user_hwdebug_state> RegisterControl::get_all_dbg(pid_t tid)
{
  struct user_hwdebug_state dbg;
  if (ptrace_get_regset(tid, &dbg, sizeof(dbg), RegisterType::DBG)) 
  {
      return dbg;
  }
  return std::nullopt;
}

bool RegisterControl::set_all_dbg(pid_t tid, const struct user_hwdebug_state& dbg)
{
  return ptrace_set_regset(tid, &dbg, sizeof(dbg), RegisterType::DBG);
}

std::optional<uint64_t> RegisterControl::get_gpr(pid_t tid, GPRegister reg)
{
  auto gpr_opt = get_all_gpr(tid);
  if (!gpr_opt) return std::nullopt;
  const auto& gpr = gpr_opt.value();

  auto ptr_opt = get_gpr_pointer(const_cast<user_pt_regs&>(gpr), reg);
  if (!ptr_opt) return std::nullopt;

  return *ptr_opt.value();
}

bool RegisterControl::set_gpr(pid_t tid, GPRegister reg, uint64_t value)
{
  auto offset = get_gpr_offset(reg);
  if (!offset) return false;

  return Utils::ptrace_wrapper(PTRACE_POKEUSER, tid, reinterpret_cast<void*>(offset.value()), 
    reinterpret_cast<void*>(value), sizeof(uint64_t));

  /* 上边的优化不生效可回退此方案
  auto gpr_opt = get_all_gpr(tid);
  if (!gpr_opt) return false;
  auto& gpr = gpr_opt.value();

  auto ptr_opt = get_gpr_pointer(gpr, reg);
  if (!ptr_opt) return false;

  uint64_t* ptr_val = ptr_opt.value();
  *ptr_val = value;
  return set_all_gpr(tid, gpr);
  */
}

std::optional<RegisterControl::FPRValue> RegisterControl::get_fpr(pid_t tid, FPRegister reg)
{
  auto fpr_opt = get_all_fpr(tid);
  if (!fpr_opt) return std::nullopt;
  const auto& fpr = fpr_opt.value();

  auto ptr_opt = get_fpr_pointer(const_cast<user_fpsimd_state&>(fpr), reg);
  if (!ptr_opt) return std::nullopt;
  const auto& ptr_var = ptr_opt.value();

  // 解析 variant 指针, 返回对应的值
  if (std::holds_alternative<__uint128_t*>(ptr_var))
  {
    const __uint128_t* ptr = std::get<__uint128_t*>(ptr_var);
    return FPRValue(*ptr);
  }
  else if (std::holds_alternative<uint32_t*>(ptr_var)) 
  {
    const uint32_t* ptr = std::get<uint32_t*>(ptr_var);
    return FPRValue(*ptr);
  }
  else return std::nullopt;
}

bool RegisterControl::set_fpr(pid_t tid, FPRegister reg, const RegisterControl::FPRValue& value)
{
  // todo: 用 ptrace 的 PTRACE_POKEUSER, 类似 set_gpr, get_fpr_offset 已经写好
  auto fpr_opt = get_all_fpr(tid);
  if (!fpr_opt) return false;
  auto& fpr = fpr_opt.value(); 

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

  return set_all_fpr(tid, fpr);
}

std::optional<std::pair<uint64_t, uint32_t>> RegisterControl::get_dbg(pid_t tid, DBRegister reg)
{
  auto dbg_opt = get_all_dbg(tid);
  if (!dbg_opt) return std::nullopt;
  const auto& dbg = dbg_opt.value();

  auto ptr_opt = get_dbg_pointer(const_cast<user_hwdebug_state&>(dbg), reg);
  if (!ptr_opt) return std::nullopt;

  auto ptr_pair = ptr_opt.value();

  if (ptr_pair.first && ptr_pair.second) 
    return std::make_pair(*ptr_pair.first, *ptr_pair.second);
  else if (ptr_pair.second && !ptr_pair.first)
    return std::make_pair(uint64_t(0), *ptr_pair.second);
  else return std::nullopt;
}

bool RegisterControl::set_dbg(pid_t tid, DBRegister reg, const DBGValue& value)
{
  auto dbg_opt = get_all_dbg(tid);
  if (!dbg_opt) return false;
  auto& dbg = dbg_opt.value();

  auto ptr_opt = get_dbg_pointer(dbg, reg);
  if (!ptr_opt) return false;

  auto ptr_pair = ptr_opt.value();

  if (ptr_pair.first && ptr_pair.second)
  {
    *ptr_pair.first = value.first;
    *ptr_pair.second = value.second;
  }
  else if (ptr_pair.second && !ptr_pair.first)
    *ptr_pair.second = value.second;  // dbg_info 的处理
  else return false;

  return set_all_dbg(tid, dbg);
}

const char* RegisterControl::get_gpr_name(GPRegister reg)
{
  int index = static_cast<int>(reg);
  if (index >= 0 && index < static_cast<int>(GPRegister::MAX_REGISTERS))
    return gpr_names[index];

  return "unknown";
}

const char* RegisterControl::get_fpr_name(FPRegister reg)
{
  int index = static_cast<int>(reg);
  if (index >= 0 && index < static_cast<int>(FPRegister::MAX_REGISTERS))
    return fpr_names[index];
  
  return "unknown";
}

const char* RegisterControl::get_dbg_name(DBRegister reg)
{
  int index = static_cast<int>(reg);
  if (index >= 0 && index < static_cast<int>(DBRegister::MAX_REGISTERS))
    return dbg_names[index];
  
  return "unknown";
}

std::optional<RegisterControl::GPRValuePtr> RegisterControl::get_gpr_pointer(struct user_pt_regs& regs, GPRegister reg)
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
    default: return std::nullopt;
  }
}

std::optional<RegisterControl::FPRValuePtr> RegisterControl::get_fpr_pointer(struct user_fpsimd_state& fpr, FPRegister reg)
{
  if (reg >= FPRegister::V0 && reg <= FPRegister::V31)
  {
    int index = static_cast<int>(reg);
    return FPRValuePtr(&fpr.vregs[index]);
  }

  switch (reg) 
  {
    case FPRegister::FPCR: return FPRValuePtr(reinterpret_cast<uint32_t*>(&fpr.fpcr));
    case FPRegister::FPSR: return FPRValuePtr(reinterpret_cast<uint32_t*>(&fpr.fpsr));
    default: 
      LOG_ERROR("获取浮点寄存器指针失败, 无效寄存器(reg = %d)", static_cast<int>(reg));
      return std::nullopt;
  }
}

std::optional<RegisterControl::DBGValuePtr> RegisterControl::get_dbg_pointer(struct user_hwdebug_state& dbg, DBRegister reg)
{
  int index = static_cast<int>(reg);
  if (index >= static_cast<int>(DBRegister::DBG0) && index <= static_cast<int>(DBRegister::DBG15))
    return std::make_pair(reinterpret_cast<uint64_t*>(&dbg.dbg_regs[index].addr), &dbg.dbg_regs[index].ctrl);

  switch (reg) 
  {
    case DBRegister::DBG_INFO: return std::make_pair(nullptr, &dbg.dbg_info);
    default: return std::nullopt;
  }
}

std::optional<uint64_t> RegisterControl::get_gpr_offset(GPRegister reg)
{
  switch (reg) 
  {
    case GPRegister::X0 ... GPRegister::X30:
      return offsetof(struct user_pt_regs, regs) + static_cast<int>(reg) * sizeof(uint64_t);
      break;
    case GPRegister::SP:
      return offsetof(struct user_pt_regs, sp);
      break;
    case GPRegister::PC:
      return offsetof(struct user_pt_regs, pc);
      break;
    case GPRegister::PSTATE:
      return offsetof(struct user_pt_regs, pstate);
      break;
    default:
      LOG_ERROR("不支持的寄存器: %s", get_gpr_name(reg));
      return std::nullopt;
  }

}

std::optional<uint64_t> RegisterControl::get_fpr_offset(FPRegister reg)
{
  switch (reg) 
  {
    case FPRegister::V0 ... FPRegister::V31: 
    {
      uint64_t base_offset = offsetof(struct user_fpsimd_state, vregs);
      uint64_t elem_offset = static_cast<int>(reg) * sizeof(__uint128_t);
      return base_offset + elem_offset;
    }
    case FPRegister::FPSR:
      return offsetof(struct user_fpsimd_state, fpsr);
    case FPRegister::FPCR:
      return offsetof(struct user_fpsimd_state, fpcr);
    default:
      LOG_ERROR("不支持的寄存器: %d", get_fpr_name(reg));
      return std::nullopt;
  }
}

}