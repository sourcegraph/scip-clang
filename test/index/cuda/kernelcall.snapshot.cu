  // Initially based off kernel-call.cu in the Clang tests
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/kernelcall.cu`/
//kind File
  // https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/test/SemaCUDA/kernel-call.cu
  
  #include "cuda_stub.h"
//         ^^^^^^^^^^^^^ reference [..] `<file>/cuda_stub.h`/
  
  __global__ void g1(int x) {}
//^^^^^^^^^^ reference [..] `cuda_stub.h:12:9`!
//                ^^ definition [..] g1(d4f767463ce0a6b3).
//                kind Function
//                       ^ definition local 0
//                       kind Parameter
  
  template <typename T> void t1(T arg) {
//                   ^ definition local 1
//                   kind TypeParameter
//                           ^^ definition [..] t1(9b289cee16747614).
//                           kind Function
//                              ^ reference local 1
//                                ^^^ definition local 2
//                                kind Parameter
    g1<<<arg, arg>>>(1);
//  ^^ reference [..] g1(d4f767463ce0a6b3).
//       ^^^ reference local 2
//            ^^^ reference local 2
  }
  
  void h1(int x) {}
//     ^^ definition [..] h1(d4f767463ce0a6b3).
//     kind Function
//            ^ definition local 3
//            kind Parameter
  int h2(int x) { return 1; }
//    ^^ definition [..] h2(7864480464b09eea).
//    kind Function
//           ^ definition local 4
//           kind Parameter
  
  int main(void) {
//    ^^^^ definition [..] main(b126dc7c1de90089).
//    kind Function
    g1<<<1, 1>>>(42);
//  ^^ reference [..] g1(d4f767463ce0a6b3).
//       ^ reference [..] dim3#dim3(6df00707c193238d).
//          ^ reference [..] dim3#dim3(6df00707c193238d).
    g1(42); // expected-error {{call to global function 'g1' not configured}}
//  ^^ reference [..] g1(d4f767463ce0a6b3).
    g1<<<1>>>(42); // expected-error {{too few execution configuration arguments to kernel function call}}
    g1<<<1, 1, 0, 0, 0>>>(42); // expected-error {{too many execution configuration arguments to kernel function call}}
  
    t1(1);
//  ^^ reference [..] t1(9b289cee16747614).
  
    h1<<<1, 1>>>(42); // expected-error {{kernel call to non-global function 'h1'}}
//  ^^ reference [..] h1(d4f767463ce0a6b3).
  
    int (*fp)(int) = h2;
//        ^^ definition local 5
//        kind Variable
//                   ^^ reference [..] h2(7864480464b09eea).
    fp<<<1, 1>>>(42); // expected-error {{must have void return type}}
//  ^^ reference local 5
  
    g1<<<undeclared, 1>>>(42); // expected-error {{use of undeclared identifier 'undeclared'}}
  }
  
  // Make sure we can call static member kernels.
  template <typename > struct a0 {
//                            ^^ definition [..] a0#
//                            kind Struct
    template <typename T> static __global__ void Call(T);
//                     ^ definition local 6
//                     kind TypeParameter
//                               ^^^^^^^^^^ reference [..] `cuda_stub.h:12:9`!
//                                               ^^^^ reference [..] a0#Call(b07662a27bd562f9).
//                                                    ^ reference local 6
  };
  struct a1 {
//       ^^ definition [..] a1#
//       kind Struct
    template <typename T> static __global__ void Call(T);
//                     ^ definition local 7
//                     kind TypeParameter
//                               ^^^^^^^^^^ reference [..] `cuda_stub.h:12:9`!
//                                               ^^^^ reference [..] a1#Call(9b289cee16747614).
//                                                    ^ reference local 7
  };
  template <typename T> struct a2 {
//                   ^ definition local 8
//                   kind TypeParameter
//                             ^^ definition [..] a2#
//                             kind Struct
    static __global__ void Call(T);
//         ^^^^^^^^^^ reference [..] `cuda_stub.h:12:9`!
//                         ^^^^ reference [..] a2#Call(9b289cee16747614).
//                              ^ reference local 8
  };
  struct a3 {
//       ^^ definition [..] a3#
//       kind Struct
    static __global__ void Call(int);
//         ^^^^^^^^^^ reference [..] `cuda_stub.h:12:9`!
//                         ^^^^ reference [..] a3#Call(d4f767463ce0a6b3).
    static __global__ void Call(void*);
//         ^^^^^^^^^^ reference [..] `cuda_stub.h:12:9`!
//                         ^^^^ reference [..] a3#Call(5d22bdacc48458e8).
  };
  
  struct b {
//       ^ definition [..] b#
//       kind Struct
    template <typename c> void d0(c arg) {
//                     ^ definition local 9
//                     kind TypeParameter
//                             ^^ definition [..] b#d0(9b289cee16747614).
//                             kind Method
//                                ^ reference local 9
//                                  ^^^ definition local 10
//                                  kind Parameter
      a0<c>::Call<<<0, 0>>>(arg);
//    ^^ reference [..] a0#
//                  ^ reference [..] dim3#dim3(6df00707c193238d).
//                     ^ reference [..] dim3#dim3(6df00707c193238d).
//                          ^^^ reference local 10
      a1::Call<<<0,0>>>(arg);
//    ^^ reference [..] a1#
//        ^^^^ reference [..] a1#Call(9b289cee16747614).
//               ^ reference [..] dim3#dim3(6df00707c193238d).
//                 ^ reference [..] dim3#dim3(6df00707c193238d).
//                      ^^^ reference local 10
      a2<c>::Call<<<0,0>>>(arg);
//    ^^ reference [..] a2#
//                  ^ reference [..] dim3#dim3(6df00707c193238d).
//                    ^ reference [..] dim3#dim3(6df00707c193238d).
//                         ^^^ reference local 10
      a3::Call<<<0, 0>>>(arg);
//    ^^ reference [..] a3#
//        ^^^^ reference [..] a3#Call(5d22bdacc48458e8).
//        ^^^^ reference [..] a3#Call(d4f767463ce0a6b3).
//               ^ reference [..] dim3#dim3(6df00707c193238d).
//                  ^ reference [..] dim3#dim3(6df00707c193238d).
//                       ^^^ reference local 10
    }
    void d1(void* arg) {
//       ^^ definition [..] b#d1(5d22bdacc48458e8).
//       kind Method
//                ^^^ definition local 11
//                kind Parameter
      a0<void*>::Call<<<0, 0>>>(arg);
//    ^^ reference [..] a0#
//               ^^^^ reference [..] a0#Call(9b289cee16747614).
//                      ^ reference [..] dim3#dim3(6df00707c193238d).
//                         ^ reference [..] dim3#dim3(6df00707c193238d).
//                              ^^^ reference local 11
      a1::Call<<<0,0>>>(arg);
//    ^^ reference [..] a1#
//        ^^^^ reference [..] a1#Call(9b289cee16747614).
//               ^ reference [..] dim3#dim3(6df00707c193238d).
//                 ^ reference [..] dim3#dim3(6df00707c193238d).
//                      ^^^ reference local 11
      a2<void*>::Call<<<0,0>>>(arg);
//    ^^ reference [..] a2#
//               ^^^^ reference [..] a2#Call(9b289cee16747614).
//                      ^ reference [..] dim3#dim3(6df00707c193238d).
//                        ^ reference [..] dim3#dim3(6df00707c193238d).
//                             ^^^ reference local 11
      a3::Call<<<0, 0>>>(arg);
//    ^^ reference [..] a3#
//        ^^^^ reference [..] a3#Call(5d22bdacc48458e8).
//               ^ reference [..] dim3#dim3(6df00707c193238d).
//                  ^ reference [..] dim3#dim3(6df00707c193238d).
//                       ^^^ reference local 11
    }
    void e() { d0(1); }
//       ^ definition [..] b#e(49f6e7a06ebc5aa8).
//       kind Method
//             ^^ reference [..] b#d0(9b289cee16747614).
  };
  
  namespace x {
//          ^ definition [..] x/
//          kind Namespace
    namespace y {
//            ^ definition [..] x/y/
//            kind Namespace
      template <typename DType, int layout>
//                       ^^^^^ definition local 12
//                       kind TypeParameter
//                                  ^^^^^^ definition local 13
//                                  kind Parameter
      __global__ void mykernel(const int nthreads, const DType *in_data, DType *out_data) {}
//    ^^^^^^^^^^ reference [..] `cuda_stub.h:12:9`!
//                    ^^^^^^^^ definition [..] x/y/mykernel(36fc24b3817d5bcc).
//                    kind Function
//                                       ^^^^^^^^ definition local 14
//                                       kind Parameter
//                                                       ^^^^^ reference local 12
//                                                              ^^^^^^^ definition local 15
//                                                              kind Parameter
//                                                                       ^^^^^ reference local 12
//                                                                              ^^^^^^^^ definition local 16
//                                                                              kind Parameter
    }
  }
  
  template <typename DType, int layout>
//                   ^^^^^ definition local 17
//                   kind TypeParameter
//                              ^^^^^^ definition local 18
//                              kind Parameter
  void call_mykernel2() {
//     ^^^^^^^^^^^^^^ definition [..] call_mykernel2(49f6e7a06ebc5aa8).
//     kind Function
    x::y::mykernel<DType, layout><<<0, 0>>>(0, nullptr, nullptr);
//  ^ reference [..] x/
//     ^ reference [..] x/y/
//        ^^^^^^^^ reference [..] x/y/mykernel(36fc24b3817d5bcc).
//                 ^^^^^ reference local 17
//                        ^^^^^^ reference local 18
//                                  ^ reference [..] dim3#dim3(6df00707c193238d).
//                                     ^ reference [..] dim3#dim3(6df00707c193238d).
  }
