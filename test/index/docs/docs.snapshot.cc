  // format-options: showDocs
//^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/docs.cc`/
//documentation
//| File: docs.cc
  
  /// Dumhdurum!
  enum class Apink {
//           ^^^^^ definition [..] Apink#
//           documentation
//           | Dumhdurum!
    /** Leader
     * Lead Dancer */
    Chorong,
//  ^^^^^^^ definition [..] Apink#Chorong.
//  documentation
//  | Leader
//  | Lead Dancer
    // Vocalist
    Bomi,
//  ^^^^ definition [..] Apink#Bomi.
//  documentation
//  | Vocalist
    Eunji,  // Main Vocalist
//  ^^^^^ definition [..] Apink#Eunji.
//  documentation
//  | Main Vocalist
//  | From Busan
            // From Busan
    /* Departed
     * :( */
    Naeun,
//  ^^^^^ definition [..] Apink#Naeun.
//  documentation
//  | Departed
//  | :(
    Namjoo, /* Vocalist */
//  ^^^^^^ definition [..] Apink#Namjoo.
//  documentation
//  | Vocalist
    //! Maknae
    Hayoung,
//  ^^^^^^^ definition [..] Apink#Hayoung.
//  documentation
//  | Maknae
  };
  
  /// Ominous sounds
  struct Ghost;
//       ^^^^^ reference [..] Ghost#
  
  /// Boo!
  typedef struct Ghost {} Ghost;
//               ^^^^^ definition [..] Ghost#
//               documentation
//               | Boo!
//                        ^^^^^ definition [..] Ghost#
//                        documentation
//                        | Boo!
