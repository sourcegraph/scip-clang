# Troubleshooting scip-clang issues

scip-clang may report errors and/or warnings
for diagnosing issues even when indexing completes successfully,
which is marked by a `Finished indexing ...` message,
along with a non-empty `index.scip` file.

The most common problems are timeouts, crashes,
and lack of sufficient space for [IPC][] (in Docker),
which are discussed below.

## Missing code intel

In certain cases, precise code intel may be entirely missing despite
creating and uploading an index successfully.
There are two common failure modes for this.

### Possibility 1: Missing documents in SCIP index

A SCIP index stores documents containing definition and reference
information for each file on disk.

[SCIP CLI]: https://github.com/sourcegraph/scip/releases

You can inspect the documents using the [SCIP CLI][]:

```bash
scip print --json index.scip | jq .documents
```

If this prints a non-empty array, see the [troubleshooting steps for possibility 2](#possibility-2-incorrect-document-paths-in-scip-index).

If this prints `null` or an empty array, it's likely
that the indexer was invoked from a directory other than the project root.
You can double-check this using:

```bash
scip print --json index.scip | jq .metadata.projectRoot
```

If this points to a subdirectory and not the project root,
then the scip-clang invocation was incorrect.

Here is an example of a common project structure:

```
/home/me/myproject
+--- .git/
+--- README.md
+--- src/
|     |
|     +--- A.cc
|     +--- subdir/
|            +--- B.cc
+--- build/ (.gitignored directory)
```

In this case, `scip-clang` **must** be invoked from `/home/me/myproject`,
regardless of the contents of the compilation database file (`compile_commands.json`).

If you want to only index a subset of files (say under `src/subdir`),
then reduce the size of the compilation database by selecting only specific files.

For example, if the compilation database looks like the following:

```json
[
  {
    "directory": "/home/me/myproject/build",
    "file": "/home/me/myproject/src/A.cc",
    "command": "some stuff here"
  },
  {
    "directory": "/home/me/myproject/build",
    "file": "/home/me/myproject/src/subdir/B.cc",
    "command": "some stuff here"
  }
]
```

And if you only want to index the files inside `src/subdir`, then run:

```bash
cat compile_commands.json | jq '.[] | select(.file | contains("src/subdir/"))' > minimal.json
scip-clang --compdb-path=minimal.json <other args>
```

### Possibility 2: Incorrect document paths in SCIP index

If the output of the following command using the [SCIP CLI][]

```bash
scip print --json index.scip | jq .documents
```

is a non-empty array, and you're still not seeing precise code intel
in the Sourcegraph UI, it's possible the document paths stored in the index
do not match the actual paths on disk.

Double-check that the various `relativePath` keys in the `documents`
array correspond to actual files on disk relative to the `projectRoot` path.

- If the `relativePath` values are incorrect, then double-check if there
  are any symlinks present in the project layout which may potentially
  be causing incorrect path determination.
- If the `relativePath` values are correct, but you still don't see any
  precise code intel for that file, check that `occurrences` arrays for
  various documents are non-empty.
  - If the `occurrences` arrays are empty, then report it as an
    indexer bug.
  - If the `occurrences` arrays are non-empty, then you can double-check
    the precise code intel using the debug view in the Sourcegraph UI.

    Click on the 🧠 icon near the top-right of the file contents view
    for the commit for which the index was uploaded,
    and click on 'Display debug information'.
    ![Debug information for a SCIP index](https://github.com/sourcegraph/scip-clang/assets/93103176/58becf36-5524-40f6-87b9-a72bc00b1e04).

    Then try hovering over entities which have `^^^` markers below them;
    you should see precise code intel.

## Timeouts

scip-clang sets a timeout for indexing an individual translation unit,
so that any hangs or crashes during indexing a single translation unit
do not cause indexing to fail.

The default timeout value is 5 minutes,
which should be sufficient to handle most code.
That said, if you see this error a lot,
[file a bug report](/README.md#reporting-issues).

If you're debugging an issue using a `-dev` build of `scip-clang`,
this value may need to be adjusted,
as the dev build performs more runtime checks.
To change the tieout value, pass `--receive-timeout-seconds 600`
when invoking `scip-clang`.
However, we do not recommend using this flag with the release build,
as a higher value will lead to [reduced concurrency](https://github.com/sourcegraph/scip-clang/issues/45)
for the duration of the timeout value if an indexing process crashes.

## Crashes

If you are able, run the following with a [`-dev` binary](https://github.com/sourcegraph/scip-clang/releases)
for your release.

Re-run the failing `scip-clang` command with `--log-level=debug`
and `--show-compiler-diagnostics`, and include those
when you [submit an issue](/README.md#reporting-issues)

## Disk space for IPC

<!-- Be careful about re-titling this section;
we print the link to it in an error message
when the user runs into a space issue. -->

[IPC]: https://en.wikipedia.org/wiki/Inter-process_communication

scip-clang uses [IPC][] to coordinate indexing work
between a driver process and one or more worker processes.

On Linux, this corresponds to `/dev/shm`.
By default, Docker uses a size of 64 MiB for `/dev/shm`.
scip-clang defaults to using 1 worker per core,
and about 2 MB of space for each worker,
which means that a limit of 64 MiB may be too low
for high core count machines.

If the available space is too low, scip-clang may
reduce the number of workers spawned,
or may fail to start.

There are 3 possible fixes for this:

1. (Recommended) Increase the size of `/dev/shm`:
   In Docker, this can be done by passing `--shm-size`
   to the `docker run` invocation.
   For example, you can use `--shm-size 256M`.
2. Reduce the per-worker IPC space by passing
   `--ipc-size-hint-bytes` to `scip-clang`.
   For example, you could set this to a lower value
   like `500000` instead of the default `2000000`.
   Setting this to a lower value
   increases the risk of indexing failures
   in the presence of:
   - Large compilation commands in the compilation database.
   - Files which transitively include a very large number of headers.
3. Reducing the number of workers:
   scip-clang will automatically use fewer workers if possible,
   but will print a warning when it does so.
   This warning can be suppressed by explicitly passing `--jobs N`.

## Skipped compilation database entries

After completing indexing, scip-clang will print output like the following:

```
Finished indexing 100 translation units in 40.2s (indexing: 38.0s, merging: 2.2s, num errored TUs: 0).
Skipped: 30 compilation database entries (non main file extension: 30, not found on disk: 0).
```

Here, some entries are skipped because
scip-clang only looks at compilation database entries corresponding to
translation units (i.e. C, C++ or CUDA implementation files).
If a compilation database contains other entries,
for example, related to some code generation step or header files,
those are skipped.

Generally, this should not be a cause for concern,
as some compilation database generation tools
generate superfluous entries in the compilation database
which are not useful from an indexing perspective.

For example, here is the list of file extensions with counts
for scip-clang's own compilation database.

```
$ jq '.[].file' compile_commands.json | awk -F'/' '{print $(NF)}' | sed -E 's/"//g' | sed -E 's/.+\./\./g' | sort | uniq -c
  27 .c
 225 .cc
1956 .cpp
   6 .def
3375 .h
6081 .hpp
 175 .inc
 104 .ipp
   2 .pl
```

Out of these, scip-clang will only create indexing jobs
for the entries for `.c`, `.cc` and `.cpp` files.
For header files, it is OK
to skip processing the corresponding compilation database entries
as the header file will be indexed
when they are included by a translation unit
(either directly or via some other header file).
