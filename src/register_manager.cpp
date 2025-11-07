#include <cstdint>

#include "utils.hpp"
#include "register_manager.hpp"

RegisterManager::RegisterManager()
{
  gpr_cache = std::make_unique<struct user_pt_regs>();
  fpr_cache = std::make_unique<struct user_fpsimd_state>();
  memset(gpr_cache.get(), 0, sizeof(struct user_pt_regs));
  memset(fpr_cache.get(), 0, sizeof(struct user_fpsimd_state));

  gpr_cache_valid = false;
  fpr_cache_valid = false;
}

bool RegisterManager::ptrace_get_regset(pid_t pid, void* data, size_t size, RegisterSet regset)
{
  struct iovec iov;
  iov.iov_base = data;
  iov.iov_len = size;

  return Utils::ptrace_wrapper(PTRACE_GETREGSET, pid, (void*)(uintptr_t)regset, &iov);
}

bool RegisterManager::ptrace_set_regset(pid_t pid, const void* data, size_t size, RegisterSet regset)
{
  struct iovec iov;
  iov.iov_base = const_cast<void*>(data);
  iov.iov_len = size;

  return Utils::ptrace_wrapper(PTRACE_SETREGSET, pid, (void*)(uintptr_t)regset, &iov);
}

bool RegisterManager::refresh_gpr_cache(pid_t pid)
{
  if (ptrace_get_regset(pid, &gpr_cache, sizeof(gpr_cache), RegisterSet::GPR))
  {
    gpr_cache_valid = true;
    return true;
  }
  else 
  {
    gpr_cache_valid = false;
    return false;
  }
}

bool RegisterManager::refresh_fpr_cache(pid_t pid)
{
  if (ptrace_get_regset(pid, &fpr_cache, sizeof(fpr_cache), RegisterSet::FPR))
  {
    fpr_cache_valid = true;
    return true;
  }
  else 
  {
    fpr_cache_valid = false;
    return false;  
  }
}

bool RegisterManager::commit_gpr_cache(pid_t pid)
{
  if (gpr_cache_valid) return true;
  else if (ptrace_set_regset(pid, &gpr_cache, sizeof(gpr_cache), RegisterSet::GPR))
  {
    gpr_cache_valid = true;
    return true;
  }
  else return false;
}

bool RegisterManager::commit_fpr_cache(pid_t pid)
{
  if (fpr_cache_valid) return true;
  else if (ptrace_set_regset(pid, &fpr_cache, sizeof(fpr_cache), RegisterSet::FPR))
  {
    fpr_cache_valid = true;
    return true;
  }
  else return false;
}

bool RegisterManager::get_all_gpr(pid_t pid, struct user_pt_regs& regs)
{
  if (!gpr_cache_valid && !refresh_gpr_cache(pid)) return false;
  
  regs = *gpr_cache;
  return true;
}

bool RegisterManager::set_all_gpr(pid_t pid, const struct user_pt_regs& regs)
{
  *gpr_cache = regs;
  gpr_cache_valid = false;
  return commit_gpr_cache(pid);
}

uint64_t RegisterManager::get_gpr(pid_t pid, Register reg)
{
  if (!gpr_cache_valid && !refresh_gpr_cache(pid)) return static_cast<uint64_t>(-1);

  switch (reg) 
  {
    case Register::X0:  return gpr_cache->regs[0];
    case Register::X1:  return gpr_cache->regs[1];
    case Register::X2:  return gpr_cache->regs[2];
    case Register::X3:  return gpr_cache->regs[3];
    case Register::X4:  return gpr_cache->regs[4];
    case Register::X5:  return gpr_cache->regs[5];
    case Register::X6:  return gpr_cache->regs[6];
    case Register::X7:  return gpr_cache->regs[7];
    case Register::X8:  return gpr_cache->regs[8];
    case Register::X9:  return gpr_cache->regs[9];
    case Register::X10: return gpr_cache->regs[10];
    case Register::X11: return gpr_cache->regs[11];
    case Register::X12: return gpr_cache->regs[12];
    case Register::X13: return gpr_cache->regs[13];
    case Register::X14: return gpr_cache->regs[14];
    case Register::X15: return gpr_cache->regs[15];
    case Register::X16: return gpr_cache->regs[16];
    case Register::X17: return gpr_cache->regs[17];
    case Register::X18: return gpr_cache->regs[18];
    case Register::X19: return gpr_cache->regs[19];
    case Register::X20: return gpr_cache->regs[20];
    case Register::X21: return gpr_cache->regs[21];
    case Register::X22: return gpr_cache->regs[22];
    case Register::X23: return gpr_cache->regs[23];
    case Register::X24: return gpr_cache->regs[24];
    case Register::X25: return gpr_cache->regs[25];
    case Register::X26: return gpr_cache->regs[26];
    case Register::X27: return gpr_cache->regs[27];
    case Register::X28: return gpr_cache->regs[28];
    case Register::X29: return gpr_cache->regs[29];
    case Register::X30: return gpr_cache->regs[30];
    case Register::SP:  return gpr_cache->sp;
    case Register::PC:  return gpr_cache->pc;
    case Register::CPSR:return gpr_cache->pstate;
    default: return static_cast<uint64_t>(-1);
  }
}

bool RegisterManager::set_gpr(pid_t pid, Register reg, uint64_t value)
{
  switch (reg) {
      case Register::X0:  gpr_cache->regs[0] = value; break;
      case Register::X1:  gpr_cache->regs[1] = value; break;
      case Register::X2:  gpr_cache->regs[2] = value; break;
      case Register::X3:  gpr_cache->regs[3] = value; break;
      case Register::X4:  gpr_cache->regs[4] = value; break;
      case Register::X5:  gpr_cache->regs[5] = value; break;
      case Register::X6:  gpr_cache->regs[6] = value; break;
      case Register::X7:  gpr_cache->regs[7] = value; break;
      case Register::X8:  gpr_cache->regs[8] = value; break;
      case Register::X9:  gpr_cache->regs[9] = value; break;
      case Register::X10: gpr_cache->regs[10] = value; break;
      case Register::X11: gpr_cache->regs[11] = value; break;
      case Register::X12: gpr_cache->regs[12] = value; break;
      case Register::X13: gpr_cache->regs[13] = value; break;
      case Register::X14: gpr_cache->regs[14] = value; break;
      case Register::X15: gpr_cache->regs[15] = value; break;
      case Register::X16: gpr_cache->regs[16] = value; break;
      case Register::X17: gpr_cache->regs[17] = value; break;
      case Register::X18: gpr_cache->regs[18] = value; break;
      case Register::X19: gpr_cache->regs[19] = value; break;
      case Register::X20: gpr_cache->regs[20] = value; break;
      case Register::X21: gpr_cache->regs[21] = value; break;
      case Register::X22: gpr_cache->regs[22] = value; break;
      case Register::X23: gpr_cache->regs[23] = value; break;
      case Register::X24: gpr_cache->regs[24] = value; break;
      case Register::X25: gpr_cache->regs[25] = value; break;
      case Register::X26: gpr_cache->regs[26] = value; break;
      case Register::X27: gpr_cache->regs[27] = value; break;
      case Register::X28: gpr_cache->regs[28] = value; break;
      case Register::X29: gpr_cache->regs[29] = value; break;
      case Register::X30: gpr_cache->regs[30] = value; break;
      case Register::SP:  gpr_cache->sp = value; break;
      case Register::PC:  gpr_cache->pc = value; break;
      case Register::CPSR:gpr_cache->pstate = value; break;
      default: return false;
  }

  gpr_cache_valid = false;
  return commit_gpr_cache(pid);
}

bool RegisterManager::get_all_fpr(pid_t pid, struct user_fpsimd_state& fpr)
{
  if (!fpr_cache_valid && !refresh_fpr_cache(pid)) 
    return false;
  
  fpr = *fpr_cache;
  return true;
}

bool RegisterManager::set_all_fpr(pid_t pid, const struct user_fpsimd_state& fpr)
{
  *fpr_cache = fpr;
  gpr_cache_valid = false;
  return commit_fpr_cache(pid);
}

__uint128_t RegisterManager::get_fpr(pid_t pid, Register reg)
{
  if (!fpr_cache_valid && !refresh_fpr_cache(pid)) 
    return -1;

  switch (reg) 
  {
    case Register::Q0:  return fpr_cache->vregs[0];
    case Register::Q1:  return fpr_cache->vregs[1];
    case Register::Q2:  return fpr_cache->vregs[2];
    case Register::Q3:  return fpr_cache->vregs[3];
    case Register::Q4:  return fpr_cache->vregs[4];
    case Register::Q5:  return fpr_cache->vregs[5];
    case Register::Q6:  return fpr_cache->vregs[6];
    case Register::Q7:  return fpr_cache->vregs[7];
    case Register::Q8:  return fpr_cache->vregs[8];
    case Register::Q9:  return fpr_cache->vregs[9];
    case Register::Q10: return fpr_cache->vregs[10];
    case Register::Q11: return fpr_cache->vregs[11];
    case Register::Q12: return fpr_cache->vregs[12];
    case Register::Q13: return fpr_cache->vregs[13];
    case Register::Q14: return fpr_cache->vregs[14];
    case Register::Q15: return fpr_cache->vregs[15];
    case Register::Q16: return fpr_cache->vregs[16];
    case Register::Q17: return fpr_cache->vregs[17];
    case Register::Q18: return fpr_cache->vregs[18];
    case Register::Q19: return fpr_cache->vregs[19];
    case Register::Q20: return fpr_cache->vregs[20];
    case Register::Q21: return fpr_cache->vregs[21];
    case Register::Q22: return fpr_cache->vregs[22];
    case Register::Q23: return fpr_cache->vregs[23];
    case Register::Q24: return fpr_cache->vregs[24];
    case Register::Q25: return fpr_cache->vregs[25];
    case Register::Q26: return fpr_cache->vregs[26];
    case Register::Q27: return fpr_cache->vregs[27];
    case Register::Q28: return fpr_cache->vregs[28];
    case Register::Q29: return fpr_cache->vregs[29];
    case Register::Q30: return fpr_cache->vregs[30];
    case Register::Q31: return fpr_cache->vregs[31];
    
    default: return -1;
  }
}

bool RegisterManager::set_fpr(pid_t pid, Register reg, __uint128_t value)
{
  if (!fpr_cache_valid && !refresh_fpr_cache(pid)) 
    return false;

  switch (reg) 
  {
    case Register::Q0:  fpr_cache->vregs[0] = value; break;
    case Register::Q1:  fpr_cache->vregs[1] = value; break;
    case Register::Q2:  fpr_cache->vregs[2] = value; break;
    case Register::Q3:  fpr_cache->vregs[3] = value; break;
    case Register::Q4:  fpr_cache->vregs[4] = value; break;
    case Register::Q5:  fpr_cache->vregs[5] = value; break;
    case Register::Q6:  fpr_cache->vregs[6] = value; break;
    case Register::Q7:  fpr_cache->vregs[7] = value; break;
    case Register::Q8:  fpr_cache->vregs[8] = value; break;
    case Register::Q9:  fpr_cache->vregs[9] = value; break;
    case Register::Q10: fpr_cache->vregs[10] = value; break;
    case Register::Q11: fpr_cache->vregs[11] = value; break;
    case Register::Q12: fpr_cache->vregs[12] = value; break;
    case Register::Q13: fpr_cache->vregs[13] = value; break;
    case Register::Q14: fpr_cache->vregs[14] = value; break;
    case Register::Q15: fpr_cache->vregs[15] = value; break;
    case Register::Q16: fpr_cache->vregs[16] = value; break;
    case Register::Q17: fpr_cache->vregs[17] = value; break;
    case Register::Q18: fpr_cache->vregs[18] = value; break;
    case Register::Q19: fpr_cache->vregs[19] = value; break;
    case Register::Q20: fpr_cache->vregs[20] = value; break;
    case Register::Q21: fpr_cache->vregs[21] = value; break;
    case Register::Q22: fpr_cache->vregs[22] = value; break;
    case Register::Q23: fpr_cache->vregs[23] = value; break;
    case Register::Q24: fpr_cache->vregs[24] = value; break;
    case Register::Q25: fpr_cache->vregs[25] = value; break;
    case Register::Q26: fpr_cache->vregs[26] = value; break;
    case Register::Q27: fpr_cache->vregs[27] = value; break;
    case Register::Q28: fpr_cache->vregs[28] = value; break;
    case Register::Q29: fpr_cache->vregs[29] = value; break;
    case Register::Q30: fpr_cache->vregs[30] = value; break;
    case Register::Q31: fpr_cache->vregs[31] = value; break;
    
    default: return false;
  }

  fpr_cache_valid = false;
  return commit_fpr_cache(pid);
}