template <typename T>
void f() {
  struct C {
    int plain_field;
    T dependent_field;

    void g() {};
  };

  (void)C().plain_field;
  (void)C().dependent_field;
  C().g();

  int x = 0;
  (void)(2 * x);

  // The following are not allowed:
  // - Templated function-local classes
  // - Templates inside function-local classes
}

template <typename T>
struct Z {
  void f0() {}

  void f1() {
    f0();
  }

  template <typename U>
  void g0() {
    f0();
  }

  template <typename U>
  void g1() {
    g0<U>();
  }
};

template <typename T>
struct ZZ : Z<T> {
  void ff0() {
    this->f0();
  }

  template <typename U>
  void gg0() {
    this->f0();
    this->template g0<U>();
  }
};