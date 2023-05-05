  template <typename T>
//^^^^^^^^ definition [..] `<file>/template_body.cc`/
//                   ^ definition local 0
  void f() {
//     ^ definition [..] f(49f6e7a06ebc5aa8).
    struct C {
      T field;
//    ^ reference local 0
  
      void g() {};
    };
  
    auto t = C().field;
//       ^ definition local 1
    C().g();
  
    int x = 0;
//      ^ definition local 2
    (void)(2 * x);
//             ^ reference local 2
  
    // The following are not allowed:
    // - Templated function-local classes
    // - Templates inside function-local classes
  }
