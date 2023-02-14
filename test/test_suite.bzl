load("@bazel_skylib//lib:paths.bzl", "paths")

def _test_main(name, args, data, tags):
    native.sh_test(
        name = name,
        srcs = ["test_main.sh"],
        args = args,
        data = data + ["//test:test_main"],
        env = {"TEST_MAIN": "./test/test_main"},
        size = "small",
        tags = tags,
    )

def _ipc_test(name, args):
    native.sh_test(
        name = name,
        srcs = ["test_main.sh"],
        args = args,
        data = ["//test:ipc_test_main"],
        env = {"TEST_MAIN": "./test/ipc_test_main"},
        size = "small",
        # Don't cache because the test can be non-deterministic
        tags = ["no-cache"],
    )

def _snapshot_test(name, kind, data, tags = []):
    suffix = kind + ("" if name == "" else "_" + name)
    test_name = "test_" + suffix
    update_name = "update_" + suffix
    test_args = (
        ["--test-kind=" + kind] +
        ([] if name == "" else ["--test-name=" + name])
    )
    _test_main(name = test_name, args = test_args, data = data, tags = tags)
    _test_main(name = update_name, args = test_args + ["--update"], data = data, tags = tags)
    return (test_name, update_name)

def _group_by_top_level_dir(dirname, paths):
    groups = {}
    for p in paths:
        i = p.find(dirname + "/")
        start = i + len(dirname + "/")
        end = p[start:].find("/")
        testdir = p[start:start + end]
        if testdir in groups:
            groups[testdir].append(p)
        else:
            groups[testdir] = [p]
    return groups

def _snapshot_test_suite(name, make_pair, args):
    tests, updates = make_pair(args)
    native.test_suite(
        name = "test_" + name,
        tests = tests,
    )
    native.test_suite(
        name = "update_" + name,
        tests = updates,
    )
    return (tests, updates)

def _preprocessor_tests(preprocessor_data):
    preprocessor_test_groups = _group_by_top_level_dir("preprocessor", preprocessor_data)
    tests, updates = [], []
    for (testdir, paths) in preprocessor_test_groups.items():
        if "/" in testdir:
            fail("Unexpected / in testdir: %s; wrong indexing" % testdir)
        t, u = _snapshot_test(name = testdir, kind = "preprocessor", data = paths)
        tests.append(t)
        updates.append(u)
    return (tests, updates)

def _robustness_tests(data):
    tests, updates = [], []
    for fault in ["crash", "sleep", "spin"]:
        (test_name, update_name) = _snapshot_test(
            name = fault,
            kind = "robustness",
            data = data + ["//indexer:scip-clang"],
            # no-cache doesn't work sometimes, so add "external" 😔
            # https://github.com/bazelbuild/bazel/issues/15516
            tags = ["no-cache", "external"],
        )
        tests.append(test_name)
        updates.append(update_name)
    return (tests, updates)

def _index_tests(data):
    index_test_groups = _group_by_top_level_dir("index", data)
    tests, updates = [], []
    for (testdir, paths) in index_test_groups.items():
        t, u = _snapshot_test(name = testdir, kind = "index", data = paths + ["//indexer:scip-clang"])
        tests.append(t)
        updates.append(u)
    return (tests, updates)

def scip_clang_test_suite(compdb_data, preprocessor_data, robustness_data, index_data):
    _test_main(name = "test_unit", args = ["--test-kind=unit"], data = [], tags = [])
    tests = ["test_unit"]
    updates = []

    (test_name, update_name) = _snapshot_test(name = "", kind = "compdb", data = compdb_data)
    tests.append(test_name)
    updates.append(update_name)

    ts, us = _snapshot_test_suite("preprocessor", _preprocessor_tests, preprocessor_data)
    tests += ts
    updates += us

    _ipc_test(name = "test_ipc_hang", args = ["--hang"])
    _ipc_test(name = "test_ipc_crash", args = ["--crash"])
    tests += ["test_ipc_hang", "test_ipc_crash"]

    ts, us = _snapshot_test_suite("robustness", _robustness_tests, robustness_data)
    tests += ts
    updates += us

    ts, us = _snapshot_test_suite("index", _index_tests, index_data)
    tests += ts
    updates += us

    native.test_suite(
        name = "test",
        tests = tests,
    )
    native.test_suite(
        name = "update",
        tests = updates,
    )
