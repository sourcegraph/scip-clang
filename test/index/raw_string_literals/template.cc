// extra-args: -std=c++20
template <unsigned long N> struct Text {
  char value[N];
  consteval Text(const char (&s)[N]) {
    for (unsigned long i = 0; i < N; ++i)
      value[i] = s[i];
  }
};
template <Text T> auto operator""_tag() {
  return T;
}
auto Value() {
  return R"(
word \
word \
word \
word \
end
)"_tag;
}
