#ifndef SCIP_CLANG_FILESYSTEM_H
#define SCIP_CLANG_FILESYSTEM_H

#include <cstdio>
#include <filesystem>

namespace scip_clang {

using StdPath = std::filesystem::path;

class [[nodiscard]] FileGuard {
  FILE *file;

public:
  FileGuard(FILE *file) : file(file) {}

  ~FileGuard() {
    if (file) {
      fclose(file);
    }
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_FILESYSTEM_H