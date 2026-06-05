#pragma once

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

enum class _PrivateConnectedMemoryType { Old, New };

std::expected<ShmQueue *, int> try_map_memory(const int fd,
                                              const size_t size) noexcept {
  void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (ptr == MAP_FAILED) {
    int last_os_error = errno;
    close(fd);
    return std::unexpected(last_os_error);
  }

  return static_cast<ShmQueue *>(ptr);
}

std::expected<std::tuple<_PrivateConnectedMemoryType, int>, int>
try_init_shared_memory(const std::string &name, std::size_t size) noexcept {
  int memory_fd = shm_open(name.c_str(), O_RDWR | O_EXCL | O_CREAT, 0600);
  if (memory_fd < 0) {
    int last_os_error = errno;
    if (last_os_error == EEXIST) {
      memory_fd = shm_open(name.c_str(), O_RDWR, 0);
      if (memory_fd < 0)
        return std::unexpected(memory_fd);
      return std::tuple(_PrivateConnectedMemoryType::Old, memory_fd);
    }
    return std::unexpected(last_os_error);
  }

  int trunc_res = ftruncate(memory_fd, size);
  if (trunc_res < 0) {
    return std::unexpected(trunc_res);
  }
  return std::tuple(_PrivateConnectedMemoryType::New, memory_fd);
}

static inline size_t align_ptr_up(size_t ptr, size_t align) {
  return ((ptr + align - 1) & ~(align - 1));
}

std::expected<ShmQueue *, int>
try_setup_new_memory(ShmQueue *queue, const size_t size,
                     const uint64_t num_slots, const uint64_t magic_number,
                     const uint64_t version, const size_t slot_align,
                     const uint64_t now_timestamp) noexcept {
  memset(reinterpret_cast<void *>(queue), 0, size);

  queue->header.queue_state.store(QueueState::Starting,
                                  std::memory_order_release);

  queue->header.magic = magic_number;
  queue->header.version = version;
  queue->header.num_slots = num_slots;
  queue->header.last_committed_slot.store(0, std::memory_order_relaxed);

  size_t queue_begin = reinterpret_cast<size_t>(queue);
  size_t queue_end = queue_begin + sizeof(ShmQueue);
  queue->header.data_offset = align_ptr_up(queue_end, slot_align) - queue_begin;

  new (&queue->producer_heartbeat) ProducerHeartbeat(getpid(), now_timestamp);

  queue->header.queue_state.store(QueueState::Ready, std::memory_order_release);
  return queue;
}

std::expected<ShmQueue *, int>
try_setup_old_memory(ShmQueue *queue, const size_t size,
                     const uint64_t num_slots, const uint64_t magic_number,
                     const uint64_t version, const uint64_t liveness_tolerance,
                     const uint64_t now_timestamp) noexcept {

  int state = queue->header.queue_state.load(std::memory_order_acquire);
  if (state == QueueState::Ready || state == QueueState::Starting ||
      state == QueueState::ShuttingDown) {
    if (queue->producer_heartbeat.is_alive(now_timestamp, liveness_tolerance))
      return std::unexpected(0); // queue already acquired
    if (queue->header.magic != magic_number)
      return std::unexpected(0); // queue corrupted
    if (queue->header.version != version)
      return std::unexpected(0); // version mismatch
    if (queue->header.num_slots != num_slots)
      return std::unexpected(0); // queue corrupted
  } else if (state == QueueState::Invalid)
    return std::unexpected(0); // queue is in invalid state

  new (&queue->producer_heartbeat) ProducerHeartbeat(getpid(), now_timestamp);

  queue->header.queue_state.store(QueueState::Ready, std::memory_order_release);
  return queue;
}

template <typename T> class Producer {
  void *mmap_ptr_;
  std::size_t mmap_size_;
  std::int32_t fd_;
  BroadcastWriter<T> write_handle_;

  Producer(void *mmap_ptr, std::size_t mmap_size, std::int32_t fd,
           BroadcastWriter<T> write_handle)
      : mmap_ptr_(mmap_ptr), mmap_size_(mmap_size), fd_(fd),
        write_handle_(std::move(write_handle)) {};

public:
  static std::expected<Producer, int>
  create(std::string name, std::size_t num_slots, std::uint64_t magic,
         std::uint64_t version, std::uint64_t liveness_tolerance,
         std::uint64_t now_timestamp) noexcept {
    const std::size_t final_queue_size =
        sizeof(ShmQueue) + sizeof(Slot<T>) * num_slots + alignof(Slot<T>) - 1;
    std::expected<std::tuple<_PrivateConnectedMemoryType, int>, int>
        init_result = try_init_shared_memory(name, final_queue_size);

    if (!init_result.has_value())
      return std::unexpected(init_result.error());

    auto [memory_type, fd] = init_result.value();

    return try_map_memory(fd, final_queue_size)
        .and_then([&](ShmQueue *queue) {
          if (memory_type == _PrivateConnectedMemoryType::New)
            return try_setup_new_memory(queue, final_queue_size, num_slots,
                                        magic, version, alignof(Slot<T>),
                                        now_timestamp);
          return try_setup_old_memory(queue, final_queue_size, num_slots, magic,
                                      version, liveness_tolerance,
                                      now_timestamp);
        })
        .transform([&](ShmQueue *queue) {
          Slot<T> *data_begin = reinterpret_cast<Slot<T> *>(
              reinterpret_cast<char *>(queue) + queue->header.data_offset);

          return Producer<T>(
              static_cast<void *>(queue), final_queue_size, fd,
              BroadcastWriter<T>(Queue<T>(data_begin, queue->header.num_slots,
                                          &queue->header.last_committed_slot)));
        });
  }

  void update_heartbeat(std::uint64_t now) noexcept {
    reinterpret_cast<ShmQueue*>(mmap_ptr_)->producer_heartbeat.update(now);
  }

  T* get_next_slot() noexcept {
    return write_handle_.get_next_buffer();
  }

  void commit_next_slot() noexcept {
    write_handle_.commit_next_slot();
  }

  void write(const T data) noexcept {
    write_handle_.write_value(data);
  }

  ~Producer() {
    munmap(mmap_ptr_, mmap_size_);
    close(fd_);
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
  ProducerBuilder with_magic(std::uint64_t magic) {
    magic_ = magic;
    return *this;
  };
  ProducerBuilder with_version(std::uint64_t version) {
    version_ = version;
    return *this;
  };
  ProducerBuilder with_liveness_tolerance(std::uint64_t liveness_tolerance) {
    liveness_tolerance_ = liveness_tolerance;
    return *this;
  };

  template <typename T>
  std::expected<Producer<T>, int> build(std::uint64_t now_timestamp) noexcept {
    return Producer<T>::create(std::move(name_), num_slots_, magic_, version_,
                       liveness_tolerance_, now_timestamp);
  };
};
