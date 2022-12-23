#include "indexer/Driver.h"
#include "indexer/Worker.h"

int main(int argc, char *argv[]) {
  // FIXME(def: cli-args) Do argument parsing here, allowing
  // configuration for timeouts, log level, and number of jobs.

  if (argc == 1) {
    return scip_clang::driverMain(argc, argv);
  } else {
    return scip_clang::workerMain(argc, argv);
  }
  return 0;
}
