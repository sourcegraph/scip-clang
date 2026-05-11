  template <typename T>
//^^^^^^^^ definition [..] `<file>/templates.cc`/
//kind File
//                   ^ definition local 0
//                   kind TypeParameter
  T zero = 0;
//^ reference local 0
//  ^^^^ definition [..] zero.
//  kind Variable
//  ^^^^ definition [..] zero.
//  kind Variable
  
  template <typename T>
//                   ^ definition local 1
//                   kind TypeParameter
  struct C {
//       ^ definition [..] C#
//       kind Struct
    int mono = 0;
//      ^^^^ definition [..] C#mono.
//      kind Field
  
    T from_param = 0;
//  ^ reference local 1
//    ^^^^^^^^^^ definition [..] C#from_param.
//    kind Field
  
    static int static_mono;
//             ^^^^^^^^^^^ definition [..] C#static_mono.
//             kind StaticDataMember
    static const int static_const_mono = 0;
//                   ^^^^^^^^^^^^^^^^^ definition [..] C#static_const_mono.
//                   kind StaticDataMember
  
    static T static_from_param;
//         ^ reference local 1
//           ^^^^^^^^^^^^^^^^^ definition [..] C#static_from_param.
//           kind StaticDataMember
    static const T static_const_from_param = T();
//               ^ reference local 1
//                 ^^^^^^^^^^^^^^^^^^^^^^^ definition [..] C#static_const_from_param.
//                 kind StaticDataMember
//                                           ^ reference local 1
  
    template <typename U>
//                     ^ definition local 2
//                     kind TypeParameter
    static const U static_templated = 0;
//               ^ reference local 2
//                 ^^^^^^^^^^^^^^^^ definition [..] C#static_templated.
//                 kind StaticDataMember
  };
  
  template <typename T>
//                   ^ definition local 3
//                   kind TypeParameter
  int C<T>::static_mono = 0;
//    ^ reference [..] C#
//    ^ reference [..] C#
//          ^^^^^^^^^^^ definition [..] C#static_mono.
//          kind StaticDataMember
//          ^^^^^^^^^^^ definition [..] C#static_mono.
//          kind StaticDataMember
  
  template <typename T>
//                   ^ definition local 4
//                   kind TypeParameter
  T C<T>::static_from_param = 0;
//^ reference local 4
//  ^ reference [..] C#
//  ^ reference [..] C#
//        ^^^^^^^^^^^^^^^^^ definition [..] C#static_from_param.
//        kind StaticDataMember
//        ^^^^^^^^^^^^^^^^^ definition [..] C#static_from_param.
//        kind StaticDataMember
  
  void test() {
//     ^^^^ definition [..] test(49f6e7a06ebc5aa8).
//     kind Function
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
