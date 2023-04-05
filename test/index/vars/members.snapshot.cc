  struct S0 {
//^^^^^^ definition [..] `<file>/members.cc`/
//       ^^ definition [..] S0#
    int x = a;
//      ^ definition [..] S0#x.
//          ^ reference [..] S0#a.
    static constexpr int a = 0;
//                       ^ definition [..] S0#a.
    static int y;
//             ^ definition [..] S0#y.
  
    S0(): x(a) {
//  ^^ definition [..] S0#S0(49f6e7a06ebc5aa8).
//        ^ reference [..] S0#x.
//          ^ reference [..] S0#a.
      x += y;
//    ^ reference [..] S0#x.
//         ^ reference [..] S0#y.
    }
  };
  
  int S0::y = 3;
//    ^^ reference [..] S0#
//        ^ definition [..] S0#y.
  
  struct S1: S0 {
//       ^^ definition [..] S1#
//       relation implementation [..] S0#
//           ^^ reference [..] S0#
    S1(): S0() {
//  ^^ definition [..] S1#S1(49f6e7a06ebc5aa8).
//        ^^ reference [..] S0#
//        ^^ reference [..] S0#S0(49f6e7a06ebc5aa8).
      x = y;
//    ^ reference [..] S0#x.
//        ^ reference [..] S0#y.
    }
  };
  
  struct S2 {
//       ^^ definition [..] S2#
    struct { int a; };
//  ^^^^^^ definition [..] S2#$anonymous_type_0#
//               ^ definition [..] S2#$anonymous_type_0#a.
    union u { float x; int y; };
//        ^ definition [..] S2#u#
//                  ^ definition [..] S2#u#x.
//                         ^ definition [..] S2#u#y.
    int : 4;
    int b: 3;
//      ^ definition [..] S2#b.
  
    S2(): b(1) {
//  ^^ definition [..] S2#S2(49f6e7a06ebc5aa8).
//        ^ reference [..] S2#b.
      a = 10; // Indirect field access
//    ^ reference [..] S2#$anonymous_type_0#a.
    }
  };
  
