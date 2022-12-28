#ifndef SCIP_CLANG_DRIVER_H
#define SCIP_CLANG_DRIVER_H

namespace scip_clang {

struct CliOptions;

int driverMain(CliOptions &&);

} // namespace scip_clang

#endif // SCIP_CLANG_DRIVER_H