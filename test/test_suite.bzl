load("@bazel_skylib//lib:paths.bzl", "paths")

def _snapshot_test(name, kind, data):
    suffix = kind + ("" if name == "" else "_" + name)
    test_name = "test_" + suffix
    update_name = "update_" + suffix
    test_args = (
        ["--test-kind=" + kind] +
        ([] if name == "" else ["--test-name=" + name])
    )
    native.sh_test(
        name = test_name,
        srcs = ["test_main.sh"],
        args = test_args,
        data = ["//test:test_main"] + data,
        size = "small",
    )
    native.sh_test(
        name = update_name,
        srcs = ["test_main.sh"],
        args = test_args + ["--update"],
        data = ["//test:test_main"] + data,
        size = "small",
    )
    return (test_name, update_name)

def _group_by_top_level_dir(paths):
    groups = {}
    for p in paths:
        i = p.find("preprocessor/")
        start = i + len("preprocessor/")
        end = p[start:].find("/")
        testdir = p[start:start + end]
        if testdir in groups:
            groups[testdir].append(p)
        else:
            groups[testdir] = [p]
    return groups

def scip_clang_test_suite(compdb_data, preprocessor_data):
    native.sh_test(
        name = "test_unit",
        srcs = ["test_main.sh"],
        args = ["--test-kind=unit"],
        data = ["//test:test_main"],
        size = "small",
    )
    tests = ["test_unit"]
    updates = []

    (test_name, update_name) = _snapshot_test(name = "", kind = "compdb", data = compdb_data)
    tests.append(test_name)
    updates.append(update_name)

    preprocessor_test_groups = _group_by_top_level_dir(preprocessor_data)
    preprocessor_tests = []
    preprocessor_updates = []
    for (testdir, paths) in preprocessor_test_groups.items():
        if "/" in testdir:
            fail("Unexpected / in testdir: %s; wrong indexing" % testdir)
        (test_name, update_name) = _snapshot_test(name = testdir, kind = "preprocessor", data = paths)
        preprocessor_tests.append(test_name)
        preprocessor_updates.append(update_name)
    native.test_suite(
        name = "test_preprocessor",
        tests = preprocessor_tests,
    )
    tests += preprocessor_tests
    native.test_suite(
        name = "update_preprocessor",
        tests = preprocessor_updates,
    )
    updates += preprocessor_updates

    native.test_suite(
        name = "test",
        tests = tests,
    )
    native.test_suite(
        name = "update",
        tests = updates,
    )
