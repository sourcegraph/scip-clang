  // format-options: showDocs
//^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/doc_check_def.cc`/
//documentation
//| File: doc_check_def.cc
//kind File
  
  /// Doc comment on definition
  class F {
//      ^ definition [..] F#
//      documentation
//      | Doc comment on definition
//      kind Class
  };
  
  class UndocumentedClass {
//      ^^^^^^^^^^^^^^^^^ definition [..] UndocumentedClass#
//      documentation
//      | From forward decl
//      kind Class
  };
