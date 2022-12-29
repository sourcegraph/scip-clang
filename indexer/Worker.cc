#include <chrono>
#include <memory>
#include <vector>

#include "absl/strings/str_cat.h"
#include "boost/interprocess/ipc/message_queue.hpp"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/FileSystem.h"

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

class IndexerASTVisitor : public clang::RecursiveASTVisitor<IndexerASTVisitor> {
  using Base = RecursiveASTVisitor;

  // For the various hierarchies, see clang/Basic/.*.td files
  // https://sourcegraph.com/search?q=context:global+repo:llvm/llvm-project%24+file:clang/Basic/.*.td&patternType=standard&sm=1&groupBy=repo
};

class IndexerASTConsumer : public clang::SemaConsumer {
  clang::Sema *sema;

public:
  IndexerASTConsumer(clang::CompilerInstance &compilerInstance,
                     llvm::StringRef filepath) {}

  void HandleTranslationUnit(clang::ASTContext &astContext) override {
    IndexerASTVisitor visitor{};
    visitor.VisitTranslationUnitDecl(astContext.getTranslationUnitDecl());
  }

  void InitializeSema(clang::Sema &S) override {
    this->sema = &S;
  }

  void ForgetSema() override {
    this->sema = nullptr;
  }
};

class IndexerFrontendAction : public clang::ASTFrontendAction {
public:
  IndexerFrontendAction() {}

  bool usesPreprocessorOnly() const override {
    return false;
  }

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &compilerInstance,
                    llvm::StringRef filepath) override {
    return std::make_unique<IndexerASTConsumer>(compilerInstance, filepath);
  }
};

class IndexerDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &info) override {
    // Just dropping all diagnostics on the floor for now.
    // FIXME(def: surface-diagnostics)
  }
};

} // namespace

static SemanticAnalysisJobResult
performSemanticAnalysis(SemanticAnalysisJobDetails &&job) {
  clang::FileSystemOptions fileSystemOptions;
  fileSystemOptions.WorkingDir = std::move(job.command.Directory);

  llvm::IntrusiveRefCntPtr<clang::FileManager> fileManager(
      new clang::FileManager(fileSystemOptions, nullptr));

  auto args = std::move(job.command.CommandLine);
  args.push_back("-fsyntax-only");   // Only type-checking, no codegen.
  args.push_back("-Wno-everything"); // Warnings aren't helpful.
  // Should we add a CLI flag to pass through extra arguments here?

  // TODO(def: custom-factory): Maybe we should have a custom
  // FrontendActionFactory type so that we can create IndexerFrontendActions
  // with the settings that we need.
  auto frontendActionFactory =
      clang::tooling::newFrontendActionFactory<IndexerFrontendAction>();

  clang::tooling::ToolInvocation Invocation(
      std::move(args), frontendActionFactory.get(), fileManager.get(),
      std::make_shared<clang::PCHContainerOperations>());

  IndexerDiagnosticConsumer diagnosticConsumer;
  Invocation.setDiagnosticConsumer(&diagnosticConsumer);

  {
    LogTimerRAII timer(fmt::format("invocation for {}", job.command.Filename));
    bool ranSuccessfully = Invocation.run();
    (void)ranSuccessfully; // FIXME(ref: surface-diagnostics)
  }

  return SemanticAnalysisJobResult{};
}

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
      result.kind = request.job.kind;
      switch (request.job.kind) {
      case IndexJob::Kind::EmitIndex:
        result.emitIndex = EmitIndexJobResult{"lol"};
        break;
      case IndexJob::Kind::SemanticAnalysis:
        result.semanticAnalysis = scip_clang::performSemanticAnalysis(
            std::move(request.job.semanticAnalysis));
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
