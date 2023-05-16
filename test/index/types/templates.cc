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

template <typename... Bs, template <typename...> typename... As>
void g(As<Bs...> ...) {}

template <typename T>
struct PointerType {
  using type = T *;
};

template <typename T>
struct PointerType<T &> {
  using type = T *;
};

template <typename T>
using RefPtr = typename PointerType<T &>::type;

using IntRefPtr = RefPtr<int>;

template <typename T>
using PtrPtr = typename PointerType<T *>::type;

template <typename T, typename S = typename PointerType<T>::type>
void specialized(T) {}

template <>
void specialized<int, int>(int) {}

template <typename T>
struct Empty {};

void use_empty() {
  Empty<int> x;
  Empty<Empty<void>> y;
  Empty<PointerType<void *>> z;
  RefPtr<int> w;
}

template <typename T>
struct M0 {
  using A = int;
};

template <typename T>
struct M1: M0<T> {
  using B = M0<T>;
  using B::A;
};