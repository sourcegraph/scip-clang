#include <string>
#include <vector>

#include "boost/date_time/posix_time/posix_time.hpp"
#include "spdlog/spdlog.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/JsonIpcQueue.h"
#include "indexer/LLVMAdapter.h"

namespace scip_clang {

char TimeoutError::ID = 0;

void JsonIpcQueue::sendValue(const llvm::json::Value &jsonValue) {
  auto buffer = scip_clang::formatLLVM(jsonValue);
  this->queue->send(buffer.c_str(), buffer.size(), 1);
  if (buffer.size() > IPC_BUFFER_MAX_SIZE) {
    spdlog::warn("previous message exceeded IPC_BUFFER_MAX_SIZE: {}...{}",
                 buffer.substr(0, 25), buffer.substr(buffer.size() - 25, 25));
  }
}

boost::posix_time::ptime fromNow(uint64_t durationMillis) {
  auto now = boost::posix_time::microsec_clock::local_time();
  auto after = now + boost::posix_time::milliseconds(durationMillis);
  return after;
}

llvm::Expected<llvm::json::Value>
JsonIpcQueue::timedReceive(uint64_t waitMillis) {
  std::vector<char> readBuffer;
  readBuffer.resize(IPC_BUFFER_MAX_SIZE);
  size_t recvCount;
  unsigned recvPriority;
  spdlog::info("will wait for atmost {}ms", waitMillis);
  if (this->queue->timed_receive(readBuffer.data(), readBuffer.size(),
                                 recvCount, recvPriority,
                                 fromNow(waitMillis))) {
    return llvm::json::parse(std::string_view(readBuffer.data(), recvCount));
  }
  return llvm::make_error<TimeoutError>();
}

} // end namespace scip_clang
