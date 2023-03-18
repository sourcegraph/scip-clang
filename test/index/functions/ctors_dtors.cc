// extra-args: -std=c++20

template<class T> struct remove_ref      { typedef T type; };
template<class T> struct remove_ref<T&>  { typedef T type; };
template<class T> struct remove_ref<T&&> { typedef T type; };

template <typename T>
typename remove_ref<T>::type&& move(T&& arg) {
  return static_cast<typename remove_ref<T>::type&&>(arg);
}

struct C {
  int x;
  int y;
};

struct D {
  int x;
  int y;

  D() = default;
  D(const D &) = default;
  D(D &&) = default;
  D &operator=(const D &) = default;
  D &operator=(D &&) = default;
};

void test_ctors() {
  C c0;
  D d0;
  C c1{};
  D d1{};
  C c2{0, 1};
  // TODO: Figure out a minimal stub for std::initializer_list,
  // which we can use here, without running into Clang's
  // "cannot compile this weird std::initializer_list yet" error
  // D d2{0, 1};
  C c3{move(c1)};
  D d3{move(d1)};

  C c4 = {};
  D d4 = {};
  C c5 = C();
  D d5 = D();
  C c6 = {0, 1};
  // Uncomment after adding initializer_list
  // D d6 = {0, 1};

  C c7 = {.x = 0};
  C c8 = {.x = 0, .y = 1};
  C c9 = C{0, 1};
  C c10 = move(c1);
  D d10 = move(d1);
}
