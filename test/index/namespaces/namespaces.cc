// extra-args: -std=c++20

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
