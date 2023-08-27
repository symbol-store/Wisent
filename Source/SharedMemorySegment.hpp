#pragma once
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <memory>
#include <string>
#include <unordered_map>

using namespace boost::interprocess;

/* Not a general implementation: assuming always a single allocation! */
class SharedMemorySegment {
private:
  shared_memory_object object;
  std::unique_ptr<mapped_region> region;

public:
  SharedMemorySegment(std::string const& name)
      : object(open_or_create, name.c_str(), read_write), region(nullptr) {}
  SharedMemorySegment(SharedMemorySegment&& other) = default;
  ~SharedMemorySegment() = default;

  SharedMemorySegment(SharedMemorySegment const& other) = delete;
  SharedMemorySegment& operator=(SharedMemorySegment const& other) = delete;
  SharedMemorySegment& operator=(SharedMemorySegment&& other) = delete;

  void* malloc(size_t size) {
    assert(!loaded());
    object.truncate(size);
    load();
    return baseAddress();
  }

  void* realloc(void* pointer, size_t size) {
    assert(loaded());
    assert(pointer == baseAddress());
    unload();
    object.truncate(size);
    load();
    return baseAddress();
  }

  void free(void* pointer) {
    assert(pointer == baseAddress());
    unload();
    erase();
  }

  void erase() {
    unload();
    shared_memory_object::remove(object.get_name());
  }

  void load() { region = std::make_unique<mapped_region>(object, read_write); }
  void unload() { region.reset(); }

  bool exists() const {
    offset_t size;
    return object.get_size(size) && size > 0;
  }

  bool loaded() const {
    return exists() && region.get() != nullptr;
  }

  void* baseAddress() const {
    assert(loaded());
    return region->get_address();
  }

  size_t size() const {
    assert(loaded());
    return region->get_size();
  }
};

std::unordered_map<std::string, SharedMemorySegment>& sharedMemorySegments();
SharedMemorySegment*& currentSharedMemory();
void setCurrentSharedMemory(SharedMemorySegment& sharedMemory);
void* sharedMemoryMalloc(size_t size);
void* sharedMemoryRealloc(void* pointer, size_t size);
void sharedMemoryFree(void* pointer);
SharedMemorySegment& createOrGetMemorySegment(std::string const& name);
