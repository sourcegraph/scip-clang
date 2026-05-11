  // extra-args: -std=c++20
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/ctors_dtors.cc`/
//kind File
  
  template<class T> struct remove_ref      { typedef T type; };
//               ^ definition local 0
//               kind TypeParameter
//                         ^^^^^^^^^^ definition [..] remove_ref#
//                         kind Struct
//                                                   ^ reference local 0
//                                                     ^^^^ definition [..] remove_ref#type#
//                                                     kind TypeAlias
  template<class T> struct remove_ref<T&>  { typedef T type; };
//               ^ definition local 1
//               kind TypeParameter
//                         ^^^^^^^^^^ definition [..] remove_ref#
//                         kind Struct
//                                    ^ reference local 1
//                                                   ^ reference local 1
//                                                     ^^^^ definition [..] remove_ref#type#
//                                                     kind TypeAlias
  template<class T> struct remove_ref<T&&> { typedef T type; };
//               ^ definition local 2
//               kind TypeParameter
//                         ^^^^^^^^^^ definition [..] remove_ref#
//                         kind Struct
//                                    ^ reference local 2
//                                                   ^ reference local 2
//                                                     ^^^^ definition [..] remove_ref#type#
//                                                     kind TypeAlias
  
  template <typename T>
//                   ^ definition local 3
//                   kind TypeParameter
  typename remove_ref<T>::type&& move(T&& arg) {
//         ^^^^^^^^^^ reference [..] remove_ref#
//                               ^^^^ definition [..] move(14e6c83231ab878e).
//                               kind Function
//                                    ^ reference local 3
//                                        ^^^ definition local 4
//                                        kind Parameter
    return static_cast<typename remove_ref<T>::type&&>(arg);
//                              ^^^^^^^^^^ reference [..] remove_ref#
//                                                     ^^^ reference local 4
  }
  
  struct C {
//       ^ definition [..] C#
//       kind Struct
    int x;
//      ^ definition [..] C#x.
//      kind Field
    int y;
//      ^ definition [..] C#y.
//      kind Field
  };
  
  struct D {
//       ^ definition [..] D#
//       kind Struct
    int x;
//      ^ definition [..] D#x.
//      kind Field
    int y;
//      ^ definition [..] D#y.
//      kind Field
  
    D() = default;
//  ^ definition [..] D#D(ced63f7c635d850d).
//  kind Constructor
    D(const D &) = default;
//  ^ definition [..] D#D(cf67c1cd7b9892d0).
//  kind Constructor
//          ^ reference [..] D#
    D(D &&) = default;
//  ^ definition [..] D#D(ece7426db7e2c886).
//  kind Constructor
//    ^ reference [..] D#
    D &operator=(const D &) = default;
//  ^ reference [..] D#
//     ^^^^^^^^ definition [..] D#`operator=`(37b1797afc85ed93).
//     kind Method
//                     ^ reference [..] D#
    D &operator=(D &&) = default;
//  ^ reference [..] D#
//     ^^^^^^^^ definition [..] D#`operator=`(1c0a0df55fbfcacb).
//     kind Method
//               ^ reference [..] D#
  };
  
  void test_ctors() {
//     ^^^^^^^^^^ definition [..] test_ctors(49f6e7a06ebc5aa8).
//     kind Function
    C c0;
//  ^ reference [..] C#
//    ^^ definition local 5
//    kind Variable
    D d0;
//  ^ reference [..] D#
//    ^^ definition local 6
//    kind Variable
//    ^^ reference [..] D#D(ced63f7c635d850d).
    C c1{};
//  ^ reference [..] C#
//    ^^ definition local 7
//    kind Variable
    D d1{};
//  ^ reference [..] D#
//    ^^ definition local 8
//    kind Variable
//    ^^ reference [..] D#D(ced63f7c635d850d).
    C c2{0, 1};
//  ^ reference [..] C#
//    ^^ definition local 9
//    kind Variable
    // TODO: Figure out a minimal stub for std::initializer_list,
    // which we can use here, without running into Clang's
    // "cannot compile this weird std::initializer_list yet" error
    // D d2{0, 1};
    C c3{move(c1)};
//  ^ reference [..] C#
//    ^^ definition local 10
//    kind Variable
//       ^^^^ reference [..] move(14e6c83231ab878e).
//            ^^ reference local 7
    D d3{move(d1)};
//  ^ reference [..] D#
//    ^^ definition local 11
//    kind Variable
//    ^^ reference [..] D#D(ece7426db7e2c886).
//       ^^^^ reference [..] move(14e6c83231ab878e).
//            ^^ reference local 8
  
    C c4 = {};
//  ^ reference [..] C#
//    ^^ definition local 12
//    kind Variable
    D d4 = {};
//  ^ reference [..] D#
//    ^^ definition local 13
//    kind Variable
//         ^ reference [..] D#D(ced63f7c635d850d).
    C c5 = C();
//  ^ reference [..] C#
//    ^^ definition local 14
//    kind Variable
//         ^ reference [..] C#
    D d5 = D();
//  ^ reference [..] D#
//    ^^ definition local 15
//    kind Variable
//         ^ reference [..] D#
//         ^ reference [..] D#D(ced63f7c635d850d).
    C c6 = {0, 1};
//  ^ reference [..] C#
//    ^^ definition local 16
//    kind Variable
    // Uncomment after adding initializer_list
    // D d6 = {0, 1};
  
    C c7 = {.x = 0};
//  ^ reference [..] C#
//    ^^ definition local 17
//    kind Variable
    C c8 = {.x = 0, .y = 1};
//  ^ reference [..] C#
//    ^^ definition local 18
//    kind Variable
    C c9 = C{0, 1};
//  ^ reference [..] C#
//    ^^ definition local 19
//    kind Variable
//         ^ reference [..] C#
    C c10 = move(c1);
//  ^ reference [..] C#
//    ^^^ definition local 20
//    kind Variable
//          ^^^^ reference [..] move(14e6c83231ab878e).
//               ^^ reference local 7
    D d10 = move(d1);
//  ^ reference [..] D#
//    ^^^ definition local 21
//    kind Variable
//          ^^^^ reference [..] D#D(ece7426db7e2c886).
//          ^^^^ reference [..] move(14e6c83231ab878e).
//               ^^ reference local 8
  }
