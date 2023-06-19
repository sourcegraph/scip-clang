  template <typename T>
//^^^^^^^^ definition [..] `<file>/template_body.cc`/
//                   ^ definition local 0
  void f() {
//     ^ definition [..] f(49f6e7a06ebc5aa8).
    struct C {
//         ^ definition [..] f(49f6e7a06ebc5aa8).C#
      int plain_field;
//        ^^^^^^^^^^^ definition [..] f(49f6e7a06ebc5aa8).C#plain_field.
      T dependent_field;
//    ^ reference local 0
//      ^^^^^^^^^^^^^^^ definition [..] f(49f6e7a06ebc5aa8).C#dependent_field.
  
      void g() {};
//         ^ definition [..] f(49f6e7a06ebc5aa8).C#g(49f6e7a06ebc5aa8).
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
    (void)(2 * x);
//             ^ reference local 1
  
    // The following are not allowed:
    // - Templated function-local classes
    // - Templates inside function-local classes
  }
  
  template <typename T>
//                   ^ definition local 2
  struct Z {
//       ^ definition [..] Z#
    void f0() {}
//       ^^ definition [..] Z#f0(49f6e7a06ebc5aa8).
  
    void f1() {
//       ^^ definition [..] Z#f1(49f6e7a06ebc5aa8).
      f0();
//    ^^ reference [..] Z#f0(49f6e7a06ebc5aa8).
    }
  
    template <typename U>
//                     ^ definition local 3
    void g0() {
//       ^^ definition [..] Z#g0(49f6e7a06ebc5aa8).
      f0();
//    ^^ reference [..] Z#f0(49f6e7a06ebc5aa8).
    }
  
    template <typename U>
//                     ^ definition local 4
    void g1() {
//       ^^ definition [..] Z#g1(49f6e7a06ebc5aa8).
      g0<U>();
//    ^^ reference [..] Z#g0(49f6e7a06ebc5aa8).
//       ^ reference local 4
    }
  };
  
  template <typename T>
//                   ^ definition local 5
  struct ZZ : Z<T> {
//       ^^ definition [..] ZZ#
//       relation implementation [..] Z#
//            ^ reference [..] Z#
//              ^ reference local 5
    void ff0() {
//       ^^^ definition [..] ZZ#ff0(49f6e7a06ebc5aa8).
      this->f0();
//          ^^ reference [..] Z#f0(49f6e7a06ebc5aa8).
    }
  
    template <typename U>
//                     ^ definition local 6
    void gg0() {
//       ^^^ definition [..] ZZ#gg0(49f6e7a06ebc5aa8).
      this->f0();
//          ^^ reference [..] Z#f0(49f6e7a06ebc5aa8).
      this->template g0<U>();
//                   ^^ reference [..] Z#g0(49f6e7a06ebc5aa8).
//                      ^ reference local 6
    }
  };
