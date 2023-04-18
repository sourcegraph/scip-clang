# Troubleshooting scip-clang issues

scip-clang may report errors and/or warnings
for diagnosing issues even when indexing completes successfully,
which is marked by a `Finished indexing ...` message,
along with a non-empty `index.scip` file.

The most common problems are timeouts, crashes,
and lack of sufficient space for [IPC][] (in Docker),
which are discussed below.

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