#include "queue.hpp"
#include <iostream>
#include <thread>
#include <chrono>

const unsigned long NUM_VALUES = 4096;

void producer(BroadcastWriter<unsigned long> &bw) {
  auto count = NUM_VALUES;
  while(count--) {
    std::cout << "writer writing value: " << count << std::endl;
    bw.write_value(count);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void consumer(BroadcastReader<unsigned long> &br) {
  auto count = NUM_VALUES;
  while(count) {
    auto val = br.try_read();
    if (val != nullptr) {
      std::cout << "recvd value " << *val << std::endl;
      count--;
    }
    // std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

int main() {
  Queue<unsigned long> queue(16);

  BroadcastWriter<unsigned long> bw(queue);
  BroadcastReader<unsigned long> br(queue);

  std::thread t1(producer, std::ref(bw));
  std::thread t2(consumer, std::ref(br));

  t1.join();
  t2.join();

  return 0;
}
