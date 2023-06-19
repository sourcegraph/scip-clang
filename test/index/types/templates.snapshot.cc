  // extra-args: -std=c++17
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/templates.cc`/
  
  template <typename ...Args>
//                      ^^^^ definition local 0
  struct S0 {
//       ^^ definition [..] S0#
    void f(Args... args) {}
//       ^ definition [..] S0#f(f327490b5edb0dcb).
//         ^^^^ reference local 0
//                 ^^^^ definition local 1
  };
  
  template <typename A, typename B, template <typename> typename F>
//                   ^ definition local 2
//                               ^ definition local 3
//                                                               ^ definition local 4
  F<B> fmap(A f(B), F<A> fa) {
//^ reference local 4
//  ^ reference local 3
//     ^^^^ definition [..] fmap(48b319d339a486cb).
//          ^ reference local 2
//            ^ definition local 5
//              ^ reference local 3
//                  ^ reference local 4
//                    ^ reference local 2
//                       ^^ definition local 6
    return fa.fmap(f);
//         ^^ reference local 6
//                 ^ reference local 5
  }
  
  template <int N>
//              ^ definition local 7
  void f(int arr[N]) {}
//     ^ definition [..] f(11b0e290e57a7e53).
//           ^^^ definition local 8
//               ^ reference local 7
  
  template <typename... Bs, template <typename...> typename... As>
//                      ^^ definition local 9
//                                                             ^^ definition local 10
  void g(As<Bs...> ...) {}
//     ^ definition [..] g(c3d59a70a6e5360c).
//       ^^ reference local 10
//          ^^ reference local 9
  
  template <typename T>
//                   ^ definition local 11
  struct PointerType {
//       ^^^^^^^^^^^ definition [..] PointerType#
    using type = T *;
//        ^^^^ definition [..] PointerType#type#
//               ^ reference local 11
  };
  
  template <typename T>
//                   ^ definition local 12
  struct PointerType<T &> {
//       ^^^^^^^^^^^ definition [..] PointerType#
//                   ^ reference local 12
    using type = T *;
//        ^^^^ definition [..] PointerType#type#
//               ^ reference local 12
  };
  
  template <typename T>
//                   ^ definition local 13
  using RefPtr = typename PointerType<T &>::type;
//      ^^^^^^ definition [..] RefPtr#
//                        ^^^^^^^^^^^ reference [..] PointerType#
  
  using IntRefPtr = RefPtr<int>;
//      ^^^^^^^^^ definition [..] IntRefPtr#
//                  ^^^^^^ reference [..] RefPtr#
  
  template <typename T>
//                   ^ definition local 14
  using PtrPtr = typename PointerType<T *>::type;
//      ^^^^^^ definition [..] PtrPtr#
//                        ^^^^^^^^^^^ reference [..] PointerType#
  
  template <typename T, typename S = typename PointerType<T>::type>
//                   ^ definition local 15
//                               ^ definition local 16
//                                            ^^^^^^^^^^^ reference [..] PointerType#
  void specialized(T) {}
//     ^^^^^^^^^^^ definition [..] specialized(9b289cee16747614).
//                 ^ reference local 15
  
  template <>
  void specialized<int, int>(int) {}
//     ^^^^^^^^^^^ definition [..] specialized(d4f767463ce0a6b3).
  
  template <typename T>
//                   ^ definition local 17
  struct Empty {};
//       ^^^^^ definition [..] Empty#
  
  void use_empty() {
//     ^^^^^^^^^ definition [..] use_empty(49f6e7a06ebc5aa8).
    Empty<int> x;
//  ^^^^^ reference [..] Empty#
//             ^ definition local 18
    Empty<Empty<void>> y;
//  ^^^^^ reference [..] Empty#
//        ^^^^^ reference [..] Empty#
//                     ^ definition local 19
    Empty<PointerType<void *>> z;
//  ^^^^^ reference [..] Empty#
//        ^^^^^^^^^^^ reference [..] PointerType#
//                             ^ definition local 20
    RefPtr<int> w;
//  ^^^^^^ reference [..] RefPtr#
//              ^ definition local 21
  }
  
  template <typename T>
//                   ^ definition local 22
  struct M0 {
//       ^^ definition [..] M0#
    using A = int;
//        ^ definition [..] M0#A#
  };
  
  template <typename T>
//                   ^ definition local 23
  struct M1: M0<T> {
//       ^^ definition [..] M1#
//       relation implementation [..] M0#
//           ^^ reference [..] M0#
//              ^ reference local 23
    using B = M0<T>;
//        ^ definition [..] M1#B#
//            ^^ reference [..] M0#
//               ^ reference local 23
    using B::A;
//        ^ reference [..] M1#B#
  };
