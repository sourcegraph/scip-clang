  void f() {
//^^^^ definition [..] `<file>/body.cc`/
//kind File
//     ^ definition [..] f(49f6e7a06ebc5aa8).
//     kind Function
    struct C {
//         ^ definition [..] f(49f6e7a06ebc5aa8).C#
//         kind Struct
      int plain_field;
//        ^^^^^^^^^^^ definition [..] f(49f6e7a06ebc5aa8).C#plain_field.
//        kind Field
  
      void g() {};
//         ^ definition [..] f(49f6e7a06ebc5aa8).C#g(49f6e7a06ebc5aa8).
//         kind Method
    };
  
    (void)C().plain_field;
//        ^ reference [..] f(49f6e7a06ebc5aa8).C#
//            ^^^^^^^^^^^ reference [..] f(49f6e7a06ebc5aa8).C#plain_field.
    C().g();
//  ^ reference [..] f(49f6e7a06ebc5aa8).C#
//      ^ reference [..] f(49f6e7a06ebc5aa8).C#g(49f6e7a06ebc5aa8).
  
    int x = 0;
//      ^ definition local 0
//      kind Variable
    (void)(2 * x);
//             ^ reference local 0
  }
