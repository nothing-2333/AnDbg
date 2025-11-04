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