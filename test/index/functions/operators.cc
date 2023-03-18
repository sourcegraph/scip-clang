// extra-args: -std=c++2b

// Overloaded operators

struct MyStream {};

MyStream &operator<<(MyStream &s, int) { return s; }
MyStream &operator<<(MyStream &s, const char *) { return s; }

struct Int { int val; };
struct IntPair { Int x; Int y; };

IntPair operator,(Int x, Int y) {
  return IntPair{x, y};
}

Int operator++(Int &i, int) {
  return Int{i.val+1};
}

struct FnLike {
  int operator()() { return 0; }
};

struct Table {
  int value;
  int &operator[](int i, int j) { // since C++23
    return this->value;
  }
};

struct TablePtr {
  Table *t;
  Table *operator->() { return t; }
};

void test_overloaded_operators() {
  MyStream s{};
  s << 0 << "nothing to see here";
  Int x{0};
  Int y{0};
  IntPair p = (x, y);
  p.x++;

  FnLike()();
  Table{}[0, 1];
  TablePtr{}->value;
}

// User-defined conversion function

// Based on https://en.cppreference.com/w/cpp/language/cast_operator
struct IntConvertible {
  operator int() const { return 0; }
  explicit operator const char*() const { return "aaa"; }
  using arr_t = int[3];
  operator arr_t*() const { return nullptr; }
};

void test_implicit_conversion() {
  IntConvertible x;
  (void)static_cast<int>(x);
  int m = x;
  (void)static_cast<const char *>(x);
  int (*pa)[3] = x;
}

// Allocation and deallocation

#if _WIN32
using size_t = unsigned long long;
#else
using size_t = unsigned long;
#endif

// Override global stuff
void *operator new(size_t) { return nullptr; }
void *operator new[](size_t) { return nullptr; }
void operator delete(void *) noexcept {}
void operator delete[](void *) noexcept {}

struct Arena {};

struct InArena {
  static void *operator new(size_t count, Arena &) {
    return ::operator new(count);
  }
  static void *operator new[](size_t count, Arena &) {
    return ::operator new[](count);
  }
  static void operator delete(void *p) {
    return ::operator delete(p);
  }
  static void operator delete[](void *p) {
    return ::operator delete[](p);
  }
  // These two delete operations are only implicitly called
  // if the corresponding operator new has an exception.
  static void operator delete(void *p, Arena &) {
    return ::operator delete(p);
  }
  static void operator delete[](void *p, Arena &) {
    return ::operator delete[](p);
  }
};

void test_new_delete() {
  int *x = new int;
  delete x;
  int *xs = new int[4];
  delete[] xs;

  Arena a{};
  auto *p1 = new (a) InArena;
  delete p1;
  auto *p2 = new (a) InArena[3];
  delete[] p2;
}

// User-defined literals

void operator ""_ull_lit(unsigned long long) { return; }
void operator ""_raw_lit(const char *) { return; }

template <char...>
void operator""_templated_lit() {}

struct A { constexpr A(const char *) {} };

template <A a> // since C++20
void operator ""_a_op() { return; }

void test_literals() {
  123_ull_lit;
  123_raw_lit;
  123_templated_lit;
  "123"_a_op; // invokes operator""_a_op<A("123")>
}

// Overloaded co_await
// FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/125)
