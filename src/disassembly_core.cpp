#include <cstddef>
#include <cstdint>


#include "capstone/capstone.h"
#include "memory_control.hpp"
#include "disassembly_core.hpp"
#include "log.hpp"


DisassembleCore::DisassembleCore()
{
  // 初始化 capstone 
  if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle) != CS_ERR_OK) 
  {
    LOG_ERROR(" 初始化 capstone 失败 ");
    handle = 0;
    instruction_cache = nullptr;
    return;
  }

  // 启用详细的指令输出
  cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

  // 分配单条指令缓存
  instruction_cache = cs_malloc(handle);
  if (instruction_cache == nullptr) 
  {
    LOG_ERROR(" 分配 capstone 指令缓存失败 ");
    cs_close(&handle);
    handle = 0;
    instruction_cache = nullptr;
    return;
  }
}

DisassembleCore::~DisassembleCore()
{
  if (handle != 0)
  {
    cs_close(&handle);
    handle = 0;
  }
    
  if (instruction_cache != nullptr)
  {
    cs_free(instruction_cache, 1);
    instruction_cache = nullptr;
  }
}

bool DisassembleCore::is_syscall_instruction(const cs_insn& insn)
{
  LOG_DEBUG("没有指令信息使用 id 判断系统调用指令");

  // 使用指令 id 判断系统调用指令
  switch (insn.id) 
  {
    case ARM64_INS_SVC:    // 超级用户调用
    case ARM64_INS_HVC:    // Hypervisor 调用
    case ARM64_INS_SMC:    // 安全监控调用
    case ARM64_INS_SYS:    // 系统指令
    case ARM64_INS_SYSL:   // 系统指令(带加载)
      return true;    

    default:
      return false;
  }
}

bool DisassembleCore::is_interrupt_instruction(const cs_insn& insn)
{
  LOG_DEBUG("没有指令信息使用 id 判断系统调用指令");

  // 使用指令 id 判断系统调用指令
  switch (insn.id) 
  {
    case ARM64_INS_BRK:    // 断点指令
    case ARM64_INS_BRKA:   // 断点指令A
    case ARM64_INS_BRKAS:  // 断点指令A安全
    case ARM64_INS_BRKB:   // 断点指令B
    case ARM64_INS_BRKBS:  // 断点指令B安全
    case ARM64_INS_HLT:    // 停机指令
      return true;
      
    default:
      return false;
  }
}

bool DisassembleCore::is_exception_instruction(const cs_insn& insn)
{
  LOG_DEBUG("没有指令信息使用 id 判断系统调用指令");

  // 使用指令 id 判断系统调用指令
  switch (insn.id) 
  {
    case ARM64_INS_ERET:   // 异常返回
    case ARM64_INS_ERETAA: // 带认证的异常返回
    case ARM64_INS_ERETAB: // 带认证的异常返回B
      return true;
      
    default:
      return false;
  }
}

bool is_unconditional_branch_instruction(const cs_insn& insn)
{
  // 使用指令 id 判断分支指令
  switch (insn.id) 
  {
    case ARM64_INS_B:      // 分支
    case ARM64_INS_BR:     // 分支到寄存器
    case ARM64_INS_BRB:    // 分支到寄存器 B
    case ARM64_INS_BL:     // 带链接的分支
    case ARM64_INS_BLR:    // 带链接的分支到寄存器

      return true;

    default:
      return false;
  }
}

bool is_conditional_branch_instruction(const cs_insn& insn)
{
  // 使用指令 id 判断分支指令
  switch (insn.id) 
  {
    case ARM64_INS_CBZ:   // 比较为零则分支
    case ARM64_INS_CBNZ:  // 比较非零则分支
    case ARM64_INS_TBZ:   // 测试位为零则分支
    case ARM64_INS_TBNZ:  // 测试位非零则分支
    case ARM64_INS_BC:    // 条件分支
    case ARM64_INS_BCAX:  // 条件分支并交换
      return true;

    default:
      return false;
  }
}

bool is_authenticated_branch_instruction(const cs_insn& insn)
{
  // 使用指令 id 判断分支指令
  switch (insn.id) 
  {
    // 带认证的分支指令
    case ARM64_INS_BRAA:   // 带认证的分支到寄存器
    case ARM64_INS_BRAAZ:  // 带认证的分支到寄存器(零寄存器)
    case ARM64_INS_BRAB:   // 带认证的分支到寄存器B
    case ARM64_INS_BRABZ:  // 带认证的分支到寄存器B(零寄存器)
    case ARM64_INS_BLRAA:  // 带认证的链接分支到寄存器
    case ARM64_INS_BLRAAZ: // 带认证的链接分支到寄存器(零寄存器)
    case ARM64_INS_BLRAB:  // 带认证的链接分支到寄存器B
    case ARM64_INS_BLRABZ: // 带认证的链接分支到寄存器B(零寄存器)
      return true;

    default:
      return false;
  }
}

bool is_return_instruction(const cs_insn& insn)
{
  // 使用指令 id 判断分支指令
  switch (insn.id) 
  {
    // 返回指令
    case ARM64_INS_RET:    // 返回
    case ARM64_INS_RETAA:  // 带认证的返回
    case ARM64_INS_RETAB:  // 带认证的返回B
    case ARM64_INS_ERET:   // 异常返回
    case ARM64_INS_ERETAA: // 带认证的异常返回
    case ARM64_INS_ERETAB: // 带认证的异常返回B
    case ARM64_INS_DRPS:   // 调试恢复进程状态

      return true;

    default:
      return false;
  }
}

DisassembleResult DisassembleCore::convert_capstone_instruction(const cs_insn& insn)
{
  InstructionType type;

  if (insn.id == ARM64_INS_INVALID)
  {
    type = InstructionType::UNKNOWN;
  }
  else if (is_unconditional_branch_instruction(insn))
  {
    type = InstructionType::UNCONDITIONAL_BRANCH;
  }
  else if (is_conditional_branch_instruction(insn))
  {
    type = InstructionType::CONDITIONAL_BRANCH;
  }
  else if (is_return_instruction(insn))
  {
    type = InstructionType::RETURN;
  }
  else if (is_authenticated_branch_instruction(insn))
  {
    type = InstructionType::AUTHENTICATED_BRANCH;
  }
  else if (is_syscall_instruction(insn))
  {
    type =  InstructionType::SYSCALL;
  }
  else if (is_interrupt_instruction(insn))
  {
    type = InstructionType::INTERRUPT;
  }
  else if (is_exception_instruction(insn))
  {
    type = InstructionType::EXCEPTION;
  }
  else
  {
    type = InstructionType::NORMAL;
  }

  return DisassembleResult(
    insn.address,
    std::string(insn.mnemonic),
    std::string(insn.op_str),
    type,
    insn.size
  );
}

std::optional<DisassembleResult> DisassembleCore::disassemble_single(pid_t pid, uint64_t address)
{
  if (handle== 0 || !instruction_cache) 
  {
    LOG_ERROR("capstone 未初始化或初始化失败");
    return std::nullopt;
  }

  // arm64 指令最长 16 字节
  uint8_t code[16] = {0};

  // 读取目标进程内存
  bool is_success = MemoryControl::get_instance().read_memory(pid, address, code, sizeof(code));
  if (!is_success)
  {
    LOG_ERROR(" 读取内存失败 地址: 0x%lx ", address);
    return std::nullopt;
  }

  // 反汇编单条指令
  size_t count = cs_disasm(handle, code, sizeof(code), address, 1, &instruction_cache);
  if (count > 0)
  {
    DisassembleResult result = convert_capstone_instruction(instruction_cache[0]);
    return result;
  }

  LOG_ERROR(" 反汇编失败 地址: 0x%lx ", address);
  return std::nullopt;
}

std::optional<std::vector<DisassembleResult>> DisassembleCore::disassemble(pid_t pid, uint64_t address, size_t max_count)
{
  if (handle== 0 || !instruction_cache) 
  {
    LOG_ERROR("capstone 未初始化或初始化失败");
    return std::nullopt;
  }

  std::vector<DisassembleResult> instructions;

  return instructions;
}
