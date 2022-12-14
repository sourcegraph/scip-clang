# Design sketch for scip-clang

There are three main things to discuss here:
- What the overall indexer architecture will be
- How we can avoid redundant work for headers across TUs
  (["claiming"](https://github.com/kythe/kythe/blob/master/kythe/cxx/indexer/cxx/claiming.md)
  in the Kythe docs)
- How the indexer will map C++ to SCIP

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

Workers write out SCIP Documents based on paths
provided by the driver after completing an indexing job.
This write is done after each job is complete,
instead of after completing all jobs,
because it reduces RSS as well as the blast radius
in case the worker gets killed or crashes later.

After all indexing work is completed, the driver
assembles the Documents into a SCIP Index.

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

Each job could output a compressed archive
separately containing the different Documents.
The final merge step would unique the Documents,
and assemble them into an Index.

NOTE: This functionality would not be part of an MVP.

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
This lets the driver also fit in recovery logic
-- if a header wasn't indexed by a worker
which was assigned to index it
(e.g. because the worker crashed),
then the driver can wait to see if some other
worker can index it.
In the worst case, if all workers which would've
otherwise indexed that header have already started
indexing, then the driver can later re-request
some indexing jobs so that these headers
can get indexed.

### Checking well-behavedness of headers

We will do well-behavedness analysis on-the-fly,
instead of breaking indexing up into two passes,
where first we determine all the well-behaved headers
and then perform the indexing work.
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
I'm not 100% sure if this will be enough,
but I think it makes sense to try this first.

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

- For builtins, we can create a synthetic namespace e.g. `$builtin::type`.
  (using `$` because it is not legal in C++)
- For FQNs with no anonymous namespaces, we will use the FQN directly.
- For anonymous namespaces in FQNs, they should be replaced with the TU path,
  and some suffix like `$anon`.
  Anonymous namespaces in headers are not semantically meaningful,
  we need to use the path of the final TU, not the path of the header.

One consequence of these rules is that header names
never become a part of Symbol names (except for macros,
as mentioned in the previous section).
This is important because different headers
may forward-declare the same types,
and we want all such forward declarations to
show up when doing Find references.

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

Forward declarations should only have reference Occurrences.
This means that Go to Definition will work more intuitively.

We can potentially add a `isForwardDeclaration` bit
to SCIP's occurrence mask in the future if needed.
