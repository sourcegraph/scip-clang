# Keep LLVM versions list in sync with setup_llvm.bzl
ASAN_LINKOPTS = [
    "-Wl,-rpath,@loader_path/../../../../../../external/llvm_toolchain_llvm/lib/clang/15.0.6/lib/darwin",
    # macOS release builds use 15.0.7 for x86_64
    "-Wl,-rpath,@loader_path/../../../../../../external/llvm_toolchain_llvm/lib/clang/15.0.7/lib/darwin",
    "-Wl,-rpath,@loader_path/../../../../../../external/llvm_toolchain_llvm/lib/clang/15.0.6/lib/linux",
]
