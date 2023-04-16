#include <string>
#include <system_error>
#include <vector>

#include "boost/date_time/posix_time/posix_time.hpp"
#include "spdlog/spdlog.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/CliOptions.h"
#include "indexer/JsonIpcQueue.h"
#include "indexer/LlvmAdapter.h"

namespace scip_clang {

JsonIpcQueue::~JsonIpcQueue() {
  switch (this->queueInit) {
  case QueueInit::OpenOnly:
    return;
  case QueueInit::CreateOnly:
    if (auto *innerQueue = this->queue.get()) {
      innerQueue->remove(this->name.c_str());
    }
  }
}

char TimeoutError::ID = 0;

[[nodiscard]] std::optional<boost::interprocess::interprocess_exception>
JsonIpcQueue::sendValue(const llvm::json::Value &jsonValue) {
  auto buffer = llvm_ext::format(jsonValue);
  BOOST_TRY {
    this->queue->send(buffer.c_str(), buffer.size(), 1);
  }
  BOOST_CATCH(boost::interprocess::interprocess_exception & ex) {
    if (ex.get_error_code() == boost::interprocess::size_error) {
      ENFORCE(buffer.size() > 25); // it must be kilobytes anyways...
      spdlog::error("message size ({}) exceeded IPC buffer size ({}): {}...{}",
                    buffer.size(), this->queue->get_max_msg_size(),
                    buffer.substr(0, 25),
                    buffer.substr(buffer.size() - 25, 25));
      if (buffer.size() < 10 * 1024 * 1024) {
        spdlog::info(
            "try passing --ipc-size-hint-bytes {} when invoking scip-clang",
            size_t(double(buffer.size()) * 1.5));
      }
    }
    return ex;
  }
  BOOST_CATCH_END
  return {};
}

static boost::posix_time::ptime fromNow(uint64_t durationMillis) {
  // Boost internally uses a spin-sleep loop which compares the passed end
  // instant against the current instant in UTC.
  // https://sourcegraph.com/github.com/boostorg/interprocess@4403b201bef142f07cdc43f67bf6477da5e07fe3/-/blob/include/boost/interprocess/sync/spin/condition.hpp?L171
  // So use universal_time here instead of local_time.
  auto now = boost::posix_time::microsec_clock::universal_time();
  auto after = now + boost::posix_time::milliseconds(durationMillis);
  // Hint: Use boost::posix_time::to_simple_string to debug if needed.
  return after;
}

llvm::Expected<llvm::json::Value>
JsonIpcQueue::timedReceive(uint64_t waitMillis) {
  std::vector<char> readBuffer;
  readBuffer.resize(this->queue->get_max_msg_size());
  size_t recvCount;
  unsigned recvPriority;
  spdlog::debug("will wait for atmost {}ms", waitMillis);
  if (this->queue->timed_receive(readBuffer.data(), readBuffer.size(),
                                 recvCount, recvPriority,
                                 fromNow(waitMillis))) {
    return llvm::json::parse(std::string_view(readBuffer.data(), recvCount));
  }
  return llvm::make_error<TimeoutError>();
}

MessageQueuePair MessageQueuePair::forWorker(const IpcOptions &ipcOptions) {
  auto d2w = scip_clang::driverToWorkerQueueName(ipcOptions.driverId,
                                                 ipcOptions.workerId);
  auto w2d = scip_clang::workerToDriverQueueName(ipcOptions.driverId);
  MessageQueuePair mqp;
  mqp.driverToWorker = JsonIpcQueue::open(std::move(d2w));
  mqp.workerToDriver = JsonIpcQueue::open(std::move(w2d));
  return mqp;
}

} // namespace scip_clang
