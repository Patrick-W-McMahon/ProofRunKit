-- ProofRunKit testing itself with its own Lua platform (#9, first step of #55).
-- Run with:  prk run tests/lua/prk_lua_api_test.lua
-- Every test here must pass; CTest runs this file through the shipped prk executable.

prk.add_test("lua_api.expect_equal_accepts_equal_values", function()
    prk.expect_equal(6 * 7, 42)
    prk.expect_equal("prk", "p" .. "rk")
    prk.expect_equal(nil, nil)
end)

prk.add_test("lua_api.expect_true_accepts_truthy_values", function()
    prk.expect_true(true)
    prk.expect_true(0, "zero is truthy in Lua")
    prk.expect_true("", "the empty string is truthy in Lua")
end)

prk.add_test("lua_api.pcall_catches_plain_errors_inside_a_test", function()
    local ok, message = pcall(error, "plain error")
    prk.expect_true(not ok and message:find("plain error") ~= nil)
end)

prk.add_test("lua_api.reports_versions", function()
    prk.expect_equal(prk.lua_version, "Lua 5.4.9")
    prk.expect_true(prk.version:match("^%d+%.%d+%.%d+$") ~= nil, "prk.version is semantic")
end)

prk.add_test("lua_api.skip_test_marks_the_test_skipped", function()
    prk.skip_test("demonstrates a skip; the runner counts it as skipped, not failed")
end)

prk.add_test("lua_sandbox.has_no_ambient_file_process_or_module_access", function()
    for _, name in ipairs({ "io", "os", "package", "debug", "require", "dofile", "loadfile" }) do
        prk.expect_equal(_G[name], nil, name .. " must not be reachable")
    end
    prk.expect_equal(string.dump, nil, "string.dump must not be reachable")
end)

prk.add_test("lua_sandbox.load_accepts_text_and_rejects_bytecode", function()
    prk.expect_equal(load("return 1 + 1")(), 2)
    local chunk, message = load("\27Lua", "bytes", "b")
    prk.expect_true(chunk == nil, "binary chunks must be rejected")
    prk.expect_true(message:find("binary chunk") ~= nil, message)
end)

prk.add_test("lua_sandbox.safe_standard_libraries_work", function()
    prk.expect_equal(string.format("%03d", 7), "007")
    prk.expect_equal(table.concat({ "a", "b", "c" }, ","), "a,b,c")
    prk.expect_equal(math.max(3, 9, 4), 9)
    prk.expect_equal(utf8.len("h\u{e9}llo"), 5)
    local numbers = coroutine.wrap(function()
        for value = 1, 3 do coroutine.yield(value) end
    end)
    prk.expect_equal(numbers() + numbers() + numbers(), 6)
end)
