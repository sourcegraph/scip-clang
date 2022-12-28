#include <chrono>
#include <memory>
#include <vector>

#include "boost/interprocess/ipc/message_queue.hpp"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "indexer/CliOptions.h"
#include "indexer/IpcMessages.h"
#include "indexer/JsonIpcQueue.h"
#include "indexer/LLVMAdapter.h"
#include "indexer/Logging.h"
#include "indexer/Worker.h"

namespace boost_ip = boost::interprocess;

namespace scip_clang {

namespace {

// Type representing the driver<->worker queues, as used by a worker.
struct MessageQueuePair {
  JsonIpcQueue driverToWorker;
  JsonIpcQueue workerToDriver;

  MessageQueuePair(std::string_view driverId, WorkerId workerId) {
    auto d2w = scip_clang::driverToWorkerQueueName(driverId, workerId);
    auto w2d = scip_clang::workerToDriverQueueName(driverId);
    this->driverToWorker =
        JsonIpcQueue(std::make_unique<boost_ip::message_queue>(
            boost_ip::open_only, d2w.c_str()));
    this->workerToDriver =
        JsonIpcQueue(std::make_unique<boost_ip::message_queue>(
            boost_ip::open_only, w2d.c_str()));
  }
};

} // namespace



int workerMain(CliOptions &&cliOptions) {
  BOOST_TRY {
    MessageQueuePair mq(cliOptions.driverId, cliOptions.workerId);

    while (true) {
      IndexJobRequest request{};
      auto recvError =
          mq.driverToWorker.timedReceive(request, cliOptions.receiveTimeout);
      if (recvError.isA<TimeoutError>()) {
        spdlog::error(
            "timeout in worker; is the driver dead?... shutting down");
        break;
      }
      if (recvError) {
        spdlog::error("received malformed message: {}",
                      scip_clang::formatLLVM(recvError));
        continue;
      }
      if (request.id == JobId::Shutdown()) {
        spdlog::debug("shutting down");
        break;
      }
      IndexJobResult result;
      switch (request.job.kind) {
      case IndexJob::Kind::EmitIndex:
        result.emitIndex = EmitIndexJobResult{"lol"};
        break;
      case IndexJob::Kind::SemanticAnalysis:
        result.semanticAnalysis = SemanticAnalysisJobResult{};
        break;
      }
      mq.workerToDriver.send(
          IndexJobResponse{cliOptions.workerId, request.id, result});
    }
  }
  BOOST_CATCH(boost_ip::interprocess_exception & ex) {
    // Don't delete queue from worker; let driver handle that.
    spdlog::error("worker failed {}; exiting from throw!\n", ex.what());
    return 1;
  }
  BOOST_CATCH_END
  spdlog::debug("exiting cleanly");
  return 0;
}

} // namespace scip_clang
