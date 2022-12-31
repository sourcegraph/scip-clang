#ifndef SCIP_CLANG_HASH_H
#define SCIP_CLANG_HASH_H

#include <cstdint>
#include <utility>

#include "absl/hash/hash.h"
#include "wyhash.h"

namespace scip_clang {

class HashValue {
    uint64_t value;

    HashValue(uint64_t v): value(v) {}
public:
    HashValue(): value(0) {}

    HashValue mix(const uint8_t *key, size_t data) {
        return HashValue(wyhash(key, data, this->value, /*default secret*/_wyp));
    };

    template <typename H>
    friend H AbslHashValue(H h, const HashValue &hv) {
        return H::combine(std::move(h), hv.value);
    }
    friend bool operator==(const HashValue &lhs, const HashValue &rhs) {
        return lhs.value == rhs.value;
    }
    friend bool operator!=(const HashValue &lhs, const HashValue &rhs) {
        return !(lhs == rhs);
    }
};

template <typename T>
class HashConsed {
    uint64_t value;
    T _data;

public:
    HashConsed() { return HashConsed(T()); };
    HashConsed(HashConsed<T> &&) = default;
    HashConsed &operator=(HashConsed<T> &&) = default;
    HashConsed(const HashConsed<T> &) = default;
    HashConsed &operator=(const HashConsed<T> &) = default;

    HashConsed(T &&data): value(absl::HashOf(data)), _data(data) {}

    template <typename H>
    friend H AbslHashValue(H h, const HashConsed<T> &hc) {
        return H::combine(std::move(h), hc.value);
    }

    friend bool operator==(const HashConsed<T>& lhs, const HashConsed<T>& rhs) {
        return lhs.value == rhs.value && lhs.data() == rhs.data();
    }
    friend bool operator!=(const HashConsed<T>& lhs, const HashConsed<T>& rhs) {
        return !(lhs == rhs);
    }

    const T &data() const {
        return this->_data;
    }
};

} // namespace scip_clang

#endif // SCIP_CLANG_HASH_H