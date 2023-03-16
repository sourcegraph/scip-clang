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
