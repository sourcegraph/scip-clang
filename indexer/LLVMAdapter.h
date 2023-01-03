#ifndef SCIP_CLANG_LLVM_ADAPTER_H
#define SCIP_CLANG_LLVM_ADAPTER_H

#include <string>
#include <string_view>

#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include "indexer/Derive.h"

namespace scip_clang {

template <typename T> std::string formatLLVM(const T &llvmValue) {
  std::string buffer;
  llvm::raw_string_ostream os(buffer);
  os << llvmValue;
  return buffer;
}

inline std::string_view toStringView(llvm::StringRef sref) {
  return std::string_view(sref.data(), sref.size());
}

template <typename T> struct LLVMToAbslHashAdapter final {
  T data;

  DERIVE_HASH_1(LLVMToAbslHashAdapter<T>, self.data.getHashValue())
  DERIVE_EQ_1(LLVMToAbslHashAdapter<T>, self.data)
};

} // namespace scip_clang

#endif // SCIP_CLANG_LLVM_ADAPTER_H