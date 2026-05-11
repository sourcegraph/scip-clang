  template <typename T>
//^^^^^^^^ definition [..] `<file>/templates.cc`/
//kind File
//                   ^ definition local 0
//                   kind TypeParameter
  struct T0 {
//       ^^ definition [..] T0#
//       kind Struct
    void f0(T) {}
//       ^^ definition [..] T0#f0(9b289cee16747614).
//       kind Method
//          ^ reference local 0
  
    template <typename U>
//                     ^ definition local 1
//                     kind TypeParameter
    void g0(U) {}
//       ^^ definition [..] T0#g0(b07662a27bd562f9).
//       kind Method
//          ^ reference local 1
  };
  
  template <typename T>
//                   ^ definition local 2
//                   kind TypeParameter
  struct T1: T0<T> {
//       ^^ definition [..] T1#
//       kind Struct
//       relation implementation [..] T0#
//           ^^ reference [..] T0#
//              ^ reference local 2
    void f1(T t) {
//       ^^ definition [..] T1#f1(9b289cee16747614).
//       kind Method
//          ^ reference local 2
//            ^ definition local 3
//            kind Parameter
      this->f0(t);
//          ^^ reference [..] T0#f0(9b289cee16747614).
//             ^ reference local 3
    }
  
    template <typename U>
//                     ^ definition local 4
//                     kind TypeParameter
    void g1(U u) {
//       ^^ definition [..] T1#g1(b07662a27bd562f9).
//       kind Method
//          ^ reference local 4
//            ^ definition local 5
//            kind Parameter
      this->template g0<U>(u);
//                   ^^ reference [..] T0#g0(b07662a27bd562f9).
//                      ^ reference local 4
//                         ^ reference local 5
    }
  };
  
  template <typename H>
//                   ^ definition local 6
//                   kind TypeParameter
  void h0(H) {}
//     ^^ definition [..] h0(9b289cee16747614).
//     kind Function
//        ^ reference local 6
  
  template <typename H>
//                   ^ definition local 7
//                   kind TypeParameter
  void h1(H h) { h0<H>(h); }
//     ^^ definition [..] h1(9b289cee16747614).
//     kind Function
//        ^ reference local 7
//          ^ definition local 8
//          kind Parameter
//               ^^ reference [..] h0(9b289cee16747614).
//                  ^ reference local 7
//                     ^ reference local 8
  
  void test_template() {
//     ^^^^^^^^^^^^^ definition [..] test_template(49f6e7a06ebc5aa8).
//     kind Function
    T0<int>().f0(0);
//  ^^ reference [..] T0#
//            ^^ reference [..] T0#f0(9b289cee16747614).
    T1<int>().f1(0);
//  ^^ reference [..] T1#
//            ^^ reference [..] T1#f1(9b289cee16747614).
    auto t1 = T1<int>();
//       ^^ definition local 9
//       kind Variable
//            ^^ reference [..] T1#
    t1.f0(0);
//  ^^ reference local 9
//     ^^ reference [..] T0#f0(9b289cee16747614).
  
    T0<int>().g0<int>(0);
//  ^^ reference [..] T0#
//            ^^ reference [..] T0#g0(b07662a27bd562f9).
    T1<int>().g1<unsigned>(0);
//  ^^ reference [..] T1#
//            ^^ reference [..] T1#g1(b07662a27bd562f9).
    auto t1_ = T1<int>();
//       ^^^ definition local 10
//       kind Variable
//             ^^ reference [..] T1#
    t1_.g0<char>(0);
//  ^^^ reference local 10
//      ^^ reference [..] T0#g0(b07662a27bd562f9).
  
    h0<int>(0);
//  ^^ reference [..] h0(9b289cee16747614).
    h0<void *>(0);
//  ^^ reference [..] h0(9b289cee16747614).
    h1<int>(0);
//  ^^ reference [..] h1(9b289cee16747614).
    h1<char>(0);
//  ^^ reference [..] h1(9b289cee16747614).
  }
  
  template <typename T>
//                   ^ definition local 11
//                   kind TypeParameter
  struct Q0 {
//       ^^ definition [..] Q0#
//       kind Struct
    void f() {}
//       ^ definition [..] Q0#f(49f6e7a06ebc5aa8).
//       kind Method
  };
  
  template <typename T>
//                   ^ definition local 12
//                   kind TypeParameter
  struct Q1: Q0<T> {
//       ^^ definition [..] Q1#
//       kind Struct
//       relation implementation [..] Q0#
//           ^^ reference [..] Q0#
//              ^ reference local 12
    using Base1 = Q0<T>;
//        ^^^^^ definition [..] Q1#Base1#
//        kind TypeAlias
//                ^^ reference [..] Q0#
//                   ^ reference local 12
    using Base1::f;
//        ^^^^^ reference [..] Q1#Base1#
    void g() { f(); }
//       ^ definition [..] Q1#g(49f6e7a06ebc5aa8).
//       kind Method
//             ^ reference [..] Q0#f(49f6e7a06ebc5aa8).
  };
  
  template <typename T>
//                   ^ definition local 13
//                   kind TypeParameter
  struct Q2: Q1<T> {
//       ^^ definition [..] Q2#
//       kind Struct
//       relation implementation [..] Q0#
//       relation implementation [..] Q1#
//           ^^ reference [..] Q1#
//              ^ reference local 13
    using Base2 = Q1<T>;
//        ^^^^^ definition [..] Q2#Base2#
//        kind TypeAlias
//                ^^ reference [..] Q1#
//                   ^ reference local 13
    using Base2::f;
//        ^^^^^ reference [..] Q2#Base2#
    void h() { f(); }
//       ^ definition [..] Q2#h(49f6e7a06ebc5aa8).
//       kind Method
//             ^ reference [..] Q0#f(49f6e7a06ebc5aa8).
  };
  
  template <typename T>
//                   ^ definition local 14
//                   kind TypeParameter
  struct FwdDecl1;
//       ^^^^^^^^ reference [..] FwdDecl1#
  
  template <typename T>
//                   ^ definition local 15
//                   kind TypeParameter
  struct FwdDecl2;
//       ^^^^^^^^ reference [..] FwdDecl2#
  
  template <typename X>
//                   ^ definition local 16
//                   kind TypeParameter
  void f(FwdDecl1<X> &a1, FwdDecl2<X> &a2) {
//     ^ definition [..] f(4764f947061d9ce0).
//     kind Function
//       ^^^^^^^^ reference [..] FwdDecl1#
//                ^ reference local 16
//                    ^^ definition local 17
//                    kind Parameter
//                        ^^^^^^^^ reference [..] FwdDecl2#
//                                 ^ reference local 16
//                                     ^^ definition local 18
//                                     kind Parameter
    a1.whatever(); // No code nav, sorry
//  ^^ reference local 17
    a2.whatever();
//  ^^ reference local 18
//     ^^^^^^^^ reference [..] FwdDecl2#whatever(49f6e7a06ebc5aa8).
  }
  
  template <typename T>
//                   ^ definition local 19
//                   kind TypeParameter
  struct FwdDecl2 {
//       ^^^^^^^^ definition [..] FwdDecl2#
//       kind Struct
    void whatever() {}
//       ^^^^^^^^ definition [..] FwdDecl2#whatever(49f6e7a06ebc5aa8).
//       kind Method
  };
