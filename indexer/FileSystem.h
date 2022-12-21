#ifndef SCIP_CLANG_FILESYSTEM_H
#define SCIP_CLANG_FILESYSTEM_H

#include <cstdio>

class FileGuard {
  FILE *file;

public:
  FileGuard(FILE *file) : file(file) {}

  ~FileGuard() {
    if (file) {
      fclose(file);
    }
  }
};

#endif // SCIP_CLANG_FILESYSTEM_H