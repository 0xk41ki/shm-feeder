#pragma once

#include <cstdint>
#include <expected>
#include <ostream>
#include <string_view>

enum class ErrorCode {
  QueueAlreadyAcquired,
  ProducerDead,
  MagicMismatch,
  VersionMismatch,
  CorruptedQueue,
  InternalOsError
};

constexpr int NO_DATA_MARKER = INT32_MIN;

class ShmError {
  ErrorCode code_;
  // optional data
  int data_;

public:
  explicit ShmError(ErrorCode code) noexcept : code_(code), data_(NO_DATA_MARKER) {};
  ShmError(ErrorCode code, int data) noexcept : code_(code), data_(data) {};

  ErrorCode code() const noexcept { return code_; }

  bool has_data() const noexcept { return data_ != NO_DATA_MARKER; }

  int data() const noexcept { return data_; }
};

template<typename T> using ShmResult = std::expected<T, ShmError>;

constexpr std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::QueueAlreadyAcquired: return "QueueAlreadyAcquired";
    case ErrorCode::ProducerDead:         return "ProducerDead";
    case ErrorCode::MagicMismatch:        return "MagicMismatch";
    case ErrorCode::VersionMismatch:      return "VersionMismatch";
    case ErrorCode::CorruptedQueue:       return "CorruptedQueue";
    case ErrorCode::InternalOsError:      return "InternalOsError";
  }
  return "Unknown";
}

inline std::ostream& operator<<(std::ostream& os, ErrorCode code) {
  return os << to_string(code);
}

inline std::ostream& operator<<(std::ostream& os, const ShmError& e) {
  os << "ShmError(" << e.code();
  if (e.has_data()) os << ", data=" << e.data();
  return os << ')';
}
