#pragma once

#include <memory>

#include "elf_resolver.hpp"


class ELFInjector 
{
private: 

  std::shared_ptr<ELFResolver> elf_;

public:

  ELFInjector(std::shared_ptr<ELFResolver> elf);

  
};