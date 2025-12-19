// Test for member function templates with enable_if in non-template classes,
// and free function templates with enable_if. These patterns were previously
// broken because getFunctionDisambiguator didn't handle the case where
// getInstantiatedFromMemberTemplate() returns null for such templates.

namespace std {
template <bool B, class T = void>
struct enable_if {};

template <class T>
struct enable_if<true, T> {
  using type = T;
};

template <class T>
struct is_integral {
  static constexpr bool value = false;
};

template <>
struct is_integral<int> {
  static constexpr bool value = true;
};

template <class T>
struct is_enum {
  static constexpr bool value = false;
};
} // namespace std

// Issue 1: Member function template with enable_if in a non-template class
class Widget {
public:
  template <typename T,
            typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  void process(T value) {
    (void)value;
  }
};

// Issue 2: Free function template with enable_if
template <typename T,
          typename std::enable_if<!std::is_enum<T>::value, bool>::type = false>
T convert(int x) {
  return static_cast<T>(x);
}

// Issue 3: Member call through a pointer wrapper (simplified unique_ptr)
template <typename T>
class Ptr {
  T *p;

public:
  Ptr(T *ptr) : p(ptr) {}
  T *operator->() const { return p; }
};

class ThreadLoop {
public:
  bool isHealthy() const { return true; }
};

class ThreadPool {
  Ptr<ThreadLoop> mThread;

public:
  ThreadPool() : mThread(new ThreadLoop()) {}

  bool checkHealth() const {
    return mThread->isHealthy();
  }
};

void test() {
  Widget w;
  w.process(42);
  w.process<int>(100);

  auto val = convert<int>(5);
  (void)val;

  ThreadPool pool;
  (void)pool.checkHealth();
}
