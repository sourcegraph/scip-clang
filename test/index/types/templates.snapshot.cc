  // extra-args: -std=c++17
  
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
