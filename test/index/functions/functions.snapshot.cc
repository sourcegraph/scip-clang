  void top_level_func() {}
//^^^^ definition [..] `<file>/functions.cc`/
//     ^^^^^^^^^^^^^^ definition [..] top_level_func(49f6e7a06ebc5aa8).
  
  namespace my_namespace {
//          ^^^^^^^^^^^^ definition [..] my_namespace/
    void func_in_namespace() {}
//       ^^^^^^^^^^^^^^^^^ definition [..] my_namespace/func_in_namespace(49f6e7a06ebc5aa8).
  }
  
  void overloaded_func(int) {}
//     ^^^^^^^^^^^^^^^ definition [..] overloaded_func(d4f767463ce0a6b3).
  void overloaded_func(const char *) {
//     ^^^^^^^^^^^^^^^ definition [..] overloaded_func(85c52e162fed56f9).
    overloaded_func(32);
//  ^^^^^^^^^^^^^^^ reference [..] overloaded_func(d4f767463ce0a6b3).
  }
  
  void shadowed_func() {}
//     ^^^^^^^^^^^^^ definition [..] shadowed_func(49f6e7a06ebc5aa8).
  
  namespace detail {
//          ^^^^^^ definition [..] detail/
    void shadowed_func() {
//       ^^^^^^^^^^^^^ definition [..] detail/shadowed_func(49f6e7a06ebc5aa8).
      shadowed_func();
//    ^^^^^^^^^^^^^ reference [..] detail/shadowed_func(49f6e7a06ebc5aa8).
    }
  }
  
  void use_outer() {
//     ^^^^^^^^^ definition [..] use_outer(49f6e7a06ebc5aa8).
    shadowed_func();
//  ^^^^^^^^^^^^^ reference [..] shadowed_func(49f6e7a06ebc5aa8).
  }
  
  // check that the same canonical type produces the same hash
  using IntAlias = int;
//      ^^^^^^^^ definition [..] IntAlias#
  void int_to_void_fn(int) {}
//     ^^^^^^^^^^^^^^ definition [..] int_to_void_fn(d4f767463ce0a6b3).
  void same_hash_as_previous(IntAlias) {}
//     ^^^^^^^^^^^^^^^^^^^^^ definition [..] same_hash_as_previous(d4f767463ce0a6b3).
//                           ^^^^^^^^ reference [..] IntAlias#
