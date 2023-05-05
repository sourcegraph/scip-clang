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
