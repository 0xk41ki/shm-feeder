#pragma once

#include "errors.hpp"
#include "sys/mman.h"
#include "unistd.h"
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <layout.hpp>
#include <queue.hpp>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>

static constexpr uint64_t DEFAULT_LIVENESS_TOLERANCE = 1000;

ShmResult<ShmQueue *> try_map_memory(const int fd,
                                              const size_t size) noexcept {
  void *ptr = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
  if (ptr == MAP_FAILED) {
    int last_os_error = errno;
    close(fd);
    return std::unexpected(ShmError(ErrorCode::InternalOsError, last_os_error));
  }

  return static_cast<ShmQueue *>(ptr);
}

ShmResult<int>
try_attach_shared_memory(const std::string &name) noexcept {
  int memory_fd = shm_open(name.c_str(), O_RDONLY, 0);
  if (memory_fd < 0) {
    return std::unexpected(ShmError(ErrorCode::InternalOsError, errno));
  }

  return memory_fd;
}

ShmResult<ShmQueue *>
try_verify_memory(ShmQueue *queue, const uint64_t magic_number,
                  const uint64_t version, const uint64_t liveness_tolerance,
                  const uint64_t now_timestamp) noexcept {

  int state = queue->header.queue_state.load(std::memory_order_acquire);
  if (state == QueueState::Ready) {
    if (!queue->producer_heartbeat.is_alive(now_timestamp, liveness_tolerance))
      return std::unexpected(ShmError(ErrorCode::ProducerDead, queue->producer_heartbeat.get_pid()));
    if (queue->header.magic != magic_number)
      return std::unexpected(ShmError(ErrorCode::MagicMismatch, queue->header.magic));
    if (queue->header.version != version)
      return std::unexpected(ShmError(ErrorCode::VersionMismatch, queue->header.version));
  } else
    return std::unexpected(ShmError(ErrorCode::CorruptedQueue));

  return queue;
}

template <typename T> class Consumer {
  void *mmap_ptr_;
  std::size_t mmap_size_;
  std::int32_t fd_;
  std::uint64_t liveness_tolerance_;
  BroadcastReader<T> read_handle_;

  Consumer(void *mmap_ptr, std::size_t mmap_size, std::int32_t fd,
           std::uint64_t liveness_tolerance, BroadcastReader<T> read_handle)
      : mmap_ptr_(mmap_ptr), mmap_size_(mmap_size), fd_(fd),
        liveness_tolerance_(liveness_tolerance),
        read_handle_(std::move(read_handle)) {};

public:
  static ShmResult<Consumer>
  create(std::string name, std::uint64_t magic, std::uint64_t version,
         std::uint64_t liveness_tolerance,
         std::uint64_t now_timestamp) noexcept {
    ShmResult<int> attach_result = try_attach_shared_memory(name);

    if (!attach_result.has_value())
      return std::unexpected(attach_result.error());

    int fd = attach_result.value();

    return try_map_memory(fd, sizeof(ShmQueue))
        .and_then([&](ShmQueue *queue) {
          return try_verify_memory(queue, magic, version, liveness_tolerance,
                                   now_timestamp);
        })
        .and_then([&](ShmQueue *queue) -> ShmResult<ShmQueue *> {
          std::size_t final_size = sizeof(Slot<T>) * queue->header.num_slots +
                                   queue->header.data_offset;
          int res = munmap(static_cast<void *>(queue), sizeof(ShmQueue));
          if (res < 0)
            return std::unexpected(ShmError(ErrorCode::InternalOsError, errno));
          return try_map_memory(fd, final_size);
        })
        .transform([&](ShmQueue *queue) {
          Slot<T> *data_begin = reinterpret_cast<Slot<T> *>(
              reinterpret_cast<char *>(queue) + queue->header.data_offset);

          std::size_t final_size = sizeof(Slot<T>) * queue->header.num_slots +
                                   queue->header.data_offset;
          return Consumer<T>(
              static_cast<void *>(queue), final_size, fd, liveness_tolerance,
              BroadcastReader<T>(Queue<T>(data_begin, queue->header.num_slots,
                                          &queue->header.last_committed_slot)));
        });
  }

  bool check_producer_liveness(std::uint64_t now) noexcept {
    return reinterpret_cast<ShmQueue *>(mmap_ptr_)->producer_heartbeat.is_alive(
        now, liveness_tolerance_);
  }

  T *unsafe_try_read() noexcept { return read_handle_.try_read(); }

  T *unsafe_read() noexcept {
    T *result = nullptr;
    while ((result = unsafe_try_read()) == nullptr) {
    };
    return result;
  }

  bool try_read_into(T *buf) {
    T *result = unsafe_try_read();
    if (result == nullptr)
      return false;

    *buf = *result;
    return true;
  }

  void read_into(T *buf) {
    T *result = unsafe_read();
    *buf = *result;
  }

  ~Consumer() {
    munmap(mmap_ptr_, mmap_size_);
    close(fd_);
  }
};

class ConsumerBuilder {
  std::string name_;
  std::uint64_t magic_;
  std::uint64_t version_;
  std::uint64_t liveness_tolerance_;

public:
  ConsumerBuilder(std::string name)
      : name_(name), magic_(0), version_(0),
        liveness_tolerance_(DEFAULT_LIVENESS_TOLERANCE) {};
  ConsumerBuilder with_magic(std::uint64_t magic) {
    magic_ = magic;
    return *this;
  };
  ConsumerBuilder with_version(std::uint64_t version) {
    version_ = version;
    return *this;
  };
  ConsumerBuilder with_liveness_tolerance(std::uint64_t liveness_tolerance) {
    liveness_tolerance_ = liveness_tolerance;
    return *this;
  };

  template <typename T>
  ShmResult<Consumer<T>> build(std::uint64_t now_timestamp) noexcept {
    return Consumer<T>::create(std::move(name_), magic_, version_,
                               liveness_tolerance_, now_timestamp);
  };
};
