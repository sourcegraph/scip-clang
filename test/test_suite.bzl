def scip_clang_test_suite(compdb_data):
    print("compdb_data = %s" % compdb_data)
    native.sh_test(
        name = "test_compdb",
        srcs = ["test_main.sh"],
        args = ["--compdb-tests"],
        data = ["//test:test_main"] + compdb_data,
        size = "small",
    )
    native.sh_test(
        name = "update_compdb",
        srcs = ["test_main.sh"],
        args = ["--compdb-tests", "--update"],
        data = ["//test:test_main"] + compdb_data,
        size = "small",
    )
    native.test_suite(
        name = "test",
        tests = ["test_compdb"],
    )
    native.test_suite(
        name = "update",
        tests = ["update_compdb"],
    )
