  template <typename T>
//^^^^^^^^ definition [..] `<file>/templates.cc`/
//                   ^ definition local 0
  T zero = 0;
//^ reference local 0
//  ^^^^ definition [..] zero.
//  ^^^^ definition [..] zero.
  
  template <typename T>
//                   ^ definition local 1
  struct C {
//       ^ definition [..] C#
    int mono = 0;
//      ^^^^ definition [..] C#mono.
  
    T from_param = 0;
//  ^ reference local 1
//    ^^^^^^^^^^ definition [..] C#from_param.
  
    static int static_mono;
//             ^^^^^^^^^^^ definition [..] C#static_mono.
    static const int static_const_mono = 0;
//                   ^^^^^^^^^^^^^^^^^ definition [..] C#static_const_mono.
  
    static T static_from_param;
//         ^ reference local 1
//           ^^^^^^^^^^^^^^^^^ definition [..] C#static_from_param.
    static const T static_const_from_param = T();
//               ^ reference local 1
//                 ^^^^^^^^^^^^^^^^^^^^^^^ definition [..] C#static_const_from_param.
//                                           ^ reference local 1
  
    template <typename U>
//                     ^ definition local 2
    static const U static_templated = 0;
//               ^ reference local 2
//                 ^^^^^^^^^^^^^^^^ definition [..] C#static_templated.
  };
  
  template <typename T>
//                   ^ definition local 3
  int C<T>::static_mono = 0;
//    ^ reference [..] C#
//    ^ reference [..] C#
//          ^^^^^^^^^^^ definition [..] C#static_mono.
//          ^^^^^^^^^^^ definition [..] C#static_mono.
  
  template <typename T>
//                   ^ definition local 4
  T C<T>::static_from_param = 0;
//^ reference local 4
//  ^ reference [..] C#
//  ^ reference [..] C#
//        ^^^^^^^^^^^^^^^^^ definition [..] C#static_from_param.
//        ^^^^^^^^^^^^^^^^^ definition [..] C#static_from_param.
  
  void test() {
//     ^^^^ definition [..] test(49f6e7a06ebc5aa8).
    (void)zero<int>;
//        ^^^^ reference [..] zero.
  
    (void)C<int>().mono;
//        ^ reference [..] C#
//                 ^^^^ reference [..] C#mono.
    (void)C<int>().from_param;
//        ^ reference [..] C#
//                 ^^^^^^^^^^ reference [..] C#from_param.
  
    (void)C<int>::static_mono;
//        ^ reference [..] C#
//                ^^^^^^^^^^^ reference [..] C#static_mono.
    (void)C<int>::static_const_mono;
//        ^ reference [..] C#
//                ^^^^^^^^^^^^^^^^^ reference [..] C#static_const_mono.
    (void)C<int>::static_from_param;
//        ^ reference [..] C#
//                ^^^^^^^^^^^^^^^^^ reference [..] C#static_from_param.
    (void)C<int>::static_const_from_param;
//        ^ reference [..] C#
//                ^^^^^^^^^^^^^^^^^^^^^^^ reference [..] C#static_const_from_param.
    (void)C<int>::static_templated<int>;
//        ^ reference [..] C#
//                ^^^^^^^^^^^^^^^^ reference [..] C#static_templated.
  }
