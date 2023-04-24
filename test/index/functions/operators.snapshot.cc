  // extra-args: -std=c++2b
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/operators.cc`/
  
  // Overloaded operators
  
  struct MyStream {};
//       ^^^^^^^^ definition [..] MyStream#
  
  MyStream &operator<<(MyStream &s, int) { return s; }
//^^^^^^^^ reference [..] MyStream#
//          ^^^^^^^^ definition [..] `operator<<`(97f6638197cf8bc4).
//                     ^^^^^^^^ reference [..] MyStream#
//                               ^ definition local 0
//                                                ^ reference local 0
  MyStream &operator<<(MyStream &s, const char *) { return s; }
//^^^^^^^^ reference [..] MyStream#
//          ^^^^^^^^ definition [..] `operator<<`(5651711ec4adbebf).
//                     ^^^^^^^^ reference [..] MyStream#
//                               ^ definition local 1
//                                                         ^ reference local 1
  
  struct Int { int val; };
//       ^^^ definition [..] Int#
//                 ^^^ definition [..] Int#val.
  struct IntPair { Int x; Int y; };
//       ^^^^^^^ definition [..] IntPair#
//                 ^^^ reference [..] Int#
//                     ^ definition [..] IntPair#x.
//                        ^^^ reference [..] Int#
//                            ^ definition [..] IntPair#y.
  
  IntPair operator,(Int x, Int y) {
//^^^^^^^ reference [..] IntPair#
//        ^^^^^^^^ definition [..] `operator,`(56a2b3932346e301).
//                  ^^^ reference [..] Int#
//                      ^ definition local 2
//                         ^^^ reference [..] Int#
//                             ^ definition local 3
    return IntPair{x, y};
//         ^^^^^^^ reference [..] IntPair#
//                 ^ reference local 2
//                    ^ reference local 3
  }
  
  Int operator++(Int &i, int) {
//^^^ reference [..] Int#
//    ^^^^^^^^ definition [..] operator++(be31e3af2b2ba0e).
//               ^^^ reference [..] Int#
//                    ^ definition local 4
    return Int{i.val+1};
//         ^^^ reference [..] Int#
//             ^ reference local 4
//               ^^^ reference [..] Int#val.
  }
  
  struct FnLike {
//       ^^^^^^ definition [..] FnLike#
    int operator()() { return 0; }
//      ^^^^^^^^ definition [..] FnLike#`operator()`(b126dc7c1de90089).
  };
  
  struct Table {
//       ^^^^^ definition [..] Table#
    int value;
//      ^^^^^ definition [..] Table#value.
    int &operator[](int i, int j) { // since C++23
//       ^^^^^^^^ definition [..] Table#`operator[]`(77cf9b7ed2f5124c).
//                      ^ definition local 5
//                             ^ definition local 6
      return this->value;
//                 ^^^^^ reference [..] Table#value.
    }
  };
  
  struct TablePtr {
//       ^^^^^^^^ definition [..] TablePtr#
    Table *t;
//  ^^^^^ reference [..] Table#
//         ^ definition [..] TablePtr#t.
    Table *operator->() { return t; }
//  ^^^^^ reference [..] Table#
//         ^^^^^^^^ definition [..] TablePtr#`operator->`(ed921902444779f1).
//                               ^ reference [..] TablePtr#t.
  };
  
  void test_overloaded_operators() {
//     ^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] test_overloaded_operators(49f6e7a06ebc5aa8).
    MyStream s{};
//  ^^^^^^^^ reference [..] MyStream#
//           ^ definition local 7
    s << 0 << "nothing to see here";
//  ^ reference local 7
//    ^^ reference [..] `operator<<`(97f6638197cf8bc4).
//         ^^ reference [..] `operator<<`(5651711ec4adbebf).
    Int x{0};
//  ^^^ reference [..] Int#
//      ^ definition local 8
    Int y{0};
//  ^^^ reference [..] Int#
//      ^ definition local 9
    IntPair p = (x, y);
//  ^^^^^^^ reference [..] IntPair#
//          ^ definition local 10
//               ^ reference local 8
//                ^ reference [..] `operator,`(56a2b3932346e301).
//                  ^ reference local 9
    p.x++;
//  ^ reference local 10
//    ^ reference [..] IntPair#x.
//     ^^ reference [..] operator++(be31e3af2b2ba0e).
  
    FnLike()();
//  ^^^^^^ reference [..] FnLike#
//          ^ reference [..] FnLike#`operator()`(b126dc7c1de90089).
    Table{}[0, 1];
//  ^^^^^ reference [..] Table#
//         ^ reference [..] Table#`operator[]`(77cf9b7ed2f5124c).
    TablePtr{}->value;
//  ^^^^^^^^ reference [..] TablePtr#
//            ^^ reference [..] TablePtr#`operator->`(ed921902444779f1).
//              ^^^^^ reference [..] Table#value.
  }
  
  // User-defined conversion function
  
  // Based on https://en.cppreference.com/w/cpp/language/cast_operator
  struct IntConvertible {
//       ^^^^^^^^^^^^^^ definition [..] IntConvertible#
    operator int() const { return 0; }
//  ^^^^^^^^ definition [..] IntConvertible#`operator int`(455f465bc33b4cdf).
    explicit operator const char*() const { return "aaa"; }
//           ^^^^^^^^ definition [..] IntConvertible#`operator const char *`(a9f41ea0e82d88cf).
    using arr_t = int[3];
//        ^^^^^ definition [..] IntConvertible#arr_t#
    operator arr_t*() const { return nullptr; }
//  ^^^^^^^^ definition [..] IntConvertible#`operator int (*)[3]`(a00bb5473f10e296).
//           ^^^^^ reference [..] IntConvertible#arr_t#
//           ^^^^^ reference [..] IntConvertible#arr_t#
  };
  
  void test_implicit_conversion() {
//     ^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] test_implicit_conversion(49f6e7a06ebc5aa8).
    IntConvertible x;
//  ^^^^^^^^^^^^^^ reference [..] IntConvertible#
//                 ^ definition local 11
    (void)static_cast<int>(x);
//                         ^ reference local 11
    int m = x;
//      ^ definition local 12
//          ^ reference local 11
    (void)static_cast<const char *>(x);
//                                  ^ reference local 11
    int (*pa)[3] = x;
//        ^^ definition local 13
//                 ^ reference local 11
  }
  
  // Allocation and deallocation
  
  #if _WIN32
  using size_t = unsigned long long;
  #else
  using size_t = unsigned long;
//      ^^^^^^ definition [..] size_t#
  #endif
  
  // Override global stuff
  void *operator new(size_t) { return nullptr; }
//      ^^^^^^^^ definition [..] `operator new`(bfcfa4d6b7f7ef64).
//                   ^^^^^^ reference [..] size_t#
  void *operator new[](size_t) { return nullptr; }
//      ^^^^^^^^ definition [..] `operator new[]`(bfcfa4d6b7f7ef64).
//                     ^^^^^^ reference [..] size_t#
  void operator delete(void *) noexcept {}
//     ^^^^^^^^ definition [..] `operator delete`(bd21765a0afc8e3c).
  void operator delete[](void *) noexcept {}
//     ^^^^^^^^ definition [..] `operator delete[]`(bd21765a0afc8e3c).
  
  struct Arena {};
//       ^^^^^ definition [..] Arena#
  
  struct InArena {
//       ^^^^^^^ definition [..] InArena#
    static void *operator new(size_t count, Arena &) {
//               ^^^^^^^^ definition [..] InArena#`operator new`(747707f21471d499).
//                            ^^^^^^ reference [..] size_t#
//                                   ^^^^^ definition local 14
//                                          ^^^^^ reference [..] Arena#
      return ::operator new(count);
//             ^^^^^^^^ reference [..] `operator new`(bfcfa4d6b7f7ef64).
//                          ^^^^^ reference local 14
    }
    static void *operator new[](size_t count, Arena &) {
//               ^^^^^^^^ definition [..] InArena#`operator new[]`(747707f21471d499).
//                              ^^^^^^ reference [..] size_t#
//                                     ^^^^^ definition local 15
//                                            ^^^^^ reference [..] Arena#
      return ::operator new[](count);
//             ^^^^^^^^ reference [..] `operator new[]`(bfcfa4d6b7f7ef64).
//                            ^^^^^ reference local 15
    }
    static void operator delete(void *p) {
//              ^^^^^^^^ definition [..] InArena#`operator delete`(bd21765a0afc8e3c).
//                                    ^ definition local 16
      return ::operator delete(p);
//             ^^^^^^^^ reference [..] `operator delete`(bd21765a0afc8e3c).
//                             ^ reference local 16
    }
    static void operator delete[](void *p) {
//              ^^^^^^^^ definition [..] InArena#`operator delete[]`(bd21765a0afc8e3c).
//                                      ^ definition local 17
      return ::operator delete[](p);
//             ^^^^^^^^ reference [..] `operator delete[]`(bd21765a0afc8e3c).
//                               ^ reference local 17
    }
    // These two delete operations are only implicitly called
    // if the corresponding operator new has an exception.
    static void operator delete(void *p, Arena &) {
//              ^^^^^^^^ definition [..] InArena#`operator delete`(71e17451144c5c5c).
//                                    ^ definition local 18
//                                       ^^^^^ reference [..] Arena#
      return ::operator delete(p);
//             ^^^^^^^^ reference [..] `operator delete`(bd21765a0afc8e3c).
//                             ^ reference local 18
    }
    static void operator delete[](void *p, Arena &) {
//              ^^^^^^^^ definition [..] InArena#`operator delete[]`(71e17451144c5c5c).
//                                      ^ definition local 19
//                                         ^^^^^ reference [..] Arena#
      return ::operator delete[](p);
//             ^^^^^^^^ reference [..] `operator delete[]`(bd21765a0afc8e3c).
//                               ^ reference local 19
    }
  };
  
  void test_new_delete() {
//     ^^^^^^^^^^^^^^^ definition [..] test_new_delete(49f6e7a06ebc5aa8).
    int *x = new int;
//       ^ definition local 20
    delete x;
//         ^ reference local 20
    int *xs = new int[4];
//       ^^ definition local 21
    delete[] xs;
//           ^^ reference local 21
  
    Arena a{};
//  ^^^^^ reference [..] Arena#
//        ^ definition local 22
    auto *p1 = new (a) InArena;
//        ^^ definition local 23
//                  ^ reference local 22
//                     ^^^^^^^ reference [..] InArena#
    delete p1;
//         ^^ reference local 23
    auto *p2 = new (a) InArena[3];
//        ^^ definition local 24
//                  ^ reference local 22
//                     ^^^^^^^ reference [..] InArena#
    delete[] p2;
//           ^^ reference local 24
  }
  
  // User-defined literals
  
  void operator ""_ull_lit(unsigned long long) { return; }
//     ^^^^^^^^ definition [..] `operator""_ull_lit`(891dc3055356b409).
  void operator ""_raw_lit(const char *) { return; }
//     ^^^^^^^^ definition [..] `operator""_raw_lit`(85c52e162fed56f9).
  
  template <char...>
  void operator""_templated_lit() {}
//     ^^^^^^^^ definition [..] `operator""_templated_lit`(49f6e7a06ebc5aa8).
  
  struct A { constexpr A(const char *) {} };
//       ^ definition [..] A#
//                     ^ definition [..] A#A(85c52e162fed56f9).
  
  template <A a> // since C++20
//          ^ reference [..] A#
//            ^ definition local 25
  void operator ""_a_op() { return; }
//     ^^^^^^^^ definition [..] `operator""_a_op`(49f6e7a06ebc5aa8).
  
  void test_literals() {
//     ^^^^^^^^^^^^^ definition [..] test_literals(49f6e7a06ebc5aa8).
    123_ull_lit;
//     ^^^^^^^^ reference [..] `operator""_ull_lit`(891dc3055356b409).
    123_raw_lit;
//     ^^^^^^^^ reference [..] `operator""_raw_lit`(85c52e162fed56f9).
    123_templated_lit;
//     ^^^^^^^^^^^^^^ reference [..] `operator""_templated_lit`(49f6e7a06ebc5aa8).
    "123"_a_op; // invokes operator""_a_op<A("123")>
//       ^^^^^ reference [..] `operator""_a_op`(49f6e7a06ebc5aa8).
  }
  
  // Overloaded co_await
  // FIXME(issue: https://github.com/sourcegraph/scip-clang/issues/125)
