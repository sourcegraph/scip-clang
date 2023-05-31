#include <string>

#include "llvm/Support/raw_ostream.h"

#include "indexer/Enforce.h"
#include "indexer/FileSystem.h"

namespace scip_clang {

std::string readFileToString(const StdPath &path, std::error_code &ec) {
  llvm::raw_fd_stream stream(path.c_str(), ec);
  if (ec) {
    return "";
  }
  std::string out;
  char buf[16384] = {0};
  while (true) {
    auto nread = stream.read(buf, sizeof(buf));
    if (nread == 0) {
      break;
    }
    if (nread == -1) {
      ec = stream.error();
      return "";
    }
    ENFORCE(nread >= 1);
    out.append(std::string_view(buf, nread));
  }
  return out;
}

} // namespace scip_clang