template <typename T>
struct S {};

typedef Nonexistent<int> Sint;

struct R {
  typedef S<Sint> Ssint;
};
