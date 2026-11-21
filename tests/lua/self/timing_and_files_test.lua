-- Self-hosted tests for timing and read-only file checks (#12, #55).
-- prk test (prk.tests.json) runs this with the repository root as the working directory, so relative paths below
-- resolve inside the source tree.
-- Run with (from the repository root):  prk run tests/lua/self/timing_and_files_test.lua

local this_file = "tests/lua/self/timing_and_files_test.lua"

prk.add_test("timing.clock_is_monotonic", function()
    local first = prk.read_clock_ms()
    prk.expect_type(first, "number")
    prk.expect_equal(math.type(first), "integer")
    local second = prk.read_clock_ms()
    prk.expect_greater_or_equal(second, first)
end)

prk.add_test("timing.measure_elapsed_ms", function()
    local calls = 0
    local elapsed = prk.measure_elapsed_ms(function(step) calls = calls + step end, 2)
    prk.expect_equal(calls, 2, "the action ran with its arguments")
    prk.expect_greater_or_equal(elapsed, 0)
    prk.expect_less_than(elapsed, 60000)
end)

prk.add_test("files.finds_files", function()
    prk.expect_true(prk.is_file_present(this_file))
    prk.expect_true(prk.is_file_present("LICENSE"))
    prk.expect_false(prk.is_file_present("tests/lua/self/no_such_file.txt"))
    prk.expect_false(prk.is_file_present("tests/lua/self"), "folders are not files")
    prk.expect_file_present("NOTICE")
end)

prk.add_test("files.reads_text", function()
    local text = prk.read_text_file(this_file)
    prk.expect_contains(text, "-- Self-hosted tests for timing and read-only file checks")
    prk.expect_file_contains("LICENSE", "Apache License")
    prk.expect_file_contains("NOTICE", "Lua 5.4.9")
    prk.expect_error(function() prk.expect_file_contains("LICENSE", "GNU General Public License") end, "text not found")
end)

prk.add_test("files.refuses_paths_outside_the_working_directory", function()
    for _, path in ipairs({ "../outside.txt", "tests/../../outside.txt", "/etc/hosts", "C:/Windows/win.ini",
                            "C:\\Windows\\win.ini", "\\\\server\\share\\x", "LICENSE:stream" }) do
        prk.expect_error(function() prk.read_text_file(path) end, "access denied")
        prk.expect_error(function() prk.is_file_present(path) end, "access denied")
    end
    prk.expect_error(function() prk.read_text_file("no_such_file.txt") end, "not found")
end)

prk.add_test("timeout.option_is_accepted", function()
    -- The timeout itself is checked by CTest prk_lua_timeout_option (an endless loop must stop).
    for i = 1, 1000 do local _ = i * i end
end, { timeout_ms = 60000 })
