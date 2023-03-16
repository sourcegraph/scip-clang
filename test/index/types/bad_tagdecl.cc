// Minimized from
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
struct enable_if {};
 
template<class T>
struct enable_if<true, T> { typedef T type; };

template< bool B, class T = void >
using enable_if_t = typename enable_if<B,T>::type;

template <typename T, typename Enable = void> struct MyTemplate { };

template <class T>
struct ShouldEnable { static bool const value = false; };

struct MyTemplate<Undeclared, enable_if_t<ShouldEnable<int8_t>::value>> { };
