-- Fixture for tests/js/self/cli_test.js.in (#55): the default Lua memory limit fails the test with its size.
prk.add_test("limits.memory", function()
    local _ = ("x"):rep(1 << 30)
end)
prk.add_test("limits.recovers", function()
    prk.expect_equal(#("x"):rep(100), 100)
end)
