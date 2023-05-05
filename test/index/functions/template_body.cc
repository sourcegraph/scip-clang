template <typename T>
void f() {
  struct C {
    T field;

    void g() {};
  };

  auto t = C().field;
  C().g();

  int x = 0;
  (void)(2 * x);

  // The following are not allowed:
  // - Templated function-local classes
  // - Templates inside function-local classes
}
