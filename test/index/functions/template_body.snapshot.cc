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
    (void)C().dependent_field;
//        ^ reference [..] f(49f6e7a06ebc5aa8).C#
    C().g();
//  ^ reference [..] f(49f6e7a06ebc5aa8).C#
  
    int x = 0;
//      ^ definition local 1
    (void)(2 * x);
//             ^ reference local 1
  
    // The following are not allowed:
    // - Templated function-local classes
    // - Templates inside function-local classes
  }
