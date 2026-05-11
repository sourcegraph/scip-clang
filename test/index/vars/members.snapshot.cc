  struct S0 {
//^^^^^^ definition [..] `<file>/members.cc`/
//kind File
//       ^^ definition [..] S0#
//       kind Struct
    int x = a;
//      ^ definition [..] S0#x.
//      kind Field
//          ^ reference [..] S0#a.
    static constexpr int a = 0;
//                       ^ definition [..] S0#a.
//                       kind StaticDataMember
    static int y;
//             ^ definition [..] S0#y.
//             kind StaticDataMember
  
    S0(): x(a) {
//  ^^ definition [..] S0#S0(49f6e7a06ebc5aa8).
//  kind Constructor
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
//        kind StaticDataMember
  
  struct S1: S0 {
//       ^^ definition [..] S1#
//       kind Struct
//       relation implementation [..] S0#
//           ^^ reference [..] S0#
    S1(): S0() {
//  ^^ definition [..] S1#S1(49f6e7a06ebc5aa8).
//  kind Constructor
//        ^^ reference [..] S0#
//        ^^ reference [..] S0#S0(49f6e7a06ebc5aa8).
      x = y;
//    ^ reference [..] S0#x.
//        ^ reference [..] S0#y.
    }
  };
  
  struct S2 {
//       ^^ definition [..] S2#
//       kind Struct
    struct { int a; };
//  ^^^^^^ definition [..] S2#$anonymous_type_0#
//  kind Struct
//               ^ definition [..] S2#$anonymous_type_0#a.
//               kind Field
    union u { float x; int y; };
//        ^ definition [..] S2#u#
//        kind Union
//                  ^ definition [..] S2#u#x.
//                  kind Field
//                         ^ definition [..] S2#u#y.
//                         kind Field
    int : 4;
    int b: 3;
//      ^ definition [..] S2#b.
//      kind Field
  
    S2(): b(1) {
//  ^^ definition [..] S2#S2(49f6e7a06ebc5aa8).
//  kind Constructor
//        ^ reference [..] S2#b.
      a = 10; // Indirect field access
//    ^ reference [..] S2#$anonymous_type_0#a.
    }
  };
  
