# scip-clang design notes

- [Architecture](#architecture)
  - [Handling slow, hung or crashed workers](#handling-slow-hung-or-crashed-workers)
  - [Disk I/O](#disk-io)
  - [Bazel and distributed builds](#bazel-and-distributed-builds)
- [Reducing work across headers](#reducing-work-across-headers)
  - [Checking well-behavedness of headers](#checking-well-behavedness-of-headers)
- [Indexing templates](#indexing-templates)
- [Mapping C++ to SCIP](#mapping-c-to-scip)
  - [Symbol names for macros](#symbol-names-for-macros)
  - [Symbol names for declarations](#symbol-names-for-declarations)
    - [Symbol names for enum cases](#symbol-names-for-enum-cases)
  - [Method disambiguator](#method-disambiguator)
  - [Forward declarations](#forward-declarations)
- [Cross-repo navigation](#cross-repo-navigation)
  - [Namespace declarations in a cross-repo setting](#namespace-declarations-in-a-cross-repo-setting)
  - [Forward declarations in a cross-repo setting](#forward-declarations-in-a-cross-repo-setting)

## Architecture

When working on a compilation database (a `compile_commands.json` file),
the indexer will consist of 1 driver process
and 1 or more worker processes.

We use separate processes for robustness in the face
of crashes and/or hangs.

The driver and workers communicate using two kinds of queues:
1. A shared result queue: Used by workers to send messages to the driver.
2. Per-worker task queues: Used by the driver to send messages to a worker,
   including task descriptions and control signals (e.g. shutdown).

Load-balancing is done in the driver,
instead of via a shared work queue,
as using per-worker task queues
simplifies assigning tasks to a specific worker.
This allows multi-step jobs where
a worker completes one step of a job,
communicates with the driver,
and receives further instructions
on how to continue the job.

Workers may optionally cache data across TUs for performance.

### Handling slow, hung or crashed workers

The driver does some extra book-keeping
on when it last heard from a worker.
If a worker hasn't responded in very long,
the driver terminates the worker.
After that the driver will start a new worker.
This avoids a situation where one or more workers are hung,
and the overall parallelism is reduced
by a significant amount for an extended period of time.

### Disk I/O

Workers write out shards (incomplete SCIP indexes) based on paths
provided by the driver after completing an indexing job.
This write is done after each job is complete,
instead of after completing all jobs,
because it reduces RSS as well as the blast radius
in case the worker gets terminated or crashes later.

After all indexing work is completed, the driver
assembles the shards into a full SCIP index.

### Bazel and distributed builds

In the case of a distributed builds with Bazel,
we could use [Aspects](https://bazel.build/extending/aspects)
to index C++ code. Using aspects, compilation
jobs would be mapped to indexing jobs
and then flattened, with a final merging step.
(A compilation can have multiple link jobs,
but we probably don't want multiple merge steps
because they would do redundant work.
"Flattening" the job DAG would get rid of
intermediate link jobs.)

Each job could output some shards,
along with hashes (for faster de-duplication
during the merge step).
The final merge step would take these
hashes and shards
and assemble them into an Index.

NOTE: As of Jan 24 2023, this functionality is planned
for [post-MVP](https://github.com/sourcegraph/scip-clang/issues/26).

## Reducing work across headers

There are broadly two different approaches to de-duplicating
index information across headers:
1. Do it with a mix of ahead-of-time and on-the-fly planning
   while indexing: This approach involves assigning the indexing
   work for a header to a specific worker.
   Semantic analysis still needs to be done repeatedly for
   headers to actually get name lookup/types to work properly,
   but we are only bottle-necked by Clang there.
2. Do it after the fact, similar to a linker.

I think it makes sense to go with approach 1 as far as possible.
The reason to do this is that it saves time on emitting
indexes and also on de-duplicating after-the-fact.

<details>
<summary>Well-behaved headers</summary>

Headers can be divided into two types:
1. Headers that expand the same way in different contexts
   (modulo header guard optimization) and are only `#include`d
   in a top-level context (i.e. not inside a function/type etc.).
2. Headers that expand differently in different contexts,
   or are `#included`d in a non-top-level context.

I'm going to call the first type of headers "well-behaved".

For well-behaved headers, it is sufficient to emit a SCIP Document
only once, from any TU that includes the header.

For ill-behaved headers, they need to have multiple SCIP Documents
which are aggregated.
</details>

For example, a [Clang build
has 2.6M SLOC and 575M SLOC of pre-processed code](https://github.com/sourcegraph/sourcegraph/issues/42280#issuecomment-1342500849).
So the amount of duplication is very high.
If each well-behaved header is only indexed once,
and most headers are well-behaved, then we can
save on a lot of work, both during indexing
and by avoiding de-duplication altogether.

In the [Kythe documentation](https://github.com/kythe/kythe/blob/master/kythe/cxx/indexer/cxx/claiming.md),
this is called "Claiming". AFAICT, in Kythe,
individual workers are responsible for claiming
work that they'll handle. For dynamic (on-the-fly)
claiming, they talk to a shared memcached instance
to do this work.

In our case, the driver should handle the assignment
of work so that well-behaved headers are indexed only once.
This also lets us add recovery logic in the driver in
the future. See NOTE(ref: header-recovery).

The [IndexStore documentation](https://docs.google.com/document/d/1cH2sTpgSnJZCkZtJl1aY-rzy4uGPcrI-6RrUpdATO2Q/)
describes Apple Clang relying on file checks
to implement de-duplication;
for each header-hash pair, a worker will check
if a file already exists. We don't adopt the same
strategy for debuggability:
having the logic be centralized aids debugging
and instrumentation, and we're not relying on the
inherent raciness of filesystem checks
(albeit, the TOCTOU only creates redundant work
and does not affect correctness).
One might reason that avoiding the filesystem
is better for performance too, but IndexStore
is claimed to have overhead of 2%-5% over the baseline
(semantic analysis only), so a performance-based
argument is inapplicable.

### Checking well-behavedness of headers

We will do well-behavedness analysis on-the-fly,
instead of breaking indexing up into two passes,
where first we determine all the well-behaved headers
(across all TUs) and then perform the indexing work.
The reason to do this on-the-fly is that
checking well-behavedness requires completing
semantic analysis, and we should preserve the AST contexts
for each TU for indexing. However, keeping `N_TUs`
AST contexts (instead of `N_workers` contexts) in memory
is likely to lead to OOM,
and serializing/deserializing all of them
will incur IO costs.

The [Modularize tool in Clang/LLVM](https://clang.llvm.org/extra/modularize.html)
in some sense kinda' already implements
parts of the well-behavedness checking
that we need, but it is not quite the same thing.

The main files of interest are:
- [Modularize.cpp](https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang-tools-extra/modularize/Modularize.cpp)
- [PreprocessorTracker.cpp](https://sourcegraph.com/github.com/llvm/llvm-project/-/blob/clang-tools-extra/modularize/PreprocessorTracker.cpp)

The tool checks if:
1. A header is expanded in a `namespace` or `extern` block.
2. A header expands differently in different contexts by
   comparing the list of entities in the AST after expansion.

For point 1,
AFAICT based on skimming the code, it will miss headers
included in top-level functions and types.
However, we should probably handle those too.

For point 2, comparing entities requires all the entities
to be in the same process because entities are compared
by pointer equality. So we need to serialize the entities
across the process boundary. However, that requires potentially
serializing a lot of data for comparisons that are likely
to succeed anyways.

We can work around this by computing a hash of the content
after expansion and send that for comparison.
Both Kythe and Apple Clang also use hashes,
which provides confidence that this is overall the right strategy.

<details>
<summary>Correctness of hashing post-expansion bytes</summary>

Technically, I think hashing bytes (even ignoring
any headers inside namespace/extern etc.) isn't 100% correct.
The Kythe docs on claiming point out situations
where even though a header expanded to the
same contents byte-wise,
the final corresponding AST can be different
due to differing contexts causing different
implicit definitions to be generated.

I can't think of a realistic example where this
would be true, but I think it is OK to ignore
this edge case if we can avoid comparing
entities for equality (or content-hash the AST,
which would also be error-prone).
</details>

## Indexing templates

Recommended reading: [cppreference page on dependent names](https://en.cppreference.com/w/cpp/language/dependent_name)

In the context of templates, C++ has two kinds of names:
- Dependent names, where the result of name lookup may
  depend on the substitutions for template parameters.
- Non-dependent names, where the result of name lookup
  is not allowed to depend on substitutions,
  even if considering substitutions would lead to a better match.

Consequently, it is not always possible to get the correctly
name-resolved result for a name without template substitution.

For example, prefixing method calls with `this->`
inside a templated class makes a name dependent.
So in the presence of templates,
omitting `this->` for method calls is not always permitted,
and adding `this->` can make "obviously wrong" code compile.
Here's a code example:

```cpp
template <typename T>
struct Q0 {
  void f0() {}
};

template <typename T>
struct Q1: Q0<T> {
  void f1() {
    // f0 is dependent here, since `this` has type Q1<T>*
    this->f0(); // OK
    // f0 is independent here due to absence of explicit `this`
    f0(); // error: use of undeclared identifier 'f0'
    this->non_existent(); // OK: no template instantiation => no error
  }
};
```

For indexing templates, there are roughly 3 possible options
from an indexer's point of view:

1. Pedantic:
   - Traverse uninstantiated template bodies once,
     and collect information about non-dependent names.
   - For every template instantiation, traverse the template body
     once, and collect information about dependent names.
     This can be de-duplicated on-the-fly.
2. Generalizing:
   - Traverse uninstantiated template bodies once,
     and collect information about non-dependent names.
   - Randomly select a single instantiation. Traverse the template body
     for this instantiation, and collect information about dependent names.
3. Optimistic:
  - Traverse uninstantied template bodies once,
    and collect information about both non-dependent and dependent names.
    For dependent names, rely on some way of performing approximate name lookup.

We go with the Optimistic approach in scip-clang for the following reasons:

- Performance: It is the only approach
  compatible with the optimization of indexing a header only once (per transcript).
  Otherwise, if a template in a header is included in a TU,
  but not instantiated,
  then indexing the header will not fully index the body of the template.
  Going for the Pedantic approach would likely lead to a large amount
  of redundant work across TUs due to repeated traversals of the same or
  similar template instantiations. The extra information would also
  increase the time for index merging.
- Good enough: Based on experience, most dependent names in practice
  behave like non-dependent names anyways.
  This is reflected in clangd's index also using imprecise name lookup
  for dependent names:

  ```cpp
    /// Performs an imprecise lookup of a dependent name in this class.
    ///
    /// This function does not follow strict semantic rules and should be used
    /// only when lookup rules can be relaxed, e.g. indexing.
    std::vector<const NamedDecl *>
    lookupDependentName(DeclarationName Name,
                        llvm::function_ref<bool(const NamedDecl *ND)> Filter);
  ```

## Mapping C++ to SCIP

(FQN = Fully Qualified Name)

### Symbol names for macros

The FQN for a macro should include the full source location
for the definition, i.e. both the containing file path
and the (line, column) pair.

<details>
<summary>Why include the path to the header?</summary>

The reason to include the path to the header is that,
if the same macro is defined in two different files,
it is more likely to mean two different things
rather than the same thing (unlike a forward declaration).
The one exception to this is
the pattern of having textual inclusion files
(i.e., deliberately ill-behaved headers) [[example in apple/swift](https://sourcegraph.com/github.com/apple/swift@f237fba206ab5fb152ea024611c00aa625a69f14/-/blob/lib/AST/ASTPrinter.cpp?L934-937&subtree=true)].

```cpp
// inc.h
constexpr int magic = MAGIC;

// cafe.cc
#define MAGIC 0xcafe
#include "inc.h"
#undef MAGIC

// boba.cc 
#define MAGIC 0xb0ba
#include "inc.h"
#undef MAGIC
```

However, in this case, Go to Definition on `MAGIC`
in `inc.h` will correctly show both the definitions
and `cafe.cc` and `boba.cc` under the current scheme,
so there's no problem.
</details>

<details>
<summary>Why include the source location within the header?</summary>

There are two common situations where we may have a macro
defined with the same name within the same file:
conditional definition and
def/undef for textual inclusion files.

<details>
<summary>Conditional definition</summary>

Conditional definition looks like the following:
```cpp
// mascot.h
#if __LINUX__
  #define MASCOT "penguin"
#else
  #define MASCOT ""
#endif
```

In this case, the macros represent the "same thing"
in a way, but only one of them is going to be active
at a given time. For a single build, it doesn't matter
for the two definitions to have different locations.
If/when we implement index merging across builds
(e.g. debug/release, Linux/macOS/Windows etc.),
there will be two different kinds of expansion sites:

1. Conditional expansion sites:
    ```cpp
    #include "mascot.h"
    #if __LINUX__
      #if __arm64__
        #define MASCOT_ICON (MASCOT "_muscular.jpg")
      #else
        #define MASCOT_ICON (MASCOT ".jpg")
      #endif
    #else
      #define MASCOT_ICON ""
    #endif
    ```
   For the reference to `MASCOT` under `__LINUX__`,
   we'd like Go to Definition to umambiguously
   jump to the first definition. This requires
   the two definitions to have a disambiguator
   (the line+column is one possible choice choice).
2. Unconditional expansion site:
   ```cpp
   #include <cstring>
   #include "mascot.h"
   bool hasMascot() {
     return std::strcmp(MASCOT, "") != 0;
   }
   ```
   In this case, index merging will create two references
   for `MASCOT` to the two different definitions,
   which is the desired behavior.
</details>

<details>
<summary>Def/undef for multiple inclusion</summary>

This one is similar to the point mentioned in
the section about why we should include the path,
except the includes here happen in the same file.

```cpp
// inc.h
constexpr int magic = MAGIC;

// hobby.cc
class Cafe {
  #define MAGIC 0xcafe
  #include "inc.h"
  #undef MAGIC
};

class Boba {
  #define MAGIC 0xb0ba
  #include "inc.h"
  #undef MAGIC
};
```

In this case, the two definitions are logically distinct
(the classes could well have been in different files),
so it makes sense to have a disambiguator.
</details>

</details>

### Symbol names for declarations

<!-- NOTE(def: symbol-names-for-decls) -->

- For builtins, we can create a synthetic namespace e.g. `$builtin::type`.
  (using `$` because it is not legal in C++)
- For FQNs with no anonymous namespaces, we will use the FQN directly.
- For anonymous namespaces in FQNs, they should be replaced with the TU path,
  and some suffix like `$anon`.
  Anonymous namespaces in headers are not semantically meaningful,
  we need to use the path of the final TU, not the path of the header.
- For anonymous types defined in headers, we need to use the header path
  in the symbol name, so that we can consistently identify the same
  anonymous type even when the header is included in different TUs.

One consequence of these rules is that header names almost
never become a part of Symbol names, except for two cases:

1. Macros (as mentioned in the previous section)
2. Anonymous types

This is important because different headers
may forward-declare the same (non-anonymous) declarations,
and we want all such forward declarations
to show up when doing Find references.

The point about merging forward declarations does not
apply to macros and anonymous types,
as they cannot be forward-declared.
Hence, it is OK to use the header path in the symbol name
for both of these.

#### Symbol names for enum cases

C only supports enums, whereas C++ also supports scoped enums. Whether
a declaration uses `enum` or `enum class` (or `enum struct`)
affects name lookup.

```cpp
enum E { E0 };
enum class EC { EC0 }; // C++ only

void f() {
  E0;      // OK in C and C++
  E::E0;   // ERROR in C, OK in C++
  EC0;     // ERROR in C++
  EC::EC0; // OK in C++
}
```

Certain headers may be used in both C mode and C++ mode, in which
case, they will use `enum` only. What canonical name should be used
for the `E0` case then, `E::E0` or `E0`?

We want cross-references to work correctly for mixed C/C++ codebases
(or more generally, with cross-repo references). The options are:
1. Make sure the header is indexed twice, once in C mode and once
    in C++ mode.  When indexing as C, emit `E0`, when indexing as C++,
    emit `E::E0` (for the same source range).
2. Emit both `E0` and `E::E0` in both C and C++ modes.
3. Emit only `E0` in both C and C++ modes.

Option 1 doesn't meet the cross-repo requirement. For example, we may
be indexing a pure C codebase/library which is accessed from a
separate repo in C++.

Option 2 is strictly redundant, as having different unscoped enums with
the same case names in the same namespace is an error in C++.

So we go with option 3.

### Method disambiguator

Disambiguators are allowed in SCIP to distinguish
between different method overloads.

We need to make sure that method disambiguators
are computed in a _stable_ way.
The stability should apply across different runs
(for determinism), as well as future evolution of Clang.

Since there is no specific ordering of method declarations,
we can use a hash of the canonicalized type
of the method instead.
We need to be careful about qualifiers though;
e.g. the following is allowed in C++:

```cpp
void f(int *);
void f(const int *p) { } // same as f above
```

However, this is illegal in C.
I think this means that in C mode,
type canonicalization will keep the `const` qualifier on the parameter
(so that a type error can be reported on the mismatch),
whereas in C++, it will be stripped out.
(I have not double-checked the Clang code to see if this is the case.)

One thing we could do here is potentially hash the string representation
(used for type errors) of the normalized type of the method declaration.
Yes, technically, that's not guaranteed to be stable across Clang versions,
but it's probably not going to change very often...

We don't want to hash the full type,
because the type probably contains a bunch of fluff that
doesn't matter for type equality.
We should also avoid having our own Clang type traversal,
because we would need to potentially update such code whenever
this is a change to the AST upstream.

### Forward declarations

Forward declarations should be treated as reference Occurrences,
so that Go to Definition doesn't take one to a forward declaration by default.

In the future, we could add a `ForwardDeclaration` case
to `SymbolRole` in SCIP if we want to surface information
about forward declarations in the UI.

The tricky part about forward declarations is determining
the package information, which is part of the symbol name.
In particular, the following pattern is reasonably common:

```cpp
// MyProjectHeader.h

namespace something {
// Avoid #include "external_project/LargeHeader.h" to reduce compile times
class ExternalClass;
}

void doStuff(const ExternalClass *);

// MyProjectImpl.cc

#include "external_project/LargeHeader.h"
#include "MyProjectHeader.h"

void doStuff(const ExternalClass *k) { /* impl */ }
```

Just by looking at forward-declaration with the name
`something::ExternalClass`, an indexing process has
no way to tell if the definition for `ExternalClass`
lives in a file in the current package
or in an external package.

<details>
<summary>
Could we determine the package information based
on the namespace, maybe with a user-supplied mapping
from FQN regexes to package information?
</summary>

At first glance, it might seem that maybe we can
determine the package information based on the namespace.
For example, if the user could provide a mapping for
namespaces (regexes) -> packages, and if we see 
a forward declaration in (say) the `llvm::` namespace,
and the mapping has `llvm::.* -> llvm@15.0.0`,
we could use `llvm@15.0.0` as the package information
for the forward declaration.

However, there isn't always such a strong correspondence
between namespaces and packages,
especially for patterns relying on features like ADL
and template specializations.

ADL example: (from our own code!)

```
namespace llvm {
llvm::raw_ostream &operator<<(llvm::raw_ostream &os, Escaped s) {..}
}
```

Template specialization example: (via https://stackoverflow.com/a/8157967)

```
namespace std {
template <> struct hash<MyType> {
  size_t operator()(const MyType & x) const { .. }
};
}
```

In both of these cases, the corresponding function or template specialization
lives in a different project than the project which "owns" the namespace.

</details>

So, in general, if we want proper navigation for forward declarations,
we need to actually discover which file
the definition lives in,
and this means that we need to index every TU,
even if the main file does not belong to the current package.

This raises a further problem, what do we do when the definition
is not part of the indexing process at all?
There are 4 possible cases here:
1. Third-party binary-only libraries: In this case, there's not
   really much that can be done, since a definition isn't available.
2. First-party libraries built separately: In this case, we can
   potentially consume the SCIP index generated from indexing the
   first-party library, to identify which forward declaration
   belongs to which library.
3. Platform headers (e.g. `unistd.h`): These can depend on standard
   library headers. TODO: How should we handle this case?
4. Standard library headers (e.g. `iostream`): We can double-check
   if standard library headers include any platform headers.
   If not, we can make the simplifying assumption that any
   forward declarations in standard library headers belong
   to the corresponding synthetic standard library package
   (e.g. `libc++@15.0.0`).

<!-- TODO(def: handle-forward-decls) -->
For simplicity, an initial implementation
can put fake `SymbolInformation` values
in the `external_symbols` list
with an empty package name and version.

## Cross-repo navigation

For cross-repo navigation, the "only" thing the indexer needs to do
is to consistently assign package IDs (name + version pairs)
to all non-local symbols. Ostensibly,
the package ID should be the one the declaration "belongs" to.

This runs into problems with two kinds of declarations: namespaces
and forward declarations.

### Namespace declarations in a cross-repo setting

C++ namespaces can cut across packages.
For example, generally types customize hashing
by adding template specializations inside the `std` namespace.

There are four main options for handling namespaces.

1. Attempt to figure out the "original" package for a namespace
   and use that.
2. Use whatever namespace the immediate namespace declaration
   was found in.
3. Drop package information altogether (use a blank name and version).
4. Require a "build seed" that is used to set the name and version.
   This build seed would need to be set consistently across different
   cross-repo indexing processes.

Option 1 is not practically feasible, since it's not clear how it would
be implemented in the general case. There is no central information
anywhere about which namespaces are "owned" by which packages.

Option 2 is implementable. The main downside is that namespace references
cutting across packages will not be cross-linked, because the symbol
names will differ.

Option 3 and option 4 allow cross-linking of namespace references across
packages. Option 3 creates the risk of false positives if there are
situations where a namespace with the same name
is used in entirely unrelated packages (maybe they aren't even built together).
Option 4 avoids that, but it introduces a requirement
to re-index the standard library for different seeds,
and introduces a need to plumb state across indexing jobs
(since the seed can't be inferred from the build configuration necessarily).

So we go with option 3 as the least worst option.

### Forward declarations in a cross-repo setting

The information about what symbol a forward declaration refers to
is not reliably available in a worker,
it is only available at the time of index merging.

Since the symbol name needs to take the package name into account,
we need to perform a lookup in a worker based on the symbol name
without the package ID prefix.

There are two options for doing this:

1. Additionally serialize symbol names without the package ID
   in index shards, and use those as map keys.
2. Partition the symbol name reliably during index merging,
   and use the suffix as the lookup key.

Option 1 would increase the overhead of (de)serializing indexes,
and requires us to fiddle with the Index Protobuf schema
(to add an extra field which tracks the prefix-free symbol name).

So we go with option 2 instead. Specifically, the descriptor
part of the symbol name would start with a `$` (or we could
add it to the end of the version, either is OK).
We would forbid the use of `$` in package names and versions,
allowing us to quickly split a symbol name.

<!-- See NOTE(ref: symbol-string-hack-for-forward-decls) -->

The extra `$` could be stripped when serializing the final index.

Additionally, since Occurrences track the symbol name,
we separate out occurrences for forward declarations
(see [fwd_decls.proto](/proto/fwd_decls.proto))
into a separate file with a different format,
to avoid having to rewrite the symbol string for a large
number of occurrences.
Instead, those occurrences are written out at the end.
