  // extra-args: -std=c++20
  
  template<class T> struct remove_ref      { typedef T type; };
//                         ^^^^^^^^^^ definition [..] remove_ref#
  template<class T> struct remove_ref<T&>  { typedef T type; };
//                         ^^^^^^^^^^ definition [..] remove_ref#
  template<class T> struct remove_ref<T&&> { typedef T type; };
//                         ^^^^^^^^^^ definition [..] remove_ref#
  
  template <typename T>
  typename remove_ref<T>::type&& move(T&& arg) {
//                               ^^^^ definition [..] move(721d19cf58c53974).
//                                        ^^^ definition local 0
    return static_cast<typename remove_ref<T>::type&&>(arg);
//                                                     ^^^ reference local 0
  }
  
  struct C {
//       ^ definition [..] C#
    int x;
//      ^ definition [..] C#x.
    int y;
//      ^ definition [..] C#y.
  };
  
  struct D {
//       ^ definition [..] D#
    int x;
//      ^ definition [..] D#x.
    int y;
//      ^ definition [..] D#y.
  
    D() = default;
//  ^ definition [..] D#D(ced63f7c635d850d).
    D(const D &) = default;
//  ^ definition [..] D#D(cf67c1cd7b9892d0).
//          ^ reference [..] D#
    D(D &&) = default;
//  ^ definition [..] D#D(ece7426db7e2c886).
//    ^ reference [..] D#
    D &operator=(const D &) = default;
//  ^ reference [..] D#
//     ^^^^^^^^ definition [..] D#`operator=`(37b1797afc85ed93).
//                     ^ reference [..] D#
    D &operator=(D &&) = default;
//  ^ reference [..] D#
//     ^^^^^^^^ definition [..] D#`operator=`(1c0a0df55fbfcacb).
//               ^ reference [..] D#
  };
  
  void test_ctors() {
//     ^^^^^^^^^^ definition [..] test_ctors(49f6e7a06ebc5aa8).
    C c0;
//  ^ reference [..] C#
//    ^^ definition local 1
    D d0;
//  ^ reference [..] D#
//    ^^ definition local 2
    C c1{};
//  ^ reference [..] C#
//    ^^ definition local 3
    D d1{};
//  ^ reference [..] D#
//    ^^ definition local 4
    C c2{0, 1};
//  ^ reference [..] C#
//    ^^ definition local 5
    // TODO: Figure out a minimal stub for std::initializer_list,
    // which we can use here, without running into Clang's
    // "cannot compile this weird std::initializer_list yet" error
    // D d2{0, 1};
    C c3{move(c1)};
//  ^ reference [..] C#
//    ^^ definition local 6
//            ^^ reference local 3
    D d3{move(d1)};
//  ^ reference [..] D#
//    ^^ definition local 7
//            ^^ reference local 4
  
    C c4 = {};
//  ^ reference [..] C#
//    ^^ definition local 8
    D d4 = {};
//  ^ reference [..] D#
//    ^^ definition local 9
    C c5 = C();
//  ^ reference [..] C#
//    ^^ definition local 10
//         ^ reference [..] C#
    D d5 = D();
//  ^ reference [..] D#
//    ^^ definition local 11
//         ^ reference [..] D#
    C c6 = {0, 1};
//  ^ reference [..] C#
//    ^^ definition local 12
    // Uncomment after adding initializer_list
    // D d6 = {0, 1};
  
    C c7 = {.x = 0};
//  ^ reference [..] C#
//    ^^ definition local 13
    C c8 = {.x = 0, .y = 1};
//  ^ reference [..] C#
//    ^^ definition local 14
    C c9 = C{0, 1};
//  ^ reference [..] C#
//    ^^ definition local 15
//         ^ reference [..] C#
    C c10 = move(c1);
//  ^ reference [..] C#
//    ^^^ definition local 16
//               ^^ reference local 3
    D d10 = move(d1);
//  ^ reference [..] D#
//    ^^^ definition local 17
//               ^^ reference local 4
  }
