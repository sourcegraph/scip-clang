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

1. The `path` key may be an absolute path, or a path relative to the current directory
   (which must be the project root).
2. The `package` key consists of a `name` followed by an `@` separator and a `version`.
   - The name and version must only contain characters belonging to `[a-zA-Z0-9_\-.]`.
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
is assigned based on the longest match.

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
