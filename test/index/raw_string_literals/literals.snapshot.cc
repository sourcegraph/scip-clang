  struct String {};
//^^^^^^ definition [..] `<file>/literals.cc`/
//       ^^^^^^ definition [..] String#
  String operator""_tag(const char *, unsigned long) {
//^^^^^^ reference [..] String#
//       ^^^^^^^^ definition [..] `operator""_tag`(c7f88d541c78adbf).
    return {};
  }
  String operator""_wide(const wchar_t *, unsigned long) {
//^^^^^^ reference [..] String#
//       ^^^^^^^^ definition [..] `operator""_wide`(6a168caf4fce7620).
    return {};
  }
  int operator""_number(unsigned long long value) {
//    ^^^^^^^^ definition [..] `operator""_number`(642c2e9948c4ad2).
//                                         ^^^^^ definition local 0
    return value;
//         ^^^^^ reference local 0
  }
  #define RAW_TAG R"(macro)"_tag
//        ^^^^^^^ definition [..] `literals.cc:11:9`!
  void controls() {
//     ^^^^^^^^ definition [..] controls(49f6e7a06ebc5aa8).
    auto plain = "plain"_tag;
//       ^^^^^ definition local 1
//                      ^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
    auto raw = R"(raw)"_tag;
//       ^^^ definition local 2
//                     ^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
    auto quoted = R"delimiter(a "quoted" word)delimiter"_tag;
//       ^^^^^^ definition local 3
//                                                      ^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
    auto wide = LR"(wide)"_wide;
//       ^^^^ definition local 4
//                        ^^^^^ reference [..] `operator""_wide`(6a168caf4fce7620).
    auto macro = RAW_TAG;
//       ^^^^^ definition local 5
//               ^^^^^^^ reference [..] `literals.cc:11:9`!
//               ^^^^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
    auto explicitCall = operator""_tag("explicit", 8);
//       ^^^^^^^^^^^^ definition local 6
//                      ^^^^^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
    auto number = 12_number;
//       ^^^^^^ definition local 7
//                  ^^^^^^^ reference [..] `operator""_number`(642c2e9948c4ad2).
    auto concatenated = "first"
//       ^^^^^^^^^^^^ definition local 8
                        R"(
  word \
  word \
  word \
  word \
  end
  )"_tag;
//  ^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
    auto cookedLast = R"(
//       ^^^^^^^^^^ definition local 9
  word \
  word \
  word \
  word \
  end
  )"
                      "last"_tag;
//                          ^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
  }
