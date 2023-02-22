#include <string>
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

char TimeoutError::ID = 0;

void JsonIpcQueue::sendValue(const llvm::json::Value &jsonValue) {
  auto buffer = scip_clang::formatLlvm(jsonValue);
  this->queue->send(buffer.c_str(), buffer.size(), 1);
  if (buffer.size() > IPC_BUFFER_MAX_SIZE) {
    spdlog::warn("previous message exceeded IPC_BUFFER_MAX_SIZE: {}...{}",
                 buffer.substr(0, 25), buffer.substr(buffer.size() - 25, 25));
  }
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
  readBuffer.resize(IPC_BUFFER_MAX_SIZE);
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
  namespace boost_ip = boost::interprocess;
  MessageQueuePair mqp;
  mqp.driverToWorker = JsonIpcQueue(std::make_unique<boost_ip::message_queue>(
      boost_ip::open_only, d2w.c_str()));
  mqp.workerToDriver = JsonIpcQueue(std::make_unique<boost_ip::message_queue>(
      boost_ip::open_only, w2d.c_str()));
  return mqp;
}

} // namespace scip_clang
