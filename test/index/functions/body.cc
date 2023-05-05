void f() {
  struct C {
    int plain_field;

    void g() {};
  };

  (void)C().plain_field;
  C().g();

  int x = 0;
  (void)(2 * x);
}
