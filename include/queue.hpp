#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

const unsigned long QUEUE_EMPTY_MARKER = SIZE_MAX;

template <typename T> struct Slot {
  T data;
  std::atomic<std::size_t> seq;
};

template <typename T> class Queue {
  Slot<T> *slot_begin_;
  std::size_t len_mask_;
  std::atomic<std::size_t> *last_committed_slot_;

public:
  Queue(Slot<T> *slot_begin, std::size_t len,
        std::atomic<std::size_t> *last_committed_slot)
      : slot_begin_(slot_begin), last_committed_slot_(last_committed_slot) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable");
    assert(((len & (len - 1)) == 0) && "Len must be a power of two");
    len_mask_ = len - 1;
  };
  Queue(std::size_t len) {
    assert(((len & (len - 1)) == 0) && "Len must be a power of two");
    slot_begin_ = (Slot<T> *)malloc(len * sizeof(Slot<T>));
    if (slot_begin_ != nullptr) {
      memset(slot_begin_, 0, len * sizeof(Slot<T>));
    }
    len_mask_ = len - 1;
    last_committed_slot_ =
        (std::atomic<std::size_t> *)malloc(sizeof(std::atomic<std::size_t>));
    last_committed_slot_->store(QUEUE_EMPTY_MARKER);
  };
  Slot<T> *get_slot_at(std::size_t idx) { return (slot_begin_ + idx); };
  std::size_t get_last_committed_slot() {
    return (last_committed_slot_)->load(std::memory_order_acquire);
  };
  void set_last_committed_slot(std::size_t slot) {
    last_committed_slot_->store(slot, std::memory_order_release);
  };

  std::size_t get_len_mask() { return len_mask_; }
};

template <typename T> class BroadcastWriter {
  Queue<T> queue_;
  std::size_t seq_;

public:
  BroadcastWriter(Queue<T> queue) : queue_(queue) {
    std::size_t last_slot_idx = queue.get_last_committed_slot();
    if (last_slot_idx == QUEUE_EMPTY_MARKER) {
      seq_ = 0;
      return;
    }
    Slot<T> *slot = queue.get_slot_at(last_slot_idx);
    seq_ = (slot->seq.load(std::memory_order_acquire)) + 1;
  };
  T *get_next_buffer() {
    auto next_idx =
        (queue_.get_last_committed_slot() + 1) & (queue_.get_len_mask());
    auto next_slot = queue_.get_slot_at(next_idx);

    if (seq_ - next_slot->seq.load(std::memory_order_relaxed) ==
        queue_.get_len_mask() + 1) {
      next_slot->data.~T();
    }

    return &next_slot->data;
  };
  void commit_next_slot() {
    auto next_idx =
        (queue_.get_last_committed_slot() + 1) & (queue_.get_len_mask());
    auto next_slot = queue_.get_slot_at(next_idx);

    next_slot->seq.store(++seq_, std::memory_order_release);
    queue_.set_last_committed_slot(next_idx);
  }
  void write_value(T data) {
    auto buffer = get_next_buffer();
    *buffer = std::move(data);
    commit_next_slot();
  };
  BroadcastWriter(const BroadcastWriter &bw) = delete;
  BroadcastWriter &operator=(const BroadcastWriter &) = delete;
};

template <typename T> class BroadcastReader {
  Queue<T> queue_;
  std::size_t cursor_;
  std::size_t seq_;

public:
  BroadcastReader(Queue<T> queue) : queue_(queue) {
    auto last_slot = queue.get_last_committed_slot();
    seq_ = 0;
    cursor_ = last_slot == QUEUE_EMPTY_MARKER ? 0 : last_slot;
  }
  T *try_read() {
    auto slot = queue_.get_slot_at(cursor_);
    auto slot_seq = slot->seq.load(std::memory_order_acquire);

    if (slot_seq > seq_) {
      seq_ = slot_seq;
      cursor_ = (cursor_ + 1) & (queue_.get_len_mask());

      return &slot->data;
    }

    return nullptr;
  }

  BroadcastReader(const BroadcastReader &br) = delete;
  BroadcastReader &operator=(const BroadcastReader &) = delete;
};
