#include "SharedMemorySegment.hpp"
#include <cassert>
#include <memory>
#include <string>
#include <unordered_map>

std::unordered_map<std::string, SharedMemorySegment>& sharedMemorySegments() {
  static std::unordered_map<std::string, SharedMemorySegment> segments;
  return segments;
}

SharedMemorySegment*& currentSharedMemory() {
  static SharedMemorySegment* currentSharedMemoryPtr = nullptr;
  return currentSharedMemoryPtr;
}

void setCurrentSharedMemory(SharedMemorySegment& sharedMemory) {
  currentSharedMemory() = &sharedMemory;
}

void* sharedMemoryMalloc(size_t size) {
  assert(currentSharedMemory != nullptr);
  return currentSharedMemory()->malloc(size);
}

void* sharedMemoryRealloc(void* pointer, size_t size) {
  assert(currentSharedMemory != nullptr);
  return currentSharedMemory()->realloc(pointer, size);
}

void sharedMemoryFree(void* pointer) {
  assert(currentSharedMemory != nullptr);
  currentSharedMemory()->free(pointer);
}

SharedMemorySegment& createOrGetMemorySegment(std::string const& name) {
  return sharedMemorySegments().try_emplace(name, name).first->second;
}
