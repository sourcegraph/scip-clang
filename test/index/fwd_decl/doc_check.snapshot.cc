  // format-options: showDocs
//^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/doc_check.cc`/
//documentation
//| File: doc_check.cc
  
  /// Doc comment on forward decl
  class F;
//      ^ reference [..] F#
  
  /// From forward decl
  class UndocumentedClass;
//      ^^^^^^^^^^^^^^^^^ reference [..] UndocumentedClass#
  
  void f(F *) {}
//     ^ definition [..] f(84962a9dbd25570f).
//     documentation
//     | No documentation available.
//       ^ reference [..] F#
