#ifndef SCIP_CLANG_LLVM_ADAPTER_H
#define SCIP_CLANG_LLVM_ADAPTER_H

#include <string>

#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

namespace scip_clang {

template <typename T> std::string formatLLVM(const T &llvmValue) {
  std::string buffer;
  llvm::raw_string_ostream os(buffer);
  os << llvmValue;
  return buffer;
}

} // namespace scip_clang

#endif // SCIP_CLANG_LLVM_ADAPTER_H