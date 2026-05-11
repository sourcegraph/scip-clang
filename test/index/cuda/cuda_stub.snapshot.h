  // Based off https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/test/SemaCUDA/Inputs/cuda.h
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ definition [..] `<file>/cuda_stub.h`/
//kind File
  // 
  // Add common stuff for CUDA headers here.
  
  using size_t = unsigned long long;
//      ^^^^^^ definition [..] size_t#
//      kind TypeAlias
  
  // Make this file work with nvcc, for testing compatibility.
  
  #ifndef __NVCC__
  #define __constant__ __attribute__((constant))
//        ^^^^^^^^^^^^ definition [..] `cuda_stub.h:10:9`!
//        kind Macro
  #define __device__ __attribute__((device))
//        ^^^^^^^^^^ definition [..] `cuda_stub.h:11:9`!
//        kind Macro
  #define __global__ __attribute__((global))
//        ^^^^^^^^^^ definition [..] `cuda_stub.h:12:9`!
//        kind Macro
  #define __host__ __attribute__((host))
//        ^^^^^^^^ definition [..] `cuda_stub.h:13:9`!
//        kind Macro
  #define __shared__ __attribute__((shared))
//        ^^^^^^^^^^ definition [..] `cuda_stub.h:14:9`!
//        kind Macro
  #define __managed__ __attribute__((managed))
//        ^^^^^^^^^^^ definition [..] `cuda_stub.h:15:9`!
//        kind Macro
  #define __launch_bounds__(...) __attribute__((launch_bounds(__VA_ARGS__)))
//        ^^^^^^^^^^^^^^^^^ definition [..] `cuda_stub.h:16:9`!
//        kind Macro
  
  struct dim3 {
//       ^^^^ definition [..] dim3#
//       kind Struct
    unsigned x, y, z;
//           ^ definition [..] dim3#x.
//           kind Field
//              ^ definition [..] dim3#y.
//              kind Field
//                 ^ definition [..] dim3#z.
//                 kind Field
    __host__ __device__ dim3(unsigned x, unsigned y = 1, unsigned z = 1) : x(x), y(y), z(z) {}
//  ^^^^^^^^ reference [..] `cuda_stub.h:13:9`!
//           ^^^^^^^^^^ reference [..] `cuda_stub.h:11:9`!
//                      ^^^^ definition [..] dim3#dim3(6df00707c193238d).
//                      kind Constructor
//                                    ^ definition local 0
//                                    kind Parameter
//                                                ^ definition local 1
//                                                kind Parameter
//                                                                ^ definition local 2
//                                                                kind Parameter
//                                                                         ^ reference [..] dim3#x.
//                                                                           ^ reference local 0
//                                                                               ^ reference [..] dim3#y.
//                                                                                 ^ reference local 1
//                                                                                     ^ reference [..] dim3#z.
//                                                                                       ^ reference local 2
  };
  
  #ifdef __HIP__
  typedef struct hipStream *hipStream_t;
  typedef enum hipError {} hipError_t;
  int hipConfigureCall(dim3 gridSize, dim3 blockSize, size_t sharedSize = 0,
                       hipStream_t stream = 0);
  extern "C" hipError_t __hipPushCallConfiguration(dim3 gridSize, dim3 blockSize,
                                                   size_t sharedSize = 0,
                                                   hipStream_t stream = 0);
  extern "C" hipError_t hipLaunchKernel(const void *func, dim3 gridDim,
                                        dim3 blockDim, void **args,
                                        size_t sharedMem,
                                        hipStream_t stream);
  #else
  typedef struct cudaStream *cudaStream_t;
//               ^^^^^^^^^^ reference [..] cudaStream#
//               ^^^^^^^^^^ reference [..] cudaStream#
//                           ^^^^^^^^^^^^ definition [..] cudaStream_t#
//                           kind TypeAlias
  typedef enum cudaError {} cudaError_t;
//             ^^^^^^^^^ definition [..] cudaError#
//             kind Enum
//                          ^^^^^^^^^^^ definition [..] cudaError_t#
//                          kind TypeAlias
  
  extern "C" int cudaConfigureCall(dim3 gridSize, dim3 blockSize,
//                                 ^^^^ reference [..] dim3#
//                                      ^^^^^^^^ definition local 3
//                                      kind Parameter
//                                                ^^^^ reference [..] dim3#
//                                                     ^^^^^^^^^ definition local 4
//                                                     kind Parameter
                                   size_t sharedSize = 0,
//                                 ^^^^^^ reference [..] size_t#
//                                        ^^^^^^^^^^ definition local 5
//                                        kind Parameter
                                   cudaStream_t stream = 0);
//                                 ^^^^^^^^^^^^ reference [..] cudaStream_t#
//                                              ^^^^^^ definition local 6
//                                              kind Parameter
  extern "C" int __cudaPushCallConfiguration(dim3 gridSize, dim3 blockSize,
//                                           ^^^^ reference [..] dim3#
//                                                ^^^^^^^^ definition local 7
//                                                kind Parameter
//                                                          ^^^^ reference [..] dim3#
//                                                               ^^^^^^^^^ definition local 8
//                                                               kind Parameter
                                             size_t sharedSize = 0,
//                                           ^^^^^^ reference [..] size_t#
//                                                  ^^^^^^^^^^ definition local 9
//                                                  kind Parameter
                                             cudaStream_t stream = 0);
//                                           ^^^^^^^^^^^^ reference [..] cudaStream_t#
//                                                        ^^^^^^ definition local 10
//                                                        kind Parameter
  extern "C" cudaError_t cudaLaunchKernel(const void *func, dim3 gridDim,
//           ^^^^^^^^^^^ reference [..] cudaError_t#
//                                                    ^^^^ definition local 11
//                                                    kind Parameter
//                                                          ^^^^ reference [..] dim3#
//                                                               ^^^^^^^ definition local 12
//                                                               kind Parameter
                                          dim3 blockDim, void **args,
//                                        ^^^^ reference [..] dim3#
//                                             ^^^^^^^^ definition local 13
//                                             kind Parameter
//                                                              ^^^^ definition local 14
//                                                              kind Parameter
                                          size_t sharedMem, cudaStream_t stream);
//                                        ^^^^^^ reference [..] size_t#
//                                               ^^^^^^^^^ definition local 15
//                                               kind Parameter
//                                                          ^^^^^^^^^^^^ reference [..] cudaStream_t#
//                                                                       ^^^^^^ definition local 16
//                                                                       kind Parameter
  #endif
  
  // Host- and device-side placement new overloads.
  void *operator new(size_t, void *p) { return p; }
//      ^^^^^^^^ definition [..] `operator new`(ecd71fefd6822377).
//      kind Function
//                   ^^^^^^ reference [..] size_t#
//                                 ^ definition local 17
//                                 kind Parameter
//                                             ^ reference local 17
  void *operator new[](size_t, void *p) { return p; }
//      ^^^^^^^^ definition [..] `operator new[]`(ecd71fefd6822377).
//      kind Function
//                     ^^^^^^ reference [..] size_t#
//                                   ^ definition local 18
//                                   kind Parameter
//                                               ^ reference local 18
  __device__ void *operator new(size_t, void *p) { return p; }
//^^^^^^^^^^ reference [..] `cuda_stub.h:11:9`!
//                 ^^^^^^^^ definition [..] `operator new`(ecd71fefd6822377).
//                 kind Function
//                              ^^^^^^ reference [..] size_t#
//                                            ^ definition local 19
//                                            kind Parameter
//                                                        ^ reference local 19
  __device__ void *operator new[](size_t, void *p) { return p; }
//^^^^^^^^^^ reference [..] `cuda_stub.h:11:9`!
//                 ^^^^^^^^ definition [..] `operator new[]`(ecd71fefd6822377).
//                 kind Function
//                                ^^^^^^ reference [..] size_t#
//                                              ^ definition local 20
//                                              kind Parameter
//                                                          ^ reference local 20
  
  #endif // !__NVCC__
  
