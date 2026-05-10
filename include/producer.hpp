#pragma once


#include "sys/mman.h"
#include "sys/stat.h"
#include "unistd.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <layout.hpp>
#include <queue.hpp>
#include <string>

#define DEFAULT_LIVENESS_TOLERANCE 1000

std::expected<int, int> try_init_shared_memory(const std::string  &name,
                                               std::size_t size) {
  int memory_fd = shm_open(name.c_str(), O_RDWR | O_EXCL | O_CREAT, 0600);
  if (memory_fd < 0) {
    int last_os_error = errno;
    if (last_os_error == EALREADY) {
      memory_fd = shm_open(name.c_str(), O_RDWR, 0);
      if (memory_fd < 0)
        return std::unexpected(memory_fd);
      // return this fd
      return memory_fd;
    }
    // return last os error
    return std::unexpected(last_os_error);
  }

  int trunc_res = ftruncate(memory_fd, size);
  if (trunc_res < 0) {
    // return last os error
    return std::unexpected(trunc_res);
  }

  // return this fd
  return memory_fd;
}

template <typename T> class Producer {
  void *mmap_ptr_;
  std::size_t mmap_size;
  std::int32_t fd;
  BroadcastWriter<T> *write_handle_;

public:
  Producer(std::string name, std::size_t num_slots, std::uint64_t magic,
           std::uint64_t version, std::uint64_t liveness_tolerance) {
    constexpr std::size_t final_queue_size =
        sizeof(ShmQueue) + sizeof(Slot<T>) * num_slots + alignof(Slot<T>) - 1;
    auto result = try_init_shared_memory(name, final_queue_size);
  }
};

class ProducerBuilder {
  std::string name_;
  std::size_t num_slots_;
  std::uint64_t magic_;
  std::uint64_t version_;
  std::uint64_t liveness_tolerance_;

public:
  ProducerBuilder(std::string name, std::size_t num_slots)
      : name_(name), num_slots_(num_slots), magic_(0), version_(0),
        liveness_tolerance_(DEFAULT_LIVENESS_TOLERANCE) {};
  void with_magic(std::uint64_t magic) { magic_ = magic; };
  void with_version(std::uint64_t version) { version_ = version; };
  void with_liveness_tolerance(std::uint64_t liveness_tolerance) {
    liveness_tolerance_ = liveness_tolerance;
  };

  template <typename T> Producer<T> build() {
    return Producer<T>(std::move(name_), num_slots_, magic_, version_,
                       liveness_tolerance_);
  };
};
