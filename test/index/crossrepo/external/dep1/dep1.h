namespace dep1 {
  /// My function f
  void f();

  /// My templated struct
  template <typename T>
  struct S {
    T t;

    template <typename U>
    U identity(U u) const { return u; }
  };

  /// My class
  class C;

  C *newC();
  void deleteC(C *);
}
