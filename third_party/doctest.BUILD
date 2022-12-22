cc_library(
    name = "doctest",
    hdrs = glob(["doctest/**/*.h"]),
    defines = [
        "DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL",
        "DOCTEST_CONFIG_NO_UNPREFIXED_OPTIONS",
    ],
    strip_include_prefix = "doctest",
    visibility = ["//visibility:public"],
)

genrule(
    name = "dummy-main",
    outs = ["dummy-main.cc"],
    cmd = """
    echo '#include "doctest/doctest.h"' > $@
    """,
)

cc_library(
    name = "doctest_main",
    testonly = True,
    srcs = glob(["doctest/**/*.h"]) + ["dummy-main.cc"],
    local_defines = ["DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "doctest_custom_main",
    testonly = True,
    srcs = glob(["doctest/**/*.h"]) + ["dummy-main.cc"],
    # sprintf has been deprecated in newer macOS SDKs, and
    # I was running into puzzling include related issues when upgrading
    # to doctest 2.4.9 (see TODO(ref: update-doctest), so temporarily
    # silence these warnings.
    copts = ["-Wno-deprecated-declarations"],
    local_defines = ["DOCTEST_CONFIG_IMPLEMENT"],
    visibility = ["//visibility:public"],
)
