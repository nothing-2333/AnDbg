#pragma omce 

#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <elf.h>
#include <stdint.h>
#include <cstring>
#include <cstddef>
#include <sys/types.h>
#include <cstdint>
#include <memory>
#include <sched.h>


// ARM64 寄存器枚举
enum class Register
{
  // 通用寄存器
  X0, X1, X2, X3, X4, X5, X6, X7, X8, X9,
  X10, X11, X12, X13, X14, X15, X16, X17, X18, X19,
  X20, X21, X22, X23, X24, X25, X26, X27, X28, X29,
  X30, SP, PC, CPSR,
  
  // 别名
  FP = X29,         // 帧指针
  LR = X30,         // 链接寄存器
  PSTATE = CPSR,    // 程序状态寄存器
  
  // 浮点/SIMD 寄存器 (32位)
  S0, S1, S2, S3, S4, S5, S6, S7,
  S8, S9, S10, S11, S12, S13, S14, S15,
  S16, S17, S18, S19, S20, S21, S22, S23,
  S24, S25, S26, S27, S28, S29, S30, S31,
  
  // 双精度浮点寄存器 (64位)
  D0, D1, D2, D3, D4, D5, D6, D7,
  D8, D9, D10, D11, D12, D13, D14, D15,
  D16, D17, D18, D19, D20, D21, D22, D23,
  D24, D25, D26, D27, D28, D29, D30, D31,

  // 完整寄存器 (128位)
  Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7,
  Q8, Q9, Q10, Q11, Q12, Q13, Q14, Q15,
  Q16, Q17, Q18, Q19, Q20, Q21, Q22, Q23,
  Q24, Q25, Q26, Q27, Q28, Q29, Q30, Q31,
  
  // 浮点状态寄存器
  FPSR, FPCR
};

class RegisterManager
{
private:
  // 寄存器缓存结构
  std::unique_ptr<struct user_pt_regs> gpr_cache;   // 通用寄存器
  std::unique_ptr<struct user_fpsimd_state> fpr_cache;    // 浮点/SIMD 寄存器
    
  // 缓存有效性标志
  bool gpr_cache_valid;
  bool fpr_cache_valid;

  // ARM64 寄存器集类型
  enum class RegisterSet {
    GPR = NT_PRSTATUS,        // 通用寄存器
    FPR = NT_FPREGSET,        // 浮点/SIMD 寄存器
  };

  // ptrace PTRACE_GETREGSET 封装
  bool ptrace_get_regset(pid_t pid, void* data, size_t size, RegisterSet regset);

  // ptrace PTRACE_SETREGSET 封装
  bool ptrace_set_regset(pid_t pid, const void* data, size_t size, RegisterSet regset);

  // 刷新通用寄存器缓存
  bool refresh_gpr_cache(pid_t pid);

  // 刷新浮点寄存器缓存
  bool refresh_fpr_cache(pid_t pid);

  // 提交通用寄存器缓存
  bool commit_gpr_cache(pid_t pid);

  // 提交浮点寄存器缓存
  bool commit_fpr_cache(pid_t pid);

public: 
  explicit RegisterManager();

  // 批量获取所有通用寄存器
  bool get_all_gpr(pid_t pid, struct user_pt_regs& regs);

  // 批量设置所有通用寄存器
  bool set_all_gpr(pid_t pid, const struct user_pt_regs& regs);

  // 获取单个通用寄存器
  uint64_t get_gpr(pid_t pid, Register reg);

  // 设置单个通用寄存器
  bool set_gpr(pid_t pid, Register reg, uint64_t value);

  // 批量获取所有浮点寄存器
  bool get_all_fpr(pid_t pid, struct user_fpsimd_state& fpr);

  // 批量设置所有浮点寄存器
  bool set_all_fpr(pid_t pid, const struct user_fpsimd_state& fpr);

  // 获取浮点寄存器值 128 位
  __uint128_t get_fpr(pid_t pid, Register reg);

  // 设置浮点寄存器值 128 位
  bool set_fpr(pid_t pid, Register reg, __uint128_t value);
};
