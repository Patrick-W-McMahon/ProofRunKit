-- Sample used by CTest: a syntax error must be reported with its line, not counted as success.
prk.add_test("sample.never_registered", function()
    prk.expect_true(true)
-- missing "end)"
