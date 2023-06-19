  // Virtual methods and inheritance
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/methods.cc`/
  
  struct S0 {
//       ^^ definition [..] S0#
    virtual void v1() {
//               ^^ definition [..] S0#v1(49f6e7a06ebc5aa8).
      v2();
//    ^^ reference [..] S0#v2(49f6e7a06ebc5aa8).
    }
    virtual void v2() {}
//               ^^ definition [..] S0#v2(49f6e7a06ebc5aa8).
    virtual void v3() {}
//               ^^ definition [..] S0#v3(49f6e7a06ebc5aa8).
    virtual void v4() {}
//               ^^ definition [..] S0#v4(49f6e7a06ebc5aa8).
    virtual void v5() = 0;
//               ^^ definition [..] S0#v5(49f6e7a06ebc5aa8).
  
  };
  
  struct S1_0: S0 {
//       ^^^^ definition [..] S1_0#
//       relation implementation [..] S0#
//             ^^ reference [..] S0#
    virtual void v1() override { v2(); }
//               ^^ definition [..] S1_0#v1(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S0#v1(49f6e7a06ebc5aa8).
//                               ^^ reference [..] S1_0#v2(49f6e7a06ebc5aa8).
    virtual void v2() /*override*/ {}
//               ^^ definition [..] S1_0#v2(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S0#v2(49f6e7a06ebc5aa8).
  };
  
  struct S1_1: S0 {
//       ^^^^ definition [..] S1_1#
//       relation implementation [..] S0#
//             ^^ reference [..] S0#
    virtual void v2() override { v1(); }
//               ^^ definition [..] S1_1#v2(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S0#v2(49f6e7a06ebc5aa8).
//                               ^^ reference [..] S0#v1(49f6e7a06ebc5aa8).
    virtual void v3() override { v5(); }
//               ^^ definition [..] S1_1#v3(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S0#v3(49f6e7a06ebc5aa8).
//                               ^^ reference [..] S0#v5(49f6e7a06ebc5aa8).
  };
  
  struct S2 final: S1_0, S1_1 {
//       ^^ definition [..] S2#
//       relation implementation [..] S0#
//       relation implementation [..] S1_0#
//       relation implementation [..] S1_1#
//                 ^^^^ reference [..] S1_0#
//                       ^^^^ reference [..] S1_1#
    virtual void v1() override { v5(); }
//               ^^ definition [..] S2#v1(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S0#v1(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S1_0#v1(49f6e7a06ebc5aa8).
//                               ^^ reference [..] S2#v5(49f6e7a06ebc5aa8).
    virtual void v2() override {}
//               ^^ definition [..] S2#v2(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S1_0#v2(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S1_1#v2(49f6e7a06ebc5aa8).
    virtual void v3() override {}
//               ^^ definition [..] S2#v3(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S0#v3(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S1_1#v3(49f6e7a06ebc5aa8).
    virtual void v4() override {}
//               ^^ definition [..] S2#v4(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S0#v4(49f6e7a06ebc5aa8).
    virtual void v5() override {}
//               ^^ definition [..] S2#v5(49f6e7a06ebc5aa8).
//               relation implementation+reference [..] S0#v5(49f6e7a06ebc5aa8).
  };
  
  // Method forward declarations
  
  struct A0 {
//       ^^ definition [..] A0#
    void f();
//       ^ reference [..] A0#f(49f6e7a06ebc5aa8).
    static void g();
//              ^ reference [..] A0#g(49f6e7a06ebc5aa8).
    struct B0 {
//         ^^ definition [..] A0#B0#
      void f();
//         ^ reference [..] A0#B0#f(49f6e7a06ebc5aa8).
      static void g();
//                ^ reference [..] A0#B0#g(49f6e7a06ebc5aa8).
    };
    friend bool operator==(const A0 &, const A0 &);
//              ^^^^^^^^ reference [..] `operator==`(354ee8b82d643f9c).
//                               ^^ reference [..] A0#
//                                           ^^ reference [..] A0#
  };
  
  void A0::f() {}
//     ^^ reference [..] A0#
//         ^ definition [..] A0#f(49f6e7a06ebc5aa8).
  void A0::g() {}
//     ^^ reference [..] A0#
//         ^ definition [..] A0#g(49f6e7a06ebc5aa8).
  void A0::B0::f() {}
//     ^^ reference [..] A0#
//         ^^ reference [..] A0#B0#
//             ^ definition [..] A0#B0#f(49f6e7a06ebc5aa8).
  void A0::B0::g() {}
//     ^^ reference [..] A0#
//         ^^ reference [..] A0#B0#
//             ^ definition [..] A0#B0#g(49f6e7a06ebc5aa8).
  
  // Not A::operator==
  bool operator==(const A0 &, const A0 &) { return true; }
//     ^^^^^^^^ definition [..] `operator==`(354ee8b82d643f9c).
//                      ^^ reference [..] A0#
//                                  ^^ reference [..] A0#
  
  // Static methods
  
  struct Z0 {
//       ^^ definition [..] Z0#
    static void f();
//              ^ reference [..] Z0#f(49f6e7a06ebc5aa8).
    void g() { f(); }
//       ^ definition [..] Z0#g(49f6e7a06ebc5aa8).
//             ^ reference [..] Z0#f(49f6e7a06ebc5aa8).
  };
  
  struct Z1: Z0 {
//       ^^ definition [..] Z1#
//       relation implementation [..] Z0#
//           ^^ reference [..] Z0#
    void h() {
//       ^ definition [..] Z1#h(49f6e7a06ebc5aa8).
      f();
//    ^ reference [..] Z0#f(49f6e7a06ebc5aa8).
      Z1::f();
//    ^^ reference [..] Z1#
//        ^ reference [..] Z0#f(49f6e7a06ebc5aa8).
      Z0::f();
//    ^^ reference [..] Z0#
//        ^ reference [..] Z0#f(49f6e7a06ebc5aa8).
    }
  };
  
  // Member function pointer
  
  struct M0 {
//       ^^ definition [..] M0#
    void f() {}
//       ^ definition [..] M0#f(49f6e7a06ebc5aa8).
  };
  
  void test_member_pointer() {
//     ^^^^^^^^^^^^^^^^^^^ definition [..] test_member_pointer(49f6e7a06ebc5aa8).
    void (M0::*p)() = &M0::f;
//        ^^ reference [..] M0#
//             ^ definition local 0
//                     ^^ reference [..] M0#
//                         ^ reference [..] M0#f(49f6e7a06ebc5aa8).
    M0 m{};
//  ^^ reference [..] M0#
//     ^ definition local 1
    (m.*p)();
//   ^ reference local 1
//      ^ reference local 0
  }
  
  // Using-declarations making methods public:
  
  namespace u {
//          ^ definition [..] u/
    struct Z0 {
//         ^^ definition [..] u/Z0#
    protected:
      void f(int) {}
//         ^ definition [..] u/Z0#f(d4f767463ce0a6b3).
      void f(int, int) {}
//         ^ definition [..] u/Z0#f(3e454b8ccf442868).
    };
  
    struct Z1: Z0 {
//         ^^ definition [..] u/Z1#
//         relation implementation [..] u/Z0#
//             ^^ reference [..] u/Z0#
      using Z0::f;
//          ^^ reference [..] u/Z0#
//              ^ reference [..] u/Z0#f(3e454b8ccf442868).
//              ^ reference [..] u/Z0#f(d4f767463ce0a6b3).
//              ^ definition [..] u/Z1#f(3e454b8ccf442868).
//              ^ definition [..] u/Z1#f(d4f767463ce0a6b3).
    };
  
    void use_made_public(Z1 z1) {
//       ^^^^^^^^^^^^^^^ definition [..] u/use_made_public(eb27b0ef67ae3f66).
//                       ^^ reference [..] u/Z1#
//                          ^^ definition local 2
      z1.f(0);
//    ^^ reference local 2
//       ^ reference [..] u/Z0#f(d4f767463ce0a6b3).
      z1.f(0, 0);
//    ^^ reference local 2
//       ^ reference [..] u/Z0#f(3e454b8ccf442868).
    }
  }
