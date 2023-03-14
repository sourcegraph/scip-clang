// extra-args: -std=c++17

int MyGlobal = 3;

int f(int x_, int y_);

int f(int x, int y) {
  int z = x + y;
  int arr[2] = {x, y};
  auto [a, b] = arr;
  return z + a + b + MyGlobal;
}

struct S {
  int x;
  static int y;
};

int f(S s) {
  return s.x + S::y;
}
