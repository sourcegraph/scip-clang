  // extra-args: -std=c++17
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/templates.cc`/
//kind File
  
  template <typename ...Args>
//                      ^^^^ definition local 0
//                      kind TypeParameter
  struct S0 {
//       ^^ definition [..] S0#
//       kind Struct
    void f(Args... args) {}
//       ^ definition [..] S0#f(f327490b5edb0dcb).
//       kind Method
//         ^^^^ reference local 0
//                 ^^^^ definition local 1
//                 kind Parameter
  };
  
  template <typename A, typename B, template <typename> typename F>
//                   ^ definition local 2
//                   kind TypeParameter
//                               ^ definition local 3
//                               kind TypeParameter
//                                                               ^ definition local 4
//                                                               kind TypeParameter
  F<B> fmap(A f(B), F<A> fa) {
//^ reference local 4
//  ^ reference local 3
//     ^^^^ definition [..] fmap(22a739e3a02d9724).
//     kind Function
//          ^ reference local 2
//            ^ definition local 5
//            kind Parameter
//              ^ reference local 3
//                  ^ reference local 4
//                    ^ reference local 2
//                       ^^ definition local 6
//                       kind Parameter
    return fa.fmap(f);
//         ^^ reference local 6
//                 ^ reference local 5
  }
  
  template <int N>
//              ^ definition local 7
//              kind Parameter
  void f(int arr[N]) {}
//     ^ definition [..] f(11b0e290e57a7e53).
//     kind Function
//           ^^^ definition local 8
//           kind Parameter
//               ^ reference local 7
  
  template <typename... Bs, template <typename...> typename... As>
//                      ^^ definition local 9
//                      kind TypeParameter
//                                                             ^^ definition local 10
//                                                             kind TypeParameter
  void g(As<Bs...> ...) {}
//     ^ definition [..] g(da52d0cc43a6b199).
//     kind Function
//       ^^ reference local 10
//          ^^ reference local 9
  
  template <typename T>
//                   ^ definition local 11
//                   kind TypeParameter
  struct PointerType {
//       ^^^^^^^^^^^ definition [..] PointerType#
//       kind Struct
    using type = T *;
//        ^^^^ definition [..] PointerType#type#
//        kind TypeAlias
//               ^ reference local 11
  };
  
  template <typename T>
//                   ^ definition local 12
//                   kind TypeParameter
  struct PointerType<T &> {
//       ^^^^^^^^^^^ definition [..] PointerType#
//       kind Struct
//                   ^ reference local 12
    using type = T *;
//        ^^^^ definition [..] PointerType#type#
//        kind TypeAlias
//               ^ reference local 12
  };
  
  template <typename T>
//                   ^ definition local 13
//                   kind TypeParameter
  using RefPtr = typename PointerType<T &>::type;
//      ^^^^^^ definition [..] RefPtr#
//      kind TypeAlias
//                        ^^^^^^^^^^^ reference [..] PointerType#
  
  using IntRefPtr = RefPtr<int>;
//      ^^^^^^^^^ definition [..] IntRefPtr#
//      kind TypeAlias
//                  ^^^^^^ reference [..] RefPtr#
  
  template <typename T>
//                   ^ definition local 14
//                   kind TypeParameter
  using PtrPtr = typename PointerType<T *>::type;
//      ^^^^^^ definition [..] PtrPtr#
//      kind TypeAlias
//                        ^^^^^^^^^^^ reference [..] PointerType#
  
  template <typename T, typename S = typename PointerType<T>::type>
//                   ^ definition local 15
//                   kind TypeParameter
//                               ^ definition local 16
//                               kind TypeParameter
//                                            ^^^^^^^^^^^ reference [..] PointerType#
  void specialized(T) {}
//     ^^^^^^^^^^^ definition [..] specialized(9b289cee16747614).
//     kind Function
//                 ^ reference local 15
  
  template <>
  void specialized<int, int>(int) {}
//     ^^^^^^^^^^^ definition [..] specialized(d4f767463ce0a6b3).
//     kind Function
  
  template <typename T>
//                   ^ definition local 17
//                   kind TypeParameter
  struct Empty {};
//       ^^^^^ definition [..] Empty#
//       kind Struct
  
  void use_empty() {
//     ^^^^^^^^^ definition [..] use_empty(49f6e7a06ebc5aa8).
//     kind Function
    Empty<int> x;
//  ^^^^^ reference [..] Empty#
//             ^ definition local 18
//             kind Variable
    Empty<Empty<void>> y;
//  ^^^^^ reference [..] Empty#
//        ^^^^^ reference [..] Empty#
//                     ^ definition local 19
//                     kind Variable
    Empty<PointerType<void *>> z;
//  ^^^^^ reference [..] Empty#
//        ^^^^^^^^^^^ reference [..] PointerType#
//                             ^ definition local 20
//                             kind Variable
    RefPtr<int> w;
//  ^^^^^^ reference [..] RefPtr#
//              ^ definition local 21
//              kind Variable
  }
  
  template <typename T>
//                   ^ definition local 22
//                   kind TypeParameter
  struct M0 {
//       ^^ definition [..] M0#
//       kind Struct
    using A = int;
//        ^ definition [..] M0#A#
//        kind TypeAlias
  };
  
  template <typename T>
//                   ^ definition local 23
//                   kind TypeParameter
  struct M1: M0<T> {
//       ^^ definition [..] M1#
//       kind Struct
//       relation implementation [..] M0#
//           ^^ reference [..] M0#
//              ^ reference local 23
    using B = M0<T>;
//        ^ definition [..] M1#B#
//        kind TypeAlias
//            ^^ reference [..] M0#
//               ^ reference local 23
    using B::A;
//        ^ reference [..] M1#B#
  };
