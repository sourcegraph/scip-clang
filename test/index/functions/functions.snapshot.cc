  void top_level_func() {}
//^^^^ definition [..] `<file>/functions.cc`/
//kind File
//     ^^^^^^^^^^^^^^ definition [..] top_level_func(49f6e7a06ebc5aa8).
//     kind Function
  
  namespace my_namespace {
//          ^^^^^^^^^^^^ definition [..] my_namespace/
//          kind Namespace
    void func_in_namespace() {}
//       ^^^^^^^^^^^^^^^^^ definition [..] my_namespace/func_in_namespace(49f6e7a06ebc5aa8).
//       kind Function
  }
  
  void overloaded_func(int) {}
//     ^^^^^^^^^^^^^^^ definition [..] overloaded_func(d4f767463ce0a6b3).
//     kind Function
  void overloaded_func(const char *) {
//     ^^^^^^^^^^^^^^^ definition [..] overloaded_func(85c52e162fed56f9).
//     kind Function
    overloaded_func(32);
//  ^^^^^^^^^^^^^^^ reference [..] overloaded_func(d4f767463ce0a6b3).
  }
  
  void shadowed_func() {}
//     ^^^^^^^^^^^^^ definition [..] shadowed_func(49f6e7a06ebc5aa8).
//     kind Function
  
  namespace detail {
//          ^^^^^^ definition [..] detail/
//          kind Namespace
    void shadowed_func() {
//       ^^^^^^^^^^^^^ definition [..] detail/shadowed_func(49f6e7a06ebc5aa8).
//       kind Function
      shadowed_func();
//    ^^^^^^^^^^^^^ reference [..] detail/shadowed_func(49f6e7a06ebc5aa8).
    }
  }
  
  void use_outer() {
//     ^^^^^^^^^ definition [..] use_outer(49f6e7a06ebc5aa8).
//     kind Function
    shadowed_func();
//  ^^^^^^^^^^^^^ reference [..] shadowed_func(49f6e7a06ebc5aa8).
  }
  
  // check that the same canonical type produces the same hash
  using IntAlias = int;
//      ^^^^^^^^ definition [..] IntAlias#
//      kind TypeAlias
  void int_to_void_fn(int) {}
//     ^^^^^^^^^^^^^^ definition [..] int_to_void_fn(d4f767463ce0a6b3).
//     kind Function
  void same_hash_as_previous(IntAlias) {}
//     ^^^^^^^^^^^^^^^^^^^^^ definition [..] same_hash_as_previous(d4f767463ce0a6b3).
//     kind Function
//                           ^^^^^^^^ reference [..] IntAlias#
