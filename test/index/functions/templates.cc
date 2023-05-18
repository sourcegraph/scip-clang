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

template <typename T>
struct Q0 {
  void f() {}
};

template <typename T>
struct Q1: Q0<T> {
  using Base1 = Q0<T>;
  using Base1::f;
  void g() { f(); }
};

template <typename T>
struct Q2: Q1<T> {
  using Base2 = Q1<T>;
  using Base2::f;
  void h() { f(); }
};

template <typename T>
struct FwdDecl1;

template <typename T>
struct FwdDecl2;

template <typename X>
void f(FwdDecl1<X> &a1, FwdDecl2<X> &a2) {
  a1.whatever(); // No code nav, sorry
  a2.whatever();
}

template <typename T>
struct FwdDecl2 {
  void whatever() {}
};