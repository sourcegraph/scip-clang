  void f() {
//^^^^ definition [..] `<file>/body.cc`/
//     ^ definition [..] f(49f6e7a06ebc5aa8).
    struct C {
//         ^ definition [..] f(49f6e7a06ebc5aa8).C#
      int plain_field;
//        ^^^^^^^^^^^ definition [..] f(49f6e7a06ebc5aa8).C#plain_field.
  
      void g() {};
//         ^ definition [..] f(49f6e7a06ebc5aa8).C#g(49f6e7a06ebc5aa8).
    };
  
    (void)C().plain_field;
//        ^ reference [..] f(49f6e7a06ebc5aa8).C#
//            ^^^^^^^^^^^ reference [..] f(49f6e7a06ebc5aa8).C#plain_field.
    C().g();
//  ^ reference [..] f(49f6e7a06ebc5aa8).C#
//      ^ reference [..] f(49f6e7a06ebc5aa8).C#g(49f6e7a06ebc5aa8).
  
    int x = 0;
//      ^ definition local 0
    (void)(2 * x);
//             ^ reference local 0
  }
