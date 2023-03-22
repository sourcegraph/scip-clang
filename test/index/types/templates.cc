// extra-args: -std=c++17

template <typename ...Args>
struct S0 {
  void f(Args... args) {}
};

template <typename A, typename B, template <typename> typename F>
F<B> fmap(A f(B), F<A> fa) {
  return fa.fmap(f);
}

template <int N>
void f(int arr[N]) {}
