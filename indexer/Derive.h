#ifndef SCIP_CLANG_DERIVE_H
#define SCIP_CLANG_DERIVE_H

// _expr performs a field or method access on 'self'
#define DERIVE_HASH_1(_SelfType, _expr)                                      \
  template <typename H> friend H AbslHashValue(H h, const _SelfType &self) { \
    return H::combine(std::move(h), _expr);                                  \
  }

// _expr performs a field access on 'self'
#define DERIVE_EQ_1(_Type, _expr)                              \
  friend bool operator==(const _Type &lhs, const _Type &rhs) { \
    auto &self = lhs;                                          \
    auto &lf = _expr;                                          \
    {                                                          \
      auto &self = rhs;                                        \
      auto &rf = _expr;                                        \
      return lf == rf;                                         \
    }                                                          \
  }                                                            \
  friend bool operator!=(const _Type &lhs, const _Type &rhs) { \
    return !(lhs == rhs);                                      \
  }

// _expr performs a field access on 'self'
#define DERIVE_CMP_1(_Type, _expr)                            \
  DERIVE_EQ_1(_Type, _expr)                                   \
  friend bool operator<(const _Type &lhs, const _Type &rhs) { \
    auto &self = lhs;                                         \
    auto &lf = _expr;                                         \
    {                                                         \
      auto &self = rhs;                                       \
      auto &rf = _expr;                                       \
      return lf < rf;                                         \
    }                                                         \
  }

// _expr performs a field access on 'self'
#define DERIVE_HASH_EQ_1(_Type, _expr) \
  DERIVE_HASH_1(_Type, _expr)          \
  DERIVE_EQ_1(_Type, _expr)

// _expr performs a field access on 'self'
#define DERIVE_HASH_CMP_1(_Type, _expr) \
  DERIVE_HASH_1(_Type, _expr)           \
  DERIVE_CMP_1(_Type, _expr)

#endif