#ifndef SCIP_CLANG_ABSL_EXTRAS_H
#define SCIP_CLANG_ABSL_EXTRAS_H

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"

namespace scip_clang {

template <typename K, typename V>
void extractTransform(absl::flat_hash_map<K, V> &&map, bool deterministic,
                      absl::FunctionRef<void(K &&, V &&)> f) {
  if (!deterministic) {
    for (auto it = map.begin(); it != map.end(); it = map.begin()) {
      auto node = map.extract(it);
      f(std::move(node.key()), std::move(node.mapped()));
    }
    return;
  }
  std::vector<std::pair<K, V>> entries;
  entries.reserve(map.size());
  for (auto it = map.begin(); it != map.end(); it = map.begin()) {
    auto node = map.extract(it);
    entries.emplace_back(std::move(node.key()), std::move(node.mapped()));
  }
  absl::c_sort(entries, [](const auto &p1, const auto &p2) -> bool {
    return p1.first < p2.first;
  });
  for (auto &[k, v] : entries) {
    f(std::move(k), std::move(v));
  }
}

template <typename T>
void extractTransform(absl::flat_hash_set<T> &&set, bool deterministic,
                      absl::FunctionRef<void(T &&)> f) {
  if (!deterministic) {
    for (auto it = set.begin(); it != set.end(); it = set.begin()) {
      auto node = set.extract(it);
      f(std::move(node.value()));
    }
    return;
  }
  std::vector<T> entries;
  entries.reserve(set.size());
  for (auto it = set.begin(); it != set.end(); it = set.begin()) {
    auto node = set.extract(it);
    entries.emplace_back(std::move(node.value()));
  }
  absl::c_sort(entries);
  for (auto &t : entries) {
    f(std::move(t));
  }
}

} // namespace scip_clang

#endif // SCIP_CLANG_ABSL_EXTRAS_H
