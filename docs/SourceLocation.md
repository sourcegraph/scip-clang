# Source locations in Clang

NOTE: This document is not meant to be an authoritative or complete.
Please refer to the original source code in Clang for more details:
in particular,
[SourceLocation.h](https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/include/clang/Basic/SourceLocation.h),
[SourceLocation.cpp](https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/lib/Basic/SourceLocation.cpp),
[SourceManager.h](https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/include/clang/Basic/SourceManager.h?subtree=true),
and [SourceManager.cpp](https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang/lib/Basic/SourceManager.cpp).
This document is meant to be read along-side the source code.

## Introduction

One of the tricky things about Clang is how it handles source locations.
This complexity partly comes down to the need for handling
macros and line directives, which can lead to a single token
having multiple different meaningful source locations.

The main types related to source locations and ranges are:
- [`SourceLocation`](#sourcelocation)
- [`SLocEntry`](#slocentry)
- [`FileID`](#fileid)
- [`PresumedLoc`](#presumedloc)
- [`SourceRange`](#sourcerange)
- [`CharSourceRange`](#charsourcerange)

Additionally, `SourceManager` is a central move-only container holding all
buffers and having access to mappings between these types.

## SourceLocation

Consider this code:

```cpp
#define NS(_n) namespace _n { }

NS(abc)
```

Here `abc` has two associated source ranges.
One is the range inside the macro body
where `abc` gets inserted (the range for `_n` on line 1).
The other is the range where `abc` is written in the code directly (on line 3).
(Technically, there is also a third range,
which is the source range of `abc` in the pre-processed output,
but let's ignore that.)

After pre-processing, this code becomes:

```cpp


namespace abc { }
```

Semantic analysis happens after pre-processing.
In this case, the `NamespaceDecl` for `abc` will have an associated
`SourceLocation` that looks like:

```
loc: foo.cc:3:1 (MacroID)
  -> sourceManager.getSpellingLoc(loc):  foo:1:26 (FileID)
  -> sourceManager.getExpansionLoc(loc): foo:3:1  (FileID)
```

Few points worth noting here:
- The `MacroID` bit (from `SourceLocation::isMacroID()`) indicates that
  the namespace's location was expanded from a macro. A `SourceLocation`
  is in one of 3 states: `isInvalid()`, `isFileID()` or `isMacroID()`.
  **WARNING:** Technically, `isInvalid()` source locations also return
  `isFileID()`, so one needs to be careful if only checking `isFileID()`.
- The column number in the original source location
  and the expansion location both point to macro invocation site,
  and NOT to the actual `abc` argument of the macro.
- The spelling location points inside the macro body.

### Caution: `SourceLocation`s pointing outside the source

`SourceLocation` may sometimes represent locations
not present in the source code, such as: (non-exhaustive)
- Macro definitions on the command-line `-DICE_CREAM_FLAVOR=STRAWBERRY`.
- Definitions in the preamble header implicitly inserted by the compiler (`<built-in>`).

See the various `SourceManager::isWrittenIn*` methods and
`clangd::isSpelledInSource` for more details.

### Caution: Spelling locations may not always be well-defined

Since the preprocessor allows combining multiple tokens into one using `##`,
it is possible that a token may not have a spelling location.

```
#define VISIT(_name) void Visit##_name##Decl(_name##Decl *) const {}
VISIT(Enum)
```

Here, the `VisitEnumDecl` method will not have a spelling location.

## SLocEntry

This type holds the main pieces of information needed about
files and macro expansions.

The `SourceManager` maintains a mapping `FileID -> SLocEntry`
for valid `FileID`s
([tables](https://sourcegraph.com/github.com/llvm/llvm-project@471c0e000af7f2534c84fac9beb0973e4b1c7a62/-/blob/clang/include/clang/Basic/SourceManager.h?L692-702),
[accessor](https://sourcegraph.com/github.com/llvm/llvm-project@471c0e000af7f2534c84fac9beb0973e4b1c7a62/-/blob/clang/include/clang/Basic/SourceManager.h?L1722-1729)).

## FileID

Contrary to what the name suggests, this type represents an ID
for arbitrary memory buffers.
It's probably best to mentally rename it to `SLocEntryID`,
since the main purpose of this type
is to be an ID for [`SLocEntry`](#slocentry) values,
which contain more information.

This also means that, if a single header is included multiple times,
regardless of whether it expands the same way or not, there will
be two different `FileID` values for it, not one.

- A valid `FileID` always has a corresponding `SLocEntry`.
- Since a `FileID` may not actually represent a source file,
  it is possible that `sourceManager.getFileEntryForID`
  returns null for a valid `FileID`.

### Aside: Connection between `SourceLocation::isFileID()` and `FileID`

```cpp
SourceLocation loc = ...;
if (loc.isValid()) {
  auto fileId = sourceManager.getFileID(loc);
  assert(fileId.isValid());
  if (loc.isFileID()) {
    // The corresponding SLocEntry carries a FileInfo
    assert(sourceManager.getSLocEntry(fileId).isFile());
  } else {
    assert(loc.isMacroID());
    // The corresponding SLocEntry carries an ExpansionInfo
    assert(sourceManager.getSLocEntry(fileId).isExpansion())
  }
}
```

## PresumedLoc

Takes `#line` directives into account, and hence generally
the right location to use for diagnostics.

When working with macro expansions, the presumed location
takes into account any applicable `#line` directives
at the point where the macro is expanded (i.e. at the expansion location),.
not inside the body of the macro definition (i.e. the spelling location).

This means that the following code sequence generally doesn't make sense:

```cpp
sourceManager.getPresumedLoc(sourceManager.getSpellingLoc(loc)); // ❌
// Instead, use one of
//   sourceManager.getPresumedLoc(loc)
//   sourceManager.getSpellingLoc(loc)
//   sourceManager.getExpansionLoc(loc)
// depending on the use case.
```

The following identity holds:

```cpp
sourceManager.getPresumedLoc(loc) == sourceManager.getPresumedLoc(sourceManager.getExpansionLoc(loc))
```

### Caution: Avoid `PresumedLoc::getFileID()`

In general, the following does not hold:

```cpp
// Note: getPresumedLoc has an optional UseLineDirectives = true parameter
sourceManager.getFileID(loc) == sourceManager.getPresumedLoc(loc).getFileID() // ❌
```

For example, when the following pragma is present (such as in `cstddef`):

```
#pragma GCC system_header
```

The preprocessor generates a fake `#line` directive
([source](https://sourcegraph.com/github.com/llvm/llvm-project@471c0e000af7f2534c84fac9beb0973e4b1c7a62/-/blob/clang/lib/Lex/Pragma.cpp?L496-501))
on seeing this pragma.
In `getPresumedLoc`, when a line directive is detected,
the `FileID` is marked invalid (instead of using the system header's `FileID`)
([source](https://sourcegraph.com/github.com/llvm/llvm-project@471c0e0/-/blob/clang/lib/Basic/SourceManager.cpp?L1560-1568)).

On the other hand, `sourceManager.getFileID(loc)` returns the true `FileID`
for the system header.

### SourceRange

TODO

### CharSourceRange

TODO
