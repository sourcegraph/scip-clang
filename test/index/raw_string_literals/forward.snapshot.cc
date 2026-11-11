  struct String {};
//^^^^^^ definition [..] `<file>/forward.cc`/
//       ^^^^^^ definition [..] String#
  String First();
//^^^^^^ reference [..] String#
//       ^^^^^ reference [..] First(4d33a72ea54f6f2a).
  String operator""_tag(const char *, unsigned long);
//^^^^^^ reference [..] String#
//       ^^^^^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
  String operator+(String, String);
//^^^^^^ reference [..] String#
//       ^^^^^^^^ reference [..] operator+(d8ccc96415cd066f).
//                 ^^^^^^ reference [..] String#
//                         ^^^^^^ reference [..] String#
  String Value() {
//^^^^^^ reference [..] String#
//       ^^^^^ definition [..] Value(4d33a72ea54f6f2a).
    return First() + R"(
//         ^^^^^ reference [..] First(4d33a72ea54f6f2a).
//                 ^ reference [..] operator+(d8ccc96415cd066f).
  word \
  word \
  word \
  word \
  end
  )"_tag;
//  ^^^^ reference [..] `operator""_tag`(c7f88d541c78adbf).
  }
