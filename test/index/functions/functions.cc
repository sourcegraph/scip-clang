void top_level_func() {}

namespace my_namespace {
  void func_in_namespace() {}
}

void overloaded_func(int) {}
void overloaded_func(const char *) {
  overloaded_func(32);
}

void shadowed_func() {}

namespace detail {
  void shadowed_func() {
    shadowed_func();
  }
}

void use_outer() {
  shadowed_func();
}

// check that the same canonical type produces the same hash
using IntAlias = int;
void int_to_void_fn(int) {}
void same_hash_as_previous(IntAlias) {}
