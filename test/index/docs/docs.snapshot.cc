  // format-options: showDocs
//^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/docs.cc`/
//documentation
//| File: docs.cc
//kind File
  
  /// Dumhdurum!
  enum class Apink {
//           ^^^^^ definition [..] Apink#
//           documentation
//           | Dumhdurum!
//           kind Enum
    /** Leader
     * Lead Dancer */
    Chorong,
//  ^^^^^^^ definition [..] Apink#Chorong.
//  documentation
//  | Leader
//  | Lead Dancer
//  kind EnumMember
    // Vocalist
    Bomi,
//  ^^^^ definition [..] Apink#Bomi.
//  documentation
//  | Vocalist
//  kind EnumMember
    Eunji,  // Main Vocalist
//  ^^^^^ definition [..] Apink#Eunji.
//  documentation
//  | Main Vocalist
//  | From Busan
//  kind EnumMember
            // From Busan
    /* Departed
     * :( */
    Naeun,
//  ^^^^^ definition [..] Apink#Naeun.
//  documentation
//  | Departed
//  | :(
//  kind EnumMember
    Namjoo, /* Vocalist */
//  ^^^^^^ definition [..] Apink#Namjoo.
//  documentation
//  | Vocalist
//  kind EnumMember
    //! Maknae
    Hayoung,
//  ^^^^^^^ definition [..] Apink#Hayoung.
//  documentation
//  | Maknae
//  kind EnumMember
  };
  
  /// Ominous sounds
  struct Ghost;
//       ^^^^^ reference [..] Ghost#
  
  /// Boo!
  typedef struct Ghost {} Ghost;
//               ^^^^^ definition [..] Ghost#
//               documentation
//               | Boo!
//               kind Struct
//                        ^^^^^ definition [..] Ghost#
//                        documentation
//                        | Boo!
//                        kind Struct
