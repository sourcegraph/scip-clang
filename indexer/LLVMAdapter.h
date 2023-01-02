#ifndef SCIP_CLANG_LLVM_ADAPTER_H
#define SCIP_CLANG_LLVM_ADAPTER_H

#include <string>
#include <string_view>

#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

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

  template <typename H>
  friend H AbslHashValue(H h, const LLVMToAbslHashAdapter<T> &x) {
    return H::combine(std::move(h), x.data.getHashValue());
  }
  friend bool operator==(const LLVMToAbslHashAdapter<T> &lhs,
                         const LLVMToAbslHashAdapter<T> &rhs) {
    return lhs.data == rhs.data;
  }
  friend bool operator!=(const LLVMToAbslHashAdapter<T> &lhs,
                         const LLVMToAbslHashAdapter<T> &rhs) {
    return !(lhs == rhs);
  }
};

} // namespace scip_clang

#endif // SCIP_CLANG_LLVM_ADAPTER_H