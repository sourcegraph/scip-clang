  // extra-args: -std=c++20
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/template.cc`/
  template <unsigned long N> struct Text {
//                        ^ definition local 0
//                                  ^^^^ definition [..] Text#
    char value[N];
//       ^^^^^ definition [..] Text#value.
//             ^ reference local 0
    consteval Text(const char (&s)[N]) {
//            ^^^^ definition [..] Text#`Text<N>`(12ba0ad39e13c681).
//                              ^ definition local 1
//                                 ^ reference local 0
      for (unsigned long i = 0; i < N; ++i)
//                       ^ definition local 2
//                              ^ reference local 2
//                                  ^ reference local 0
//                                       ^ reference local 2
        value[i] = s[i];
//      ^^^^^ reference [..] Text#value.
//            ^ reference local 2
//                 ^ reference local 1
//                   ^ reference local 2
    }
  };
  template <Text T> auto operator""_tag() {
//               ^ definition local 3
//                       ^^^^^^^^ definition [..] `operator""_tag`(a52531b27a9fa42b).
    return T;
//         ^ reference local 3
  }
  auto Value() {
//     ^^^^^ definition [..] Value(fe7742cb122a66a2).
    return R"(
  word \
  word \
  word \
  word \
  end
  )"_tag;
//  ^^^^ reference [..] `operator""_tag`(a52531b27a9fa42b).
  }
