  // extra-args: -std=c++17
//^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/inheritance.cc`/
  
  struct MonoBase {};
//       ^^^^^^^^ definition [..] MonoBase#
  
  struct MonoDerived: MonoBase {};
//       ^^^^^^^^^^^ definition [..] MonoDerived#
//       relation implementation [..] MonoBase#
//                    ^^^^^^^^ reference [..] MonoBase#
  
  struct MonoDerivedTwice: MonoDerived {};
//       ^^^^^^^^^^^^^^^^ definition [..] MonoDerivedTwice#
//       relation implementation [..] MonoBase#
//       relation implementation [..] MonoDerived#
//                         ^^^^^^^^^^^ reference [..] MonoDerived#
  
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
  
  template <bool, class T> struct BaseWithOnlySpecializations;
//                      ^ definition local 8
//                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^ reference [..] BaseWithOnlySpecializations#
  
  template <class T>
//                ^ definition local 9
  struct BaseWithOnlySpecializations<false, T> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] BaseWithOnlySpecializations#
//                                          ^ reference local 9
  
  template <class T>
//                ^ definition local 10
  struct DerivedFromBasedWithOnlySpecialization: public BaseWithOnlySpecializations<false, T> {};
//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] DerivedFromBasedWithOnlySpecialization#
//       relation implementation [..] BaseWithOnlySpecializations#
//                                                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^ reference [..] BaseWithOnlySpecializations#
//                                                                                         ^ reference local 10
  
  template <typename T>
//                   ^ definition local 11
  struct DerivedFromSelf: DerivedFromSelf<T *> {};
//       ^^^^^^^^^^^^^^^ definition [..] DerivedFromSelf#
//                        ^^^^^^^^^^^^^^^ reference [..] DerivedFromSelf#
//                                        ^ reference local 11
  
  template <>
  struct DerivedFromSelf<int *> {};
//       ^^^^^^^^^^^^^^^ reference [..] DerivedFromSelf#
//       ^^^^^^^^^^^^^^^ definition [..] DerivedFromSelf#
  
  void useDerivedFromSelf() {
//     ^^^^^^^^^^^^^^^^^^ definition [..] useDerivedFromSelf(49f6e7a06ebc5aa8).
      DerivedFromSelf<int> x;
//    ^^^^^^^^^^^^^^^ reference [..] DerivedFromSelf#
//                         ^ definition local 12
      (void)x;
//          ^ reference local 12
  }
