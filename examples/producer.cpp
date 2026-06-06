#include <chrono>
#include <cstdint>
#include <expected>
#include <iostream>
#include <producer.hpp>
#include <thread>

std::uint64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int main() {
  ProducerBuilder producer("some_memory", 16);
  auto queue_result = producer.with_magic(90)
                          .with_version(1)
                          .with_liveness_tolerance(10000)
                          .build<std::uint64_t>(now_ms());

  if (!queue_result.has_value()) {
    std::cout << "failed to create queue:" << queue_result.error() << std::endl;
    return 1;
  }

  auto &queue = queue_result.value();
  std::uint64_t i = 1;
  while (true) {
    std::cout << "writing " << i << std::endl;
    queue.write(i += 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (i % 5 == 0)
      queue.update_heartbeat(now_ms());
  }
}
