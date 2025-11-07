#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "memory_manager.hpp"
#include "utils.hpp"

bool MemoryManager::read_memory_ptrace(pid_t pid, uint64_t address, void* buffer, size_t size)
{
  uint8_t* byte_buffer = static_cast<uint8_t*>(buffer);
  size_t bytes_read = 0;

  while (bytes_read < size)
  {
    long word;
    if (Utils::ptrace_wrapper(PTRACE_PEEKDATA, pid, 
      reinterpret_cast<void*>(address + bytes_read), nullptr, 0, &word))
      return false;
      
    size_t copy_size = std::min(sizeof(word), size - bytes_read);
    memcpy(byte_buffer + bytes_read, &word, copy_size);
    bytes_read += copy_size;
  }

  return true;
}

bool MemoryManager::write_memory_ptrace(pid_t pid, uint64_t address, void* buffer, size_t size)
{
  
}