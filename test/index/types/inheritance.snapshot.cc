  // extra-args: -std=c++17
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/inheritance.cc`/
//kind File
  
  struct MonoBase {};
//       ^^^^^^^^ definition [..] MonoBase#
//       kind Struct
  
  struct MonoDerived: MonoBase {};
//       ^^^^^^^^^^^ definition [..] MonoDerived#
//       kind Struct
//       relation implementation [..] MonoBase#
//                    ^^^^^^^^ reference [..] MonoBase#
  
  struct MonoDerivedTwice: MonoDerived {};
//       ^^^^^^^^^^^^^^^^ definition [..] MonoDerivedTwice#
//       kind Struct
//       relation implementation [..] MonoBase#
//       relation implementation [..] MonoDerived#
//                         ^^^^^^^^^^^ reference [..] MonoDerived#
  
  template <typename T>
//                   ^ definition local 0
//                   kind TypeParameter
  struct TemplatedBase {};
//       ^^^^^^^^^^^^^ definition [..] TemplatedBase#
//       kind Struct
  
  template <typename T>
//                   ^ definition local 1
//                   kind TypeParameter
  struct TemplatedDerived: TemplatedBase<T> {};
//       ^^^^^^^^^^^^^^^^ definition [..] TemplatedDerived#
//       kind Struct
//       relation implementation [..] TemplatedBase#
//                         ^^^^^^^^^^^^^ reference [..] TemplatedBase#
//                                       ^ reference local 1
  
  struct DerivedFromInstantiation: TemplatedBase<int> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromInstantiation#
//       kind Struct
//       relation implementation [..] TemplatedBase#
//                                 ^^^^^^^^^^^^^ reference [..] TemplatedBase#
  
  template <typename T>
//                   ^ definition local 2
//                   kind TypeParameter
  struct SpecializedBase {};
//       ^^^^^^^^^^^^^^^ definition [..] SpecializedBase#
//       kind Struct
  
  template <>
  struct SpecializedBase<int> {};
//       ^^^^^^^^^^^^^^^ reference [..] SpecializedBase#
//       ^^^^^^^^^^^^^^^ definition [..] SpecializedBase#
//       kind Struct
  
  template <typename T>
//                   ^ definition local 3
//                   kind TypeParameter
  struct SpecializedDerived: SpecializedBase<T> {};
//       ^^^^^^^^^^^^^^^^^^ definition [..] SpecializedDerived#
//       kind Struct
//       relation implementation [..] SpecializedBase#
//                           ^^^^^^^^^^^^^^^ reference [..] SpecializedBase#
//                                           ^ reference local 3
  
  struct DerivedFromSpecialization: SpecializedBase<int> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromSpecialization#
//       kind Struct
//       relation implementation [..] SpecializedBase#
//                                  ^^^^^^^^^^^^^^^ reference [..] SpecializedBase#
  
  template <typename T>
//                   ^ definition local 4
//                   kind TypeParameter
  struct CrtpBase { T *t; };
//       ^^^^^^^^ definition [..] CrtpBase#
//       kind Struct
//                  ^ reference local 4
//                     ^ definition [..] CrtpBase#t.
//                     kind Field
  
  struct CrtpDerivedMono: CrtpBase<CrtpDerivedMono> {};
//       ^^^^^^^^^^^^^^^ definition [..] CrtpDerivedMono#
//       kind Struct
//       relation implementation [..] CrtpBase#
//                        ^^^^^^^^ reference [..] CrtpBase#
//                                 ^^^^^^^^^^^^^^^ reference [..] CrtpDerivedMono#
  
  template <typename T>
//                   ^ definition local 5
//                   kind TypeParameter
  struct CrtpDerivedTemplated: CrtpBase<CrtpDerivedTemplated<T>> {};
//       ^^^^^^^^^^^^^^^^^^^^ definition [..] CrtpDerivedTemplated#
//       kind Struct
//       relation implementation [..] CrtpBase#
//                             ^^^^^^^^ reference [..] CrtpBase#
//                                      ^^^^^^^^^^^^^^^^^^^^ reference [..] CrtpDerivedTemplated#
//                                                           ^ reference local 5
  
  template <typename T>
//                   ^ definition local 6
//                   kind TypeParameter
  struct DerivedFromTemplateParam: T {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromTemplateParam#
//       kind Struct
//                                 ^ reference local 6
  
  template <template <typename> typename H>
//                                       ^ definition local 7
//                                       kind TypeParameter
  struct DerivedFromTemplateTemplateParam: H<int> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromTemplateTemplateParam#
//       kind Struct
//                                         ^ reference local 7
  
  template <bool, class T> struct BaseWithOnlySpecializations;
//                      ^ definition local 8
//                      kind TypeParameter
//                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^ reference [..] BaseWithOnlySpecializations#
  
  template <class T>
//                ^ definition local 9
//                kind TypeParameter
  struct BaseWithOnlySpecializations<false, T> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] BaseWithOnlySpecializations#
//       kind Struct
//                                          ^ reference local 9
  
  template <class T>
//                ^ definition local 10
//                kind TypeParameter
  struct DerivedFromBasedWithOnlySpecialization: public BaseWithOnlySpecializations<false, T> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromBasedWithOnlySpecialization#
//       kind Struct
//       relation implementation [..] BaseWithOnlySpecializations#
//                                                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^ reference [..] BaseWithOnlySpecializations#
//                                                                                         ^ reference local 10
  
  template <typename T>
//                   ^ definition local 11
//                   kind TypeParameter
  struct DerivedFromSelf: DerivedFromSelf<T *> {};
//       ^^^^^^^^^^^^^^^ definition [..] DerivedFromSelf#
//       kind Struct
//                        ^^^^^^^^^^^^^^^ reference [..] DerivedFromSelf#
//                                        ^ reference local 11
  
  template <>
  struct DerivedFromSelf<int *> {};
//       ^^^^^^^^^^^^^^^ reference [..] DerivedFromSelf#
//       ^^^^^^^^^^^^^^^ definition [..] DerivedFromSelf#
//       kind Struct
  
  void useDerivedFromSelf() {
//     ^^^^^^^^^^^^^^^^^^ definition [..] useDerivedFromSelf(49f6e7a06ebc5aa8).
//     kind Function
      DerivedFromSelf<int> x;
//    ^^^^^^^^^^^^^^^ reference [..] DerivedFromSelf#
//                         ^ definition local 12
//                         kind Variable
      (void)x;
//          ^ reference local 12
  }
