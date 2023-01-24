#ifndef SCIP_CLANG_DERIVE_H
#define SCIP_CLANG_DERIVE_H

#include <compare>
#include <utility>

#include "llvm/Support/JSON.h"

// _expr performs a field or method access on 'self'
#define DERIVE_HASH_1(_SelfType, _expr)                                      \
  template <typename H> friend H AbslHashValue(H h, const _SelfType &self) { \
    return H::combine(std::move(h), _expr);                                  \
  }

#define DERIVE_EQ_ALL(_Type) \
  friend bool operator==(const _Type &, const _Type &) = default;

// For custom derivations of <=> where fields may not support comparison.
#define DERIVE_EQ_VIA_CMP(_Type)                               \
  friend bool operator==(const _Type &lhs, const _Type &rhs) { \
    return (lhs <=> rhs) == 0;                                 \
  }

#define DERIVE_CMP_ALL(_Type)                                             \
  friend std::strong_ordering operator<=>(const _Type &, const _Type &) = \
      default;                                                            \
  DERIVE_EQ_ALL(_Type)                                                    \
  friend bool operator<(const _Type &, const _Type &) = default;

#define DERIVE_HASH_CMP_NEWTYPE(_Type, _Field, _CMP_MACRO)    \
  DERIVE_HASH_1(_Type, self._Field)                           \
  friend std::strong_ordering operator<=>(const _Type &lhs,   \
                                          const _Type &rhs) { \
    _CMP_MACRO(lhs._Field, rhs._Field);                       \
    return std::strong_ordering::equal;                       \
  }                                                           \
  DERIVE_EQ_VIA_CMP(_Type)

#define SERIALIZABLE(T)                \
  llvm::json::Value toJSON(const T &); \
  bool fromJSON(const llvm::json::Value &value, T &, llvm::json::Path path);

#define DERIVE_SERIALIZE_1_NEWTYPE(_Type, _Field)             \
  llvm::json::Value toJSON(const _Type &t) {                  \
    return llvm::json::Value(t._Field);                       \
  }                                                           \
  bool fromJSON(const llvm::json::Value &jsonValue, _Type &t, \
                llvm::json::Path path) {                      \
    decltype(t._Field) _field;                                \
    if (fromJSON(jsonValue, _field, path)) {                  \
      t._Field = std::move(_field);                           \
      return true;                                            \
    }                                                         \
    return false;                                             \
  }

#define DERIVE_SERIALIZE_2(_Type, _Field1, _Field2)           \
  llvm::json::Value toJSON(const _Type &t) {                  \
    return llvm::json::Object{                                \
        {#_Field1, t._Field1},                                \
        {#_Field2, t._Field2},                                \
    };                                                        \
  }                                                           \
  bool fromJSON(const llvm::json::Value &jsonValue, _Type &t, \
                llvm::json::Path path) {                      \
    llvm::json::ObjectMapper mapper(jsonValue, path);         \
    return mapper && mapper.map(#_Field1, t._Field1)          \
           && mapper.map(#_Field2, t._Field2);                \
  }

#endif // SCIP_CLANG_DERIVE_H
