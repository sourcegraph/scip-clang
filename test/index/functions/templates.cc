template <typename T>
struct T0 {
  void f0(T) {}

  template <typename U>
  void g0(U) {}
};

template <typename T>
struct T1: T0<T> {
  void f1(T t) {
    this->f0(t);
  }

  template <typename U>
  void g1(U u) {
    this->template g0<U>(u);
  }
};

template <typename H>
void h0(H) {}

template <typename H>
void h1(H h) { h0<H>(h); }

void test_template() {
  T0<int>().f0(0);
  T1<int>().f1(0);
  auto t1 = T1<int>();
  t1.f0(0);

  T0<int>().g0<int>(0);
  T1<int>().g1<unsigned>(0);
  auto t1_ = T1<int>();
  t1_.g0<char>(0);

  h0<int>(0);
  h0<void *>(0);
  h1<int>(0);
  h1<char>(0);
}
