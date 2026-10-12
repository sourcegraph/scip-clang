struct String {};
String operator""_tag(const char *, unsigned long) {
  return {};
}
String operator""_wide(const wchar_t *, unsigned long) {
  return {};
}
int operator""_number(unsigned long long value) {
  return value;
}
#define RAW_TAG R"(macro)"_tag
void controls() {
  auto plain = "plain"_tag;
  auto raw = R"(raw)"_tag;
  auto quoted = R"delimiter(a "quoted" word)delimiter"_tag;
  auto wide = LR"(wide)"_wide;
  auto macro = RAW_TAG;
  auto explicitCall = operator""_tag("explicit", 8);
  auto number = 12_number;
  auto concatenated = "first"
                      R"(
word \
word \
word \
word \
end
)"_tag;
  auto cookedLast = R"(
word \
word \
word \
word \
end
)"
                    "last"_tag;
}
