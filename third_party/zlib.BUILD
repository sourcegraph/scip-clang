package(default_visibility = ["//visibility:public"])

# This file was copied from the Kythe repository

licenses(["notice"])  # BSD/MIT-like license (for zlib)

# From zlib/README
_ZLIB_PUBLIC_HEADERS = [
    "zconf.h",
    "zlib.h",
]

_ZLIB_PREFIXED_HEADERS = ["zlib/include/" + hdr for hdr in _ZLIB_PUBLIC_HEADERS]

# In order to limit the damage from the `includes` propagation
# via `:zlib`, copy the public headers to a subdirectory and
# expose those.
genrule(
    name = "copy_public_headers",
    srcs = _ZLIB_PUBLIC_HEADERS,
    outs = _ZLIB_PREFIXED_HEADERS,
    cmd = "cp $(SRCS) $(@D)/zlib/include/",
    visibility = ["//visibility:private"],
)

cc_library(
    name = "zlib",
    srcs = [
        "adler32.c",
        "compress.c",
        "crc32.c",
        "deflate.c",
        "gzclose.c",
        "gzlib.c",
        "gzread.c",
        "gzwrite.c",
        "infback.c",
        "inffast.c",
        "inflate.c",
        "inftrees.c",
        "trees.c",
        "uncompr.c",
        "zutil.c",
        "crc32.h",
        "deflate.h",
        "gzguts.h",
        "inffast.h",
        "inffixed.h",
        "inflate.h",
        "inftrees.h",
        "trees.h",
        "zutil.h",
        # Include the un-prefixed headers in srcs to work
        # around the fact that zlib uses "" when including itself,
        # but clients generally use <>.
    ] + _ZLIB_PUBLIC_HEADERS,
    hdrs = _ZLIB_PREFIXED_HEADERS,
    copts = [
        "-Wno-unused-variable",
        "-Wno-implicit-function-declaration",
        # Uncomment this flag once we start using Clang 15 for builds.
        # Right now, the latest Xcode (14) uses Apple Clang 14.
        # "-Wno-deprecated-non-prototype",
    ],
    includes = ["zlib/include/"],
)
