  // Test for member function templates with enable_if in non-template classes,
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/enable_if_templates.cc`/
//kind File
  // and free function templates with enable_if. These patterns were previously
  // broken because getFunctionDisambiguator didn't handle the case where
  // getInstantiatedFromMemberTemplate() returns null for such templates.
  
  namespace std {
//          ^^^ definition [..] std/
//          kind Namespace
  template <bool B, class T = void>
//               ^ definition local 0
//               kind Parameter
//                        ^ definition local 1
//                        kind TypeParameter
  struct enable_if {};
//       ^^^^^^^^^ definition [..] std/enable_if#
//       kind Struct
  
  template <class T>
//                ^ definition local 2
//                kind TypeParameter
  struct enable_if<true, T> {
//       ^^^^^^^^^ definition [..] std/enable_if#
//       kind Struct
//                       ^ reference local 2
    using type = T;
//        ^^^^ definition [..] std/enable_if#type#
//        kind TypeAlias
//               ^ reference local 2
  };
  
  template <class T>
//                ^ definition local 3
//                kind TypeParameter
  struct is_integral {
//       ^^^^^^^^^^^ definition [..] std/is_integral#
//       kind Struct
    static constexpr bool value = false;
//                        ^^^^^ definition [..] std/is_integral#value.
//                        kind StaticDataMember
  };
  
  template <>
  struct is_integral<int> {
//       ^^^^^^^^^^^ reference [..] std/is_integral#
//       ^^^^^^^^^^^ definition [..] std/is_integral#
//       kind Struct
    static constexpr bool value = true;
//                        ^^^^^ definition [..] std/is_integral#value.
//                        kind StaticDataMember
  };
  
  template <class T>
//                ^ definition local 4
//                kind TypeParameter
  struct is_enum {
//       ^^^^^^^ definition [..] std/is_enum#
//       kind Struct
    static constexpr bool value = false;
//                        ^^^^^ definition [..] std/is_enum#value.
//                        kind StaticDataMember
  };
  } // namespace std
  
  // Issue 1: Member function template with enable_if in a non-template class
  class Widget {
//      ^^^^^^ definition [..] Widget#
//      kind Class
  public:
    template <typename T,
//                     ^ definition local 5
//                     kind TypeParameter
              typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
//                     ^^^ reference [..] std/
//                          ^^^^^^^^^ reference [..] std/enable_if#
    void process(T value) {
//       ^^^^^^^ definition [..] Widget#process(9b289cee16747614).
//       kind Method
//               ^ reference local 5
//                 ^^^^^ definition local 6
//                 kind Parameter
      (void)value;
//          ^^^^^ reference local 6
    }
  };
  
  // Issue 2: Free function template with enable_if
  template <typename T,
//                   ^ definition local 7
//                   kind TypeParameter
            typename std::enable_if<!std::is_enum<T>::value, bool>::type = false>
//                   ^^^ reference [..] std/
//                        ^^^^^^^^^ reference [..] std/enable_if#
  T convert(int x) {
//^ reference local 7
//  ^^^^^^^ definition [..] convert(767fea59dce4185d).
//  kind Function
//              ^ definition local 8
//              kind Parameter
    return static_cast<T>(x);
//                     ^ reference local 7
//                        ^ reference local 8
  }
  
  // Issue 3: Member call through a pointer wrapper (simplified unique_ptr)
  template <typename T>
//                   ^ definition local 9
//                   kind TypeParameter
  class Ptr {
//      ^^^ definition [..] Ptr#
//      kind Class
    T *p;
//  ^ reference local 9
//     ^ definition [..] Ptr#p.
//     kind Field
  
  public:
    Ptr(T *ptr) : p(ptr) {}
//  ^^^ definition [..] Ptr#`Ptr<T>`(ebd0a1552f8ce24f).
//  kind Constructor
//      ^ reference local 9
//         ^^^ definition local 10
//         kind Parameter
//                ^ reference [..] Ptr#p.
//                  ^^^ reference local 10
    T *operator->() const { return p; }
//  ^ reference local 9
//     ^^^^^^^^ definition [..] Ptr#`operator->`(5a2a78a048fb49a8).
//     kind Method
//                                 ^ reference [..] Ptr#p.
  };
  
  class ThreadLoop {
//      ^^^^^^^^^^ definition [..] ThreadLoop#
//      kind Class
  public:
    bool isHealthy() const { return true; }
//       ^^^^^^^^^ definition [..] ThreadLoop#isHealthy(50ce9a9e25b4a850).
//       kind Method
  };
  
  class ThreadPool {
//      ^^^^^^^^^^ definition [..] ThreadPool#
//      kind Class
    Ptr<ThreadLoop> mThread;
//  ^^^ reference [..] Ptr#
//      ^^^^^^^^^^ reference [..] ThreadLoop#
//                  ^^^^^^^ definition [..] ThreadPool#mThread.
//                  kind Field
  
  public:
    ThreadPool() : mThread(new ThreadLoop()) {}
//  ^^^^^^^^^^ definition [..] ThreadPool#ThreadPool(49f6e7a06ebc5aa8).
//  kind Constructor
//                 ^^^^^^^ reference [..] ThreadPool#mThread.
//                 ^^^^^^^ reference [..] Ptr#Ptr(ebd0a1552f8ce24f).
//                             ^^^^^^^^^^ reference [..] ThreadLoop#
  
    bool checkHealth() const {
//       ^^^^^^^^^^^ definition [..] ThreadPool#checkHealth(50ce9a9e25b4a850).
//       kind Method
      return mThread->isHealthy();
//           ^^^^^^^ reference [..] ThreadPool#mThread.
//                  ^^ reference [..] Ptr#`operator->`(5a2a78a048fb49a8).
//                    ^^^^^^^^^ reference [..] ThreadLoop#isHealthy(50ce9a9e25b4a850).
    }
  };
  
  void test() {
//     ^^^^ definition [..] test(49f6e7a06ebc5aa8).
//     kind Function
    Widget w;
//  ^^^^^^ reference [..] Widget#
//         ^ definition local 11
//         kind Variable
    w.process(42);
//  ^ reference local 11
//    ^^^^^^^ reference [..] Widget#process(9b289cee16747614).
    w.process<int>(100);
//  ^ reference local 11
//    ^^^^^^^ reference [..] Widget#process(9b289cee16747614).
  
    auto val = convert<int>(5);
//       ^^^ definition local 12
//       kind Variable
//             ^^^^^^^ reference [..] convert(767fea59dce4185d).
    (void)val;
//        ^^^ reference local 12
  
    ThreadPool pool;
//  ^^^^^^^^^^ reference [..] ThreadPool#
//             ^^^^ definition local 13
//             kind Variable
//             ^^^^ reference [..] ThreadPool#ThreadPool(49f6e7a06ebc5aa8).
    (void)pool.checkHealth();
//        ^^^^ reference local 13
//             ^^^^^^^^^^^ reference [..] ThreadPool#checkHealth(50ce9a9e25b4a850).
  }
