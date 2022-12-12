#include "indexer/Driver.h"
#include "indexer/Worker.h"

int main(int argc, char *argv[]) {
  // TODO: Make a logger here!
  // TODO: Do argument parsing here

  if (argc == 1) {
    return scip_clang::driverMain(argc, argv);
  } else {
    return scip_clang::workerMain(argc, argv);
  }
  return 0;
}
