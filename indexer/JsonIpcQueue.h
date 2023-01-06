#ifndef SCIP_CLANG_JSON_IPC_QUEUE_H
#define SCIP_CLANG_JSON_IPC_QUEUE_H

#include <chrono>
#include <system_error>

#include "boost/interprocess/ipc/message_queue.hpp"

#include "llvm/ADT/Optional.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/IpcMessages.h"

namespace scip_clang {

struct TimeoutError : public llvm::ErrorInfo<TimeoutError> {
  static char ID;
  virtual void log(llvm::raw_ostream &os) const override {
    os << "timeout when receiving from queue";
  }
  virtual std::error_code convertToErrorCode() const override {
    return std::make_error_code(std::errc::timed_out);
  }
};

// I think 1MB should be enough to hold any CLI invocations.
constexpr static size_t IPC_BUFFER_MAX_SIZE = 1 * 1024 * 1024;

class JsonIpcQueue final {
  std::unique_ptr<boost::interprocess::message_queue> queue;

  void sendValue(const llvm::json::Value &t);

  // Tries to wait for waitMillis; if that succeeds, then attempts to parse the
  // result.
  llvm::Expected<llvm::json::Value> timedReceive(uint64_t waitMillis);

public:
  JsonIpcQueue() : queue() {}
  JsonIpcQueue(std::unique_ptr<boost::interprocess::message_queue> queue)
      : queue(std::move(queue)) {}

  template <typename T> void send(const T &t) {
    this->sendValue(llvm::json::Value(t));
  }

  enum class ReceiveStatus {
    Timeout,
    ParseFailure,
    ParseSuccess,
  };

  template <typename T>
  llvm::Error timedReceive(T &t, std::chrono::seconds waitDuration) {
    auto durationMillis =
        std::chrono::duration_cast<std::chrono::milliseconds>(waitDuration)
            .count();
    auto valueOrErr = this->timedReceive(durationMillis);
    if (auto err = valueOrErr.takeError()) {
      return err;
    }
    llvm::json::Path::Root root("ipc-message");
    if (scip_clang::fromJSON(*valueOrErr, t, root)) {
      return llvm::Error::success();
    }
    return root.getError();
  }
};

struct IpcOptions;

// Type representing the driver<->worker queues.
//
// This type doesn't have a forDriver static method because
// the driver has N send queues for N workers.
struct MessageQueuePair {
  JsonIpcQueue driverToWorker;
  JsonIpcQueue workerToDriver;

  static MessageQueuePair forWorker(const IpcOptions &);
};

} // namespace scip_clang

#endif
