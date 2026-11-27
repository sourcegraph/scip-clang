struct String {};
String First();
String operator""_tag(const char *, unsigned long);
String operator+(String, String);
String Value() {
  return First() + R"(
word \
word \
word \
word \
end
)"_tag;
}
