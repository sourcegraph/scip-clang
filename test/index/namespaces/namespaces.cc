// extra-args: -std=c++20

#include "system_header.h"

namespace a {
}

// nested namespace definition allowed since C++17
namespace a::b {
}

namespace {
}

inline namespace xx {
}

namespace z {

inline namespace {
}

}

// inline nested namespace definition allowed since C++20
namespace z::inline y {
}

namespace c {
class C {
};
}

using C = c::C;

#define EXPAND_TO_NAMESPACE \
  namespace from_macro {}

EXPAND_TO_NAMESPACE

#define EXPAND_TO_NAMESPACE_2 EXPAND_TO_NAMESPACE

EXPAND_TO_NAMESPACE_2

#define IDENTITY(x) x

IDENTITY(namespace in_macro { })

namespace a {
  namespace c {
    enum E { E0 };
  }
  namespace c_alias = c;
}

void f(a::c_alias::E) {
  (void)a::c::E::E0;
  (void)a::c_alias::E::E0;
}
