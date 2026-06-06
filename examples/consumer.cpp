#include <chrono>
#include <consumer.hpp>
#include <cstdint>
#include <expected>
#include <iostream>

std::uint64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int main() {
  ConsumerBuilder consumer("some_memory");
  auto queue_result = consumer.with_magic(90)
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
    std::uint64_t *res = queue.unsafe_try_read();
    if (res != nullptr)
      std::cout << "read " << *res << std::endl;

    if (!queue.check_producer_liveness(now_ms())) {
      std::cout << "producer died" << std::endl;
      break;
    }
  }
}
