#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "boost/date_time/microsec_time_clock.hpp"
#include "boost/interprocess/ipc/message_queue.hpp"
#include "boost/process/args.hpp"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"

#include "llvm/ADT/Optional.h"

void checkLLVMWorks() { llvm::Optional<int> x{}; }

namespace boost_ip = boost::interprocess;

struct MessageQueuePair {
  std::unique_ptr<boost_ip::message_queue> driverToWorker;
  std::unique_ptr<boost_ip::message_queue> workerToDriver;

  MessageQueuePair(std::string name, std::pair<size_t, size_t> elementSizes) {
    char buf1[64];
    std::sprintf(buf1, "scip-clang-%s-send", name.c_str());
    this->driverToWorker = std::make_unique<boost_ip::message_queue>(
        boost_ip::create_only, buf1, 1, elementSizes.first);
    char buf2[64];
    std::sprintf(buf2, "scip-clang-%s-recv", name.c_str());
    this->workerToDriver = std::make_unique<boost_ip::message_queue>(
        boost_ip::create_only, buf2, 1, elementSizes.second);
  }

  MessageQueuePair(std::string name) {
    char buf1[64];
    std::sprintf(buf1, "scip-clang-%s-send", name.c_str());
    this->driverToWorker =
        std::make_unique<boost_ip::message_queue>(boost_ip::open_only, buf1);
    char buf2[64];
    std::sprintf(buf2, "scip-clang-%s-recv", name.c_str());
    this->workerToDriver =
        std::make_unique<boost_ip::message_queue>(boost_ip::open_only, buf2);
  }
};

constexpr int SHUTDOWN_VALUE = 69;

boost::posix_time::ptime fromNow(int numMillis) {
  auto now = boost::interprocess::microsec_clock::local_time();
  auto after = now + boost::posix_time::milliseconds(numMillis);
  return after;
}

int workerMain(int argc, char *argv[]) {
  BOOST_TRY {
    // Open a message queue.
    MessageQueuePair mq("0");

    int recv_value;
    size_t recv_count;
    unsigned recv_priority;

    while (mq.driverToWorker->timed_receive(&recv_value, sizeof(recv_value),
                                            recv_count, recv_priority,
                                            fromNow(1000))) {
      assert(recv_count == sizeof(recv_value));
      if (recv_value == SHUTDOWN_VALUE) {
        std::cout << "worker: shutting down\n";
        break;
      }
      int new_value = recv_value + 100;
      std::cout << "worker: received " << recv_value << ", sending "
                << new_value << "\n";
      mq.workerToDriver->send(&new_value, sizeof(new_value), 0);
    }
  }
  BOOST_CATCH(boost_ip::interprocess_exception & ex) {
    boost_ip::message_queue::remove("scip-clang-0-send");
    boost_ip::message_queue::remove("scip-clang-0-recv");
    std::cout << "worker: error: " << ex.what() << std::endl;
    std::cout << "worker: exiting from throw!\n";
    return 1;
  }
  BOOST_CATCH_END
  std::cout << "worker: exiting cleanly\n";
  return 0;
}

int driverMain(int argc, char *argv[]) {
  BOOST_TRY {
    // Erase previous message queue
    boost_ip::message_queue::remove("scip-clang-0-send");
    boost_ip::message_queue::remove("scip-clang-0-recv");

    // Create a message_queue.
    MessageQueuePair mq("0", {sizeof(int), sizeof(int)});

    std::vector<std::string> args;
    args.push_back(std::string(argv[0]));
    args.push_back("--worker");
    boost::process::child worker(args, boost::process::std_out > stdout);
    std::cout << "driver: worker info running = " << worker.running()
              << ", pid = " << worker.id() << "\n";

    for (int i = 99; i < 105; i++) {
      std::cout << "driver: sending " << i << "\n";
      mq.driverToWorker->send(&i, sizeof(i), 1);
      int recv_i;
      size_t recv_count;

      unsigned recv_priority;
      // mq.workerToDriver->receive(&recv_i, 1, recv_count, recv_priority);
      mq.workerToDriver->timed_receive(&recv_i, sizeof(recv_i), recv_count,
                                       recv_priority, fromNow(2000));
      assert(recv_count == sizeof(recv_i));
      std::cout << "driver: received " << recv_i << "\n";
    }
    int shutdown = SHUTDOWN_VALUE;
    mq.driverToWorker->send(&shutdown, sizeof(shutdown), 1);
    worker.wait();
    std::cout << "driver: worker exited, going now kthxbai\n";
  }
  BOOST_CATCH(boost_ip::interprocess_exception & ex) {
    std::cout << "driver: error: " << ex.what() << std::endl;
    return 1;
  }
  BOOST_CATCH_END
  boost_ip::message_queue::remove("scip-clang-0-send");
  boost_ip::message_queue::remove("scip-clang-0-recv");
  return 0;
}

int main(int argc, char *argv[]) {
  std::vector<int> v{1, 2, 3, 4};

  std::cout << "running main\n";

  if (argc == 1) {
    return driverMain(argc, argv);
  } else {
    std::cout << "calling workerMain\n";
    return workerMain(argc, argv);
  }
  return 0;
}
