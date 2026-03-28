#include <algorithm>
#include <cstddef>
#include <cstdint>


#include "capstone/capstone.h"
#include "assembly.hpp"
#include "log.hpp"


namespace Assembly 
{

DisassemblyControl::DisassemblyControl()
{
  // 初始化 capstone 
  if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle_) != CS_ERR_OK) 
  {
    LOG_ERROR(" 初始化 capstone 失败 ");
    handle_ = 0;
    return;
  }

  // 启用详细的指令输出
  cs_option(handle_, CS_OPT_DETAIL, CS_OPT_ON);
}

DisassemblyControl::~DisassemblyControl()
{
  if (handle_ != 0)
  {
    cs_close(&handle_);
    handle_ = 0;
  }
}

bool DisassemblyControl::is_syscall_instruction(const cs_insn& insn)
{
  // 使用指令 id 判断系统调用指令
  switch (insn.id) 
  {
    case ARM64_INS_SVC:    // 超级用户调用
      return true;    

    default:
      return false;
  }
}

bool DisassemblyControl::is_unconditional_branch_instruction(const cs_insn& insn)
{
  // 使用指令 id 判断分支指令
  switch (insn.id) 
  {
    case ARM64_INS_B: 
    case ARM64_INS_BR:  
    case ARM64_INS_BRB:  
    case ARM64_INS_BL:  
    case ARM64_INS_BLR:  
    // 带认证的分支指令
    case ARM64_INS_BRAA:  
    case ARM64_INS_BRAAZ: 
    case ARM64_INS_BRAB:  
    case ARM64_INS_BRABZ:
    case ARM64_INS_BLRAA:  
    case ARM64_INS_BLRAAZ: 
    case ARM64_INS_BLRAB:  
    case ARM64_INS_BLRABZ: 
      return true;

    default:
      return false;
  }
}

bool DisassemblyControl::is_conditional_branch_instruction(const cs_insn& insn)
{
  // 使用指令 id 判断分支指令
  switch (insn.id) 
  {
    case ARM64_INS_CBZ:   // 比较为零则分支
    case ARM64_INS_CBNZ:  // 比较非零则分支
    case ARM64_INS_TBZ:   // 测试位为零则分支
    case ARM64_INS_TBNZ:  // 测试位非零则分支
    case ARM64_INS_BC:    // 条件分支
      return true;

    default:
      return false;
  }
}

bool DisassemblyControl::is_return_instruction(const cs_insn& insn)
{
  // 使用指令 id 判断分支指令
  switch (insn.id) 
  {
    // 返回指令
    case ARM64_INS_RET:    // 返回
    case ARM64_INS_RETAA:  // 带认证的返回
    case ARM64_INS_RETAB:  // 带认证的返回B
      return true;

    default:
      return false;
  }
}

// 线程反汇编, 当遇到跳转指令时重新反汇编
std::optional<std::vector<Instruction>> DisassemblyControl::disassemble(const std::vector<char>& codes)
{
  if (handle_== 0) 
  {
    LOG_ERROR("capstone 未初始化或初始化失败");
    return std::nullopt;
  }
  if (codes.empty())
  {
    LOG_ERROR("指令数据为空");
    return std::nullopt;
  }

  cs_insn* insn_array = nullptr;
  size_t count = 0;

  count = cs_disasm(handle_, (const uint8_t*)codes.data(), 
  codes.size(), 0, 0, &insn_array);

  if (count == 0 || insn_array == nullptr)
  {
    LOG_ERROR("反汇编失败，无指令解析");
    return std::nullopt;
  }

  std::vector<Instruction> results;
  results.reserve(count);

  for (size_t i = 0; i < count; i++)
  {
    const cs_insn& insn = insn_array[i];
    Instruction::Type type;
    if (insn.id == ARM64_INS_INVALID)
      type = Instruction::Type::UNKNOWN;
    else if (is_unconditional_branch_instruction(insn))
      type = Instruction::Type::UNCONDITIONAL_BRANCH;
    else if (is_conditional_branch_instruction(insn))
      type = Instruction::Type::CONDITIONAL_BRANCH;
    else if (is_return_instruction(insn))
      type = Instruction::Type::RETURN;
    else if (is_syscall_instruction(insn))
      type =  Instruction::Type::SYSCALL;
    else
      type = Instruction::Type::NORMAL;

    std::vector<char> data(insn.bytes, insn.bytes + insn.size);

    Operand ops[8] = {};
    uint8_t op_count = 0;

    if (insn.detail != nullptr)
    {
      const cs_arm64& arm64 = insn.detail->arm64;
      op_count = std::min(arm64.op_count, (uint8_t)8);
      for (uint8_t j = 0; j < op_count; j++)
      {
        const cs_arm64_op& src = arm64.operands[j];
        Operand& dst = ops[j];

        switch (src.type)
        {
          case ARM64_OP_REG:
          case ARM64_OP_REG_MRS:
          case ARM64_OP_REG_MSR:
          case ARM64_OP_SVCR:
            dst.type = Operand::type::REG;
            dst.reg = src.reg;
            break;

          case ARM64_OP_IMM:
          case ARM64_OP_CIMM:
          case ARM64_OP_FP:
          case ARM64_OP_PSTATE:
          case ARM64_OP_SYS:
          case ARM64_OP_PREFETCH:
          case ARM64_OP_BARRIER:
            dst.type = Operand::type::IMM;
            dst.imm = src.imm;
            break;

          case ARM64_OP_MEM:
            dst.type = Operand::type::MEM;
            dst.reg = src.mem.base;    // 基址
            dst.reg2 = src.mem.index;  // 变址
            dst.imm = src.mem.disp;    // 偏移
            break;

          default:
            dst.type = Operand::type::OTHER;
            dst.reg = 0;
            dst.reg2 = 0;
            dst.imm = 0;
            break;
        }
      }
    }
    else  
    {
      LOG_ERROR("insn.detail 缺失");
      cs_free(insn_array, count);
      return std::nullopt;
    }
      
    results.emplace_back(type, data, insn.mnemonic, insn.op_str, ops, op_count);
  }

  // 用完必须手动释放
  cs_free(insn_array, count);

  return std::move(results);
}


}
