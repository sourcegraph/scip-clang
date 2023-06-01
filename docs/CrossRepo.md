# Cross-repository code navigation

scip-clang supports cross-repository configuration via explicit configuration.
Before following the steps here, we recommend that you first
[set up single-repository code navigation](/README.md#usage)
and check that it works as expected.

Specifically, each project (i.e. different repo in Sourcegraph)
of interest needs to be indexed with
an extra JSON file describing package information.

```bash
# Run from package0's root directory
scip-clang --package-map-path=package-map.json <other flags>...
```

The package map JSON file contains a list of objects in the following format:

```json
[
  {"path": ".", "package": "package0@v0"},
  {"path": "path/to/package1_root", "package": "package1@v1"},
  ...
]
```

As an example, you can see [scip-clang's own package map file](/tools/package-map.json).

1. The `path` key may be an absolute path, or a path relative to the current directory
   (which must be the project root). For example:
   - For projects using Bazel, these paths will generally look like:
     `./bazel-myproject/external/com_company_libcool`.
2. The `package` key consists of a `name` followed by an `@` separator and a `version`.
   - The name and version must only contain characters belonging to `[a-zA-Z0-9_\-\.]`.
   - The version should be chosen based on release information.
     For example, if you use git tags to mark releases in repos,
     and repositories only depend on tagged releases (instead of arbitrary commits),
     then the git tag can be used as the version.
     The important thing is that the version needs to be consistent when
     different projects are indexed, and it should not be reused over time.

     The reason for this is that cross-repo code navigation works
     by treating the concatenation of (1) the package name,
     (2) the package version and (3) the qualified symbol name
     (e.g `std::vector`) as the unique symbol ID across a Sourcegraph instance.

Files under the directories `path/to/package1_root`
will be treated as belonging to `package1`'s `v1` version.

If one package root is a prefix of another, package information
is assigned based on the longest match. For example, if you're
using git submodules, then packages in subdirectories will be
recognized correctly if there is a package map entry
pointing to the subdirectory.

For cross-repository navigation to work,
`package1` must also be indexed with the same version information:

```bash
# This doesn't contain information about package0,
# as package0 depends on package1, but not vice-versa.
$ cat other-package-map.json
[
  {"path": ".", "package": "package1@v1"},
  ...
]
$ scip-clang --package-map-path=other-package-map.json <other flags>...
```

Once both these indexing operations are performed and the indexes
are uploaded to a Sourcegraph instance, cross-repository navigation
should work across `package0` and `package1`.

## Limitations

At the moment, the amount of indexing work required
scales quadratically with the depth of the dependency graph.
Specifically, if package PC depends on PB which depends on PA,
then PA will need to be indexed thrice (once by itself, once when PB is indexed,
once when PC is indexed), PB will be indexed twice (once by itself,
once when PC is indexed), and PC will be indexed once.

The reason for this is that the indexer needs to identify
package information for which declaration is defined in which package,
so that it can correctly support code navigation for forward declarations.
However, strictly speaking, any package can forward-declare any entity.

For example, if PA defines a function f, and a header in PC
forward-declares `pa::f`, then when C is indexed, the indexer
needs to somehow know that the definition of `f` lives in some
file in PA. There are a few different ways to do this:

1. Always index all TUs. This is the current strategy.
   This is the build equivalent of building everything from source.
2. Provide a way to reuse the indexes from dependencies,
   and use those to resolve forward declarations.
   This is the build equivalent of using archives/shared libraries/TBDs.
3. Only index the TUs for the "current" package. If the
   definition for a forward declaration is not found,
   no reference information is emitted (slightly worse UX, but faster).
   This is not supported directly in scip-clang,
   but can be achieved by removing entries for
   out-of-project files from the compilation database.
4. Rely on some heuristics and/or user-supplied hint.
   For example, if there was a way to provide a hint that
   unresolved forward declarations in namespace `pa` must map
   to declarations in package PA, then indexer could
   trust that information and correctly emit a reference
   for `pa::f` at the forward declaration without having
   to re-index TUs in PA.

We're [looking for feedback](https://github.com/sourcegraph/scip-clang/issues/358)
on which approach would work best for your use case,
before implementing a solution for this.
