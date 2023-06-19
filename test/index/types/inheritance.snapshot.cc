  struct MonoBase {};
//^^^^^^ definition [..] `<file>/inheritance.cc`/
//       ^^^^^^^^ definition [..] MonoBase#
  
  struct MonoDerived: MonoBase {};
//       ^^^^^^^^^^^ definition [..] MonoDerived#
//       relation implementation [..] MonoBase#
//                    ^^^^^^^^ reference [..] MonoBase#
  
  template <typename T>
//                   ^ definition local 0
  struct TemplatedBase {};
//       ^^^^^^^^^^^^^ definition [..] TemplatedBase#
  
  template <typename T>
//                   ^ definition local 1
  struct TemplatedDerived: TemplatedBase<T> {};
//       ^^^^^^^^^^^^^^^^ definition [..] TemplatedDerived#
//       relation implementation [..] TemplatedBase#
//                         ^^^^^^^^^^^^^ reference [..] TemplatedBase#
//                                       ^ reference local 1
  
  struct DerivedFromInstantiation: TemplatedBase<int> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromInstantiation#
//       relation implementation [..] TemplatedBase#
//                                 ^^^^^^^^^^^^^ reference [..] TemplatedBase#
  
  template <typename T>
//                   ^ definition local 2
  struct SpecializedBase {};
//       ^^^^^^^^^^^^^^^ definition [..] SpecializedBase#
  
  template <>
  struct SpecializedBase<int> {};
//       ^^^^^^^^^^^^^^^ reference [..] SpecializedBase#
//       ^^^^^^^^^^^^^^^ definition [..] SpecializedBase#
  
  template <typename T>
//                   ^ definition local 3
  struct SpecializedDerived: SpecializedBase<T> {};
//       ^^^^^^^^^^^^^^^^^^ definition [..] SpecializedDerived#
//       relation implementation [..] SpecializedBase#
//                           ^^^^^^^^^^^^^^^ reference [..] SpecializedBase#
//                                           ^ reference local 3
  
  struct DerivedFromSpecialization: SpecializedBase<int> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromSpecialization#
//       relation implementation [..] SpecializedBase#
//                                  ^^^^^^^^^^^^^^^ reference [..] SpecializedBase#
  
  template <typename T>
//                   ^ definition local 4
  struct CrtpBase { T *t; };
//       ^^^^^^^^ definition [..] CrtpBase#
//                  ^ reference local 4
//                     ^ definition [..] CrtpBase#t.
  
  struct CrtpDerivedMono: CrtpBase<CrtpDerivedMono> {};
//       ^^^^^^^^^^^^^^^ definition [..] CrtpDerivedMono#
//       relation implementation [..] CrtpBase#
//                        ^^^^^^^^ reference [..] CrtpBase#
//                                 ^^^^^^^^^^^^^^^ reference [..] CrtpDerivedMono#
  
  template <typename T>
//                   ^ definition local 5
  struct CrtpDerivedTemplated: CrtpBase<CrtpDerivedTemplated<T>> {};
//       ^^^^^^^^^^^^^^^^^^^^ definition [..] CrtpDerivedTemplated#
//       relation implementation [..] CrtpBase#
//                             ^^^^^^^^ reference [..] CrtpBase#
//                                      ^^^^^^^^^^^^^^^^^^^^ reference [..] CrtpDerivedTemplated#
//                                                           ^ reference local 5
  
  template <typename T>
//                   ^ definition local 6
  struct DerivedFromTemplateParam: T {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromTemplateParam#
//                                 ^ reference local 6
  
  template <template <typename> typename H>
//                                       ^ definition local 7
  struct DerivedFromTemplateTemplateParam: H<int> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromTemplateTemplateParam#
//                                         ^ reference local 7
