def _snapshot_test(name, data):
    test_name = "test_" + name
    update_name = "update_" + name
    native.sh_test(
        name = test_name,
        srcs = ["test_main.sh"],
        args = ["--test-kind=" + name],
        data = ["//test:test_main"] + data,
        size = "small",
    )
    native.sh_test(
        name = update_name,
        srcs = ["test_main.sh"],
        args = ["--test-kind=" + name, "--update"],
        data = ["//test:test_main"] + data,
        size = "small",
    )
    return (test_name, update_name)

def scip_clang_test_suite(compdb_data):
    native.sh_test(
        name = "test_unit",
        srcs = ["test_main.sh"],
        args = ["--test-kind=unit"],
        data = ["//test:test_main"],
        size = "small",
    )
    tests = ["test_unit"]
    updates = []

    (test_name, update_name) = _snapshot_test("compdb", compdb_data)
    tests.append(test_name)
    updates.append(update_name)

    (test_name, update_name) = _snapshot_test("preprocessor", [])
    tests.append(test_name)
    updates.append(update_name)

    native.test_suite(
        name = "test",
        tests = tests,
    )
    native.test_suite(
        name = "update",
        tests = updates,
    )
