#include "queue.hpp"
#include "producer.hpp"
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>

const unsigned long NUM_VALUES = 4096;
std::mutex print_mtx;

void producer(BroadcastWriter<unsigned long> &bw) {
  auto count = NUM_VALUES;
  while(count--) {
    print_mtx.lock();
    std::cout << "writer writing value: " << count << std::endl;
    print_mtx.unlock();
    bw.write_value(count);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void consumer(BroadcastReader<unsigned long> &br, int tid) {
  auto count = NUM_VALUES;
  while(count) {
    auto val = br.try_read();
    if (val != nullptr) {
      print_mtx.lock();
      std::cout << tid << " recvd value " << *val << std::endl;
      print_mtx.unlock();
      count--;
    }
    // std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

int main() {
  Queue<unsigned long> queue(16);

  BroadcastWriter<unsigned long> bw(queue);
  BroadcastReader<unsigned long> br1(queue);
  BroadcastReader<unsigned long> br2(queue);
  BroadcastReader<unsigned long> br3(queue);
  BroadcastReader<unsigned long> br4(queue);

  std::thread t1(producer, std::ref(bw));
  std::thread t2(consumer, std::ref(br1), 1);
  std::thread t3(consumer, std::ref(br2), 2);
  std::thread t4(consumer, std::ref(br3), 3);
  std::thread t5(consumer, std::ref(br4), 4);

  t1.join();
  t2.join();
  t3.join();
  t4.join();
  t5.join();

  return 0;
}
