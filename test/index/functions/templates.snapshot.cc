  template <typename T>
//^^^^^^^^ definition [..] `<file>/templates.cc`/
//                   ^ definition local 0
  struct T0 {
//       ^^ definition [..] T0#
    void f0(T) {}
//       ^^ definition [..] T0#f0(9b289cee16747614).
//          ^ reference local 0
  
    template <typename U>
//                     ^ definition local 1
    void g0(U) {}
//       ^^ definition [..] T0#g0(b07662a27bd562f9).
//          ^ reference local 1
  };
  
  template <typename T>
//                   ^ definition local 2
  struct T1: T0<T> {
//       ^^ definition [..] T1#
//           ^^ reference [..] T0#
//              ^ reference local 2
    void f1(T t) {
//       ^^ definition [..] T1#f1(9b289cee16747614).
//          ^ reference local 2
//            ^ definition local 3
      this->f0(t);
//             ^ reference local 3
    }
  
    template <typename U>
//                     ^ definition local 4
    void g1(U u) {
//       ^^ definition [..] T1#g1(b07662a27bd562f9).
//          ^ reference local 4
//            ^ definition local 5
      this->template g0<U>(u);
//                      ^ reference local 4
//                         ^ reference local 5
    }
  };
  
  template <typename H>
//                   ^ definition local 6
  void h0(H) {}
//     ^^ definition [..] h0(9b289cee16747614).
//        ^ reference local 6
  
  template <typename H>
//                   ^ definition local 7
  void h1(H h) { h0<H>(h); }
//     ^^ definition [..] h1(9b289cee16747614).
//        ^ reference local 7
//          ^ definition local 8
//                  ^ reference local 7
//                     ^ reference local 8
  
  void test_template() {
//     ^^^^^^^^^^^^^ definition [..] test_template(49f6e7a06ebc5aa8).
    T0<int>().f0(0);
//  ^^ reference [..] T0#
//            ^^ reference [..] T0#f0(d4f767463ce0a6b3).
    T1<int>().f1(0);
//  ^^ reference [..] T1#
//            ^^ reference [..] T1#f1(d4f767463ce0a6b3).
    auto t1 = T1<int>();
//       ^^ definition local 9
//            ^^ reference [..] T1#
    t1.f0(0);
//  ^^ reference local 9
//     ^^ reference [..] T0#f0(d4f767463ce0a6b3).
  
    T0<int>().g0<int>(0);
//  ^^ reference [..] T0#
//            ^^ reference [..] T0#g0(d4f767463ce0a6b3).
    T1<int>().g1<unsigned>(0);
//  ^^ reference [..] T1#
//            ^^ reference [..] T1#g1(cb2f890c2fdad230).
    auto t1_ = T1<int>();
//       ^^^ definition local 10
//             ^^ reference [..] T1#
    t1_.g0<char>(0);
//  ^^^ reference local 10
//      ^^ reference [..] T0#g0(44b6ea1973a07080).
  
    h0<int>(0);
    h0<void *>(0);
    h1<int>(0);
    h1<char>(0);
  }
