#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
        std::atomic<std::size_t> *last_committed_slot) {
    this->slot_begin_ = slot_begin;
    this->len_mask_ = len - 1;
    this->last_committed_slot_ = last_committed_slot;
  };
  Queue(std::size_t len) {
    this->slot_begin_ = (Slot<T> *)malloc(len * sizeof(Slot<T>));
    if (this->slot_begin_ != nullptr) {
      memset(this->slot_begin_, 0, len * sizeof(Slot<T>));
    }
    this->len_mask_ = len - 1;
    this->last_committed_slot_ =
        (std::atomic<std::size_t> *)malloc(sizeof(std::atomic<std::size_t>));
    this->last_committed_slot_->store(SIZE_MAX);
  };
  Slot<T> *get_slot_at(std::size_t idx) { return (this->slot_begin_ + idx); };
  std::size_t get_last_committed_slot() {
    return (this->last_committed_slot_)->load(std::memory_order_acquire);
  };
  void set_last_committed_slot(std::size_t slot) {
    this->last_committed_slot_->store(slot, std::memory_order_release);
  };

  const std::size_t get_len_mask() { return this->len_mask_; }
};

template <typename T> class BroadcastWriter {
  Queue<T> queue_;
  std::atomic<std::size_t> seq_;

public:
  BroadcastWriter(Queue<T> queue) : queue_(queue) {
    std::size_t last_slot_idx = queue.get_last_committed_slot();
    Slot<T> *slot = queue.get_slot_at(last_slot_idx);
    this->seq_ = (slot->seq.load(std::memory_order_acquire)) + 1;
  };
  T *get_next_buffer() {
    auto next_idx = (this->queue_.get_last_committed_slot() + 1) &
                    (this->queue_.get_len_mask());
    auto next_slot = this->queue_.get_slot_at(next_idx);

    if (this->seq_ - next_slot->seq.load(std::memory_order_relaxed) ==
        this->queue_.get_len_mask() + 1) {
      next_slot->data.~T();
    }

    return &next_slot->data;
  };
  void commit_next_slot() {
    auto next_idx = (this->queue_.get_last_committed_slot() + 1) &
                    (this->queue_.get_len_mask());
    auto next_slot = this->queue_.get_slot_at(next_idx);

    next_slot->seq.store(++this->seq_, std::memory_order_release);
    this->queue_.set_last_committed_slot(next_idx);
  }
  void write_value(T data) {
    auto buffer = this->get_next_buffer();
    *buffer = std::move(data);
    this->commit_next_slot();
  };
  BroadcastWriter(BroadcastWriter &br) = delete;
};

template <typename T> class BroadcastReader {
  Queue<T> queue_;
  std::size_t cursor_;
  std::size_t seq_;

public:
  BroadcastReader(Queue<T> queue) : queue_(queue) {
    auto last_slot = queue.get_last_committed_slot();
    this->seq_ = 0;
    this->cursor_ = last_slot == SIZE_MAX ? 0 : last_slot;
  }
  T *try_read() {
    auto slot = this->queue_.get_slot_at(this->cursor_);
    auto slot_seq = slot->seq.load(std::memory_order_acquire);

    if (slot_seq > this->seq_) {
      this->seq_ = slot_seq;
      this->cursor_ = (this->cursor_ + 1) & (this->queue_.get_len_mask());

      return &slot->data;
    }

    return nullptr;
  }

  BroadcastReader(BroadcastReader &br) = delete;
};
