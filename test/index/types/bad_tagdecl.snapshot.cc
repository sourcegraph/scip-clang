  // Minimized from
//^^^^^^^^^^^^^^^^^ definition [..] `<file>/bad_tagdecl.cc`/
  // https://sourcegraph.com/github.com/llvm/llvm-project@08b9835072c0b2c50cf3be9d6182bc89f64ae51d/-/blob/llvm/include/llvm/Support/YAMLTraits.h?L1283-1285
  // when indexing llvm/lib/Support/AMDGPUMetadata.cpp
  //
  // The code is well-formed, but I haven't been able to create
  // a reduced test case with well-formed code which leads
  // to an unusual TagDecl being created. That's why this test
  // cases uses ill-formed code to trigger the same.
  //
  // See NOTE(def: missing-definition-for-tagdecl)
  
  template<bool B, class T = void>
//              ^ definition local 0
//                       ^ definition local 1
  struct enable_if {};
//       ^^^^^^^^^ definition [..] enable_if#
   
  template<class T>
//               ^ definition local 2
  struct enable_if<true, T> { typedef T type; };
//       ^^^^^^^^^ definition [..] enable_if#
//                       ^ reference local 2
//                                    ^ reference local 2
//                                      ^^^^ definition [..] enable_if#type#
  
  template< bool B, class T = void >
//               ^ definition local 3
//                        ^ definition local 4
  using enable_if_t = typename enable_if<B,T>::type;
//      ^^^^^^^^^^^ definition [..] enable_if_t#
//                             ^^^^^^^^^ reference [..] enable_if#
  
  template <typename T, typename Enable = void> struct MyTemplate { };
//                   ^ definition local 5
//                               ^^^^^^ definition local 6
//                                                     ^^^^^^^^^^ definition [..] MyTemplate#
  
  template <class T>
//                ^ definition local 7
  struct ShouldEnable { static bool const value = false; };
//       ^^^^^^^^^^^^ definition [..] ShouldEnable#
//                                        ^^^^^ definition [..] ShouldEnable#value.
  
  struct MyTemplate<Undeclared, enable_if_t<ShouldEnable<int8_t>::value>> { };
