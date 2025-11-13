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
#include <sched.h>
#include <optional>
#include <variant>

#include "singleton_base.hpp"


// 通用寄存器索引
enum class GPRegister : int 
{
  X0 = 0, X1, X2, X3, X4, X5, X6, X7,
  X8, X9, X10, X11, X12, X13, X14, X15,
  X16, X17, X18, X19, X20, X21, X22, X23,
  X24, X25, X26, X27, X28, X29, X30,
  SP,             // 栈指针 (X31)
  PC,             // 程序计数器
  PSTATE,         // 处理器状态
  MAX_REGISTERS
};

// 浮点寄存器索引
enum class FPRegister : int 
{
  V0 = 0, V1, V2, V3, V4, V5, V6, V7,
  V8, V9, V10, V11, V12, V13, V14, V15,
  V16, V17, V18, V19, V20, V21, V22, V23,
  V24, V25, V26, V27, V28, V29, V30, V31,
  FPSR, // 浮点状态寄存器
  FPCR, // 浮点控制寄存器
  MAX_REGISTERS
};

// 调试寄存器索引
enum class DBRegister : int {
  DBG0 = 0, DBG1, DBG2, DBG3, DBG4, DBG5, DBG6, DBG7,
  DBG8, DBG9, DBG10, DBG11, DBG12, DBG13, DBG14, DBG15,
  DBG_INFO,  // 调试信息寄存器
  
  MAX_REGISTERS
};

class RegisterControl : public SingletonBase<RegisterControl>
{
private:

  // 友元声明, 允许基类访问子类的私有构造函数
  friend class SingletonBase<RegisterControl>; 

  // 私有构造函数, 析构函数
  RegisterControl() = default;
  ~RegisterControl() = default;

  // ARM64 寄存器集类型
  enum class RegisterType : unsigned int 
  {
    GPR = NT_PRSTATUS,      // 通用寄存器
    FPR = NT_FPREGSET,      // 浮点/SIMD寄存器
    DBG = NT_ARM_HW_BREAK   // 硬件断点调试寄存器
  };

  // 浮点寄存器值类型: 支持 128 位(V0-V31)或 32 位(fpsr/fpcr)
  using FPRValue = std::variant<__uint128_t, uint32_t>;
  using FPRMutablePtr = std::variant<__uint128_t*, uint32_t*>;
  using FPRConstPtr = std::variant<const __uint128_t*, const uint32_t*>;

  // ptrace PTRACE_GETREGSET 封装
  bool ptrace_get_regset(pid_t pid, void* data, size_t size, RegisterType regset);

  // ptrace PTRACE_SETREGSET 封装
  bool ptrace_set_regset(pid_t pid, const void* data, size_t size, RegisterType regset);

  // 辅助函数
  uint64_t* get_gpr_pointer(struct user_pt_regs& regs, GPRegister reg);
  const uint64_t* get_gpr_pointer(const struct user_pt_regs& regs, GPRegister reg) const;

  std::optional<FPRMutablePtr> get_fpr_pointer(struct user_fpsimd_state& fpr, FPRegister reg);
  std::optional<FPRConstPtr> get_fpr_pointer(const struct user_fpsimd_state& fpr, FPRegister reg) const;

  std::pair<uint64_t*, uint32_t*> get_dbg_pointer(struct user_hwdebug_state& dbg, DBRegister reg);
  const std::pair<const uint64_t*, const uint32_t*> get_dbg_pointer(const struct user_hwdebug_state& dbg, DBRegister reg) const;

  // 寄存器名称映射
  static const char* gpr_names[static_cast<int>(GPRegister::MAX_REGISTERS)];
  static const char* fpr_names[static_cast<int>(FPRegister::MAX_REGISTERS)];
  static const char* dbg_names[static_cast<int>(DBRegister::MAX_REGISTERS)];

public: 

  // 获取所有通用寄存器
  std::optional<struct user_pt_regs> get_all_gpr(pid_t pid);

  // 设置所有通用寄存器
  bool set_all_gpr(pid_t pid, const struct user_pt_regs& regs);

  // 获取所有浮点寄存器
  std::optional<struct user_fpsimd_state> get_all_fpr(pid_t pid);

  // 设置所有浮点寄存器
  bool set_all_fpr(pid_t pid, const struct user_fpsimd_state& fpr);

  // 获取所有调试寄存器
  std::optional<struct user_hwdebug_state> get_all_dbg(pid_t pid);

  // 设置所有调试寄存器
  bool set_all_dbg(pid_t pid, const struct user_hwdebug_state& dbg);

  // 获取单个通用寄存器值
  std::optional<uint64_t> get_gpr(pid_t pid, GPRegister reg);

  // 设置单个通用寄存器值
  bool set_gpr(pid_t pid, GPRegister reg, uint64_t value);

  // 获取单个浮点寄存器值 128 位
  std::optional<FPRValue> get_fpr(pid_t pid, FPRegister reg);

  // 设置单个浮点寄存器值 128 位
  bool set_fpr(pid_t pid, FPRegister reg, const FPRValue& value);

  // 获取单个调试寄存器
  std::optional<std::pair<uint64_t, uint32_t>> get_dbg(pid_t pid, DBRegister reg);

  // 设置单个调试寄存器
  bool set_dbg(pid_t pid, DBRegister reg, uint64_t addr, uint32_t ctrl);

  // 获取程序计数器
  inline std::optional<uint64_t> get_pc(pid_t pid);

  // 设置程序计数器
  inline bool set_pc(pid_t pid, uint64_t value);

  // 获取栈指针
  inline std::optional<uint64_t> get_sp(pid_t pid);

  // 设置栈指针
  inline bool set_sp(pid_t pid, uint64_t value);

  // 获取寄存器名称
  static const char* get_gpr_name(GPRegister reg);
  static const char* get_fpr_name(FPRegister reg);
  static const char* get_dbg_name(DBRegister reg);
};
