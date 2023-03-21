struct S0 {
  int x = a;
  static constexpr int a = 0;
  static int y;

  S0(): x(a) {
    x += y;
  }
};

int S0::y = 3;

struct S1: S0 {
  S1(): S0() {
    x = y;
  }
};

struct S2 {
  struct { int a; };
  union u { float x; int y; };
  int : 4;
  int b: 3;

  S2(): b(1) {
    a = 10; // Indirect field access
  }
};

