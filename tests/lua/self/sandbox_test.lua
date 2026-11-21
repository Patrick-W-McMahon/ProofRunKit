-- Self-hosted (#55) twin of the C++ cases lua.sandbox.* and lua.engine.reports_versions: what a Lua test script can
-- and cannot reach, under the limits `prk run` uses by default.
-- Run with:  prk run tests/lua/self/sandbox_test.lua

prk.describe("lua_sandbox", function()
    prk.add_test("hides_ambient_access", function()
        for _, name in ipairs({"io", "os", "package", "debug", "require", "dofile", "loadfile"}) do
            prk.expect_equal(_G[name], nil, name)
        end
        prk.expect_equal(string.dump, nil, "string.dump")
        prk.expect_equal(type(string.format), "function")
        prk.expect_equal(type(table.concat), "function")
        prk.expect_equal(type(coroutine.wrap), "function")
    end)

    prk.add_test("loads_text_but_not_bytecode", function()
        prk.expect_equal(load("return 40 + 2")(), 42)
        local chunk, message = load("\27Lua", "bytes", "b")
        prk.expect_nil(chunk)
        prk.expect_contains(message, "binary chunk")
        local text_chunk = load("\27Lua" .. string.rep("\0", 32))
        prk.expect_nil(text_chunk)
    end)

    prk.add_test("memory_limit_is_a_catchable_failure", function()
        local ok, message = pcall(function() return ("x"):rep(1 << 30) end)
        prk.expect_false(ok)
        prk.expect_match(tostring(message), "memory") -- "not enough memory" inside pcall.
        prk.expect_equal(#("x"):rep(100), 100) -- The script keeps working after the limit was hit.
    end)


    prk.add_test("add_test_only_while_loading", function()
        prk.expect_error(function() prk.add_test("late", function() end) end,
                         "prk.add_test can only be called while the script loads")
    end)

    prk.add_test("reports_versions", function()
        prk.expect_equal(prk.lua_version, "Lua 5.4.9")
        prk.expect_match(prk.version, "^%d+%.%d+%.%d+")
    end)
end)
