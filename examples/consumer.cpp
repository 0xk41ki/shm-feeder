#include <chrono>
#include <consumer.hpp>
#include <cstdint>
#include <expected>
#include <iostream>

std::uint64_t now_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

int main() {
  auto queue_result = ConsumerBuilder("some_memory")
                          .with_magic(90)
                          .with_version(1)
                          .with_liveness_tolerance(1000)
                          .build<std::uint64_t>(now_ms());

  if (!queue_result.has_value()) {
    std::cout << "failed to create queue: " << queue_result.error()
              << std::endl;
    return 1;
  }

  auto &queue = queue_result.value();
  std::uint64_t i = 1;
  while (true) {
    bool did_write = queue.try_read_into(&i);
    if (did_write)
      std::cout << "read " << i << std::endl;

    if (!queue.check_producer_liveness(now_ms())) {
      std::cout << "producer died" << std::endl;
      break;
    }
  }
}
