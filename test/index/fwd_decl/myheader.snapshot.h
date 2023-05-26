  
// definition [..] `<file>/myheader.h`/
  /// Look, a function!
  void f();
//     ^ reference [..] f(49f6e7a06ebc5aa8).
  
  /// Look, a struct!
  struct S;
//       ^ reference [..] S#
  
  struct C {
//       ^ definition [..] C#
    /// Look, a method!
    void m();
//       ^ reference [..] C#m(49f6e7a06ebc5aa8).
  };
  
  /// Look, a global!
  extern int Global;
//           ^^^^^^ definition [..] Global.
