-- Sample used by CTest: one passing and one failing test, so prk exits with code 1.
prk.add_test("sample.passes", function() prk.expect_equal(1 + 1, 2) end)
prk.add_test("sample.fails", function() prk.expect_equal(1 + 1, 3, "deliberate failure") end)
