#ifndef SCIP_CLANG_WORKER_H
#define SCIP_CLANG_WORKER_H

namespace scip_clang {

struct CliOptions;

int workerMain(CliOptions &&);

} // namespace scip_clang

#endif // SCIP_CLANG_WORKER_H