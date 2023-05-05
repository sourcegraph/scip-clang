template <typename T>
T zero = 0;

template <typename T>
struct C {
  int mono = 0;

  T from_param = 0;

  static int static_mono;
  static const int static_const_mono = 0;

  static T static_from_param;
  static const T static_const_from_param = T();

  template <typename U>
  static const U static_templated = 0;
};

template <typename T>
int C<T>::static_mono = 0;

template <typename T>
T C<T>::static_from_param = 0;

void test() {
  (void)zero<int>;

  (void)C<int>().mono;
  (void)C<int>().from_param;

  (void)C<int>::static_mono;
  (void)C<int>::static_const_mono;
  (void)C<int>::static_from_param;
  (void)C<int>::static_const_from_param;
  (void)C<int>::static_templated<int>;
}
