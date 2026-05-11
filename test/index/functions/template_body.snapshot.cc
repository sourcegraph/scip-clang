  template <typename T>
//^^^^^^^^ definition [..] `<file>/template_body.cc`/
//kind File
//                   ^ definition local 0
//                   kind TypeParameter
  void f() {
//     ^ definition [..] f(49f6e7a06ebc5aa8).
//     kind Function
    struct C {
//         ^ definition [..] f(49f6e7a06ebc5aa8).C#
//         kind Struct
      int plain_field;
//        ^^^^^^^^^^^ definition [..] f(49f6e7a06ebc5aa8).C#plain_field.
//        kind Field
      T dependent_field;
//    ^ reference local 0
//      ^^^^^^^^^^^^^^^ definition [..] f(49f6e7a06ebc5aa8).C#dependent_field.
//      kind Field
  
      void g() {};
//         ^ definition [..] f(49f6e7a06ebc5aa8).C#g(49f6e7a06ebc5aa8).
//         kind Method
    };
  
    (void)C().plain_field;
//        ^ reference [..] f(49f6e7a06ebc5aa8).C#
//            ^^^^^^^^^^^ reference [..] f(49f6e7a06ebc5aa8).C#plain_field.
    (void)C().dependent_field;
//        ^ reference [..] f(49f6e7a06ebc5aa8).C#
//            ^^^^^^^^^^^^^^^ reference [..] f(49f6e7a06ebc5aa8).C#dependent_field.
    C().g();
//  ^ reference [..] f(49f6e7a06ebc5aa8).C#
//      ^ reference [..] f(49f6e7a06ebc5aa8).C#g(49f6e7a06ebc5aa8).
  
    int x = 0;
//      ^ definition local 1
//      kind Variable
    (void)(2 * x);
//             ^ reference local 1
  
    // The following are not allowed:
    // - Templated function-local classes
    // - Templates inside function-local classes
  }
  
  template <typename T>
//                   ^ definition local 2
//                   kind TypeParameter
  struct Z {
//       ^ definition [..] Z#
//       kind Struct
    void f0() {}
//       ^^ definition [..] Z#f0(49f6e7a06ebc5aa8).
//       kind Method
  
    void f1() {
//       ^^ definition [..] Z#f1(49f6e7a06ebc5aa8).
//       kind Method
      f0();
//    ^^ reference [..] Z#f0(49f6e7a06ebc5aa8).
    }
  
    template <typename U>
//                     ^ definition local 3
//                     kind TypeParameter
    void g0() {
//       ^^ definition [..] Z#g0(49f6e7a06ebc5aa8).
//       kind Method
      f0();
//    ^^ reference [..] Z#f0(49f6e7a06ebc5aa8).
    }
  
    template <typename U>
//                     ^ definition local 4
//                     kind TypeParameter
    void g1() {
//       ^^ definition [..] Z#g1(49f6e7a06ebc5aa8).
//       kind Method
      g0<U>();
//    ^^ reference [..] Z#g0(49f6e7a06ebc5aa8).
//       ^ reference local 4
    }
  };
  
  template <typename T>
//                   ^ definition local 5
//                   kind TypeParameter
  struct ZZ : Z<T> {
//       ^^ definition [..] ZZ#
//       kind Struct
//       relation implementation [..] Z#
//            ^ reference [..] Z#
//              ^ reference local 5
    void ff0() {
//       ^^^ definition [..] ZZ#ff0(49f6e7a06ebc5aa8).
//       kind Method
      this->f0();
//          ^^ reference [..] Z#f0(49f6e7a06ebc5aa8).
    }
  
    template <typename U>
//                     ^ definition local 6
//                     kind TypeParameter
    void gg0() {
//       ^^^ definition [..] ZZ#gg0(49f6e7a06ebc5aa8).
//       kind Method
      this->f0();
//          ^^ reference [..] Z#f0(49f6e7a06ebc5aa8).
      this->template g0<U>();
//                   ^^ reference [..] Z#g0(49f6e7a06ebc5aa8).
//                      ^ reference local 6
    }
  };
