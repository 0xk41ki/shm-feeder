#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

struct ShmHeader {
  std::uint64_t magic;
  std::uint64_t version;
  std::size_t num_slots;
  std::size_t data_offset;
  std::atomic<std::size_t> last_committed_slot;
  std::atomic<std::uint8_t> queue_state;
};

enum QueueState {
  Starting = 0,
  Ready = 1,
  ShuttingDown = 2,
  Uninit = 3,
  Invalid = 255
};

class ProducerHeartbeat {
  pid_t pid_;
  std::atomic<std::uint64_t> heartbeat_;

public:
  ProducerHeartbeat() : pid_(0), heartbeat_(0) {};
  ProducerHeartbeat(pid_t pid, std::uint64_t heartbeat)
      : pid_(pid), heartbeat_(heartbeat) {};
  inline bool is_alive(std::uint64_t now, std::uint64_t tolerance) {
    return now - heartbeat_.load(std::memory_order_acquire) < tolerance;
  };
  inline void update(std::uint64_t now) {
    heartbeat_.store(now, std::memory_order_release);
  };
  inline pid_t get_pid() { return pid_; }
};

struct ShmQueue {
  ShmHeader heaeder;
  ProducerHeartbeat producer_heartbeat;
};
