-- Self-hosted tests for suites and setup/teardown hooks (#12, #55). Tests run in registration
-- order, so the last test can check the order every hook ran in.
-- Run with:  prk run tests/lua/self/suites_and_hooks_test.lua

local log = {}
local function record(entry) log[#log + 1] = entry end

prk.before_all(function() record("script.before_all") end)
prk.before_each(function() record("script.before_each") end)
prk.after_each(function() record("script.after_each") end)

prk.describe("outer", function()
    local fixture
    prk.before_all(function() record("outer.before_all") fixture = { count = 0 } end)
    prk.before_each(function() record("outer.before_each") fixture.count = fixture.count + 1 end)
    prk.after_each(function() record("outer.after_each") end)
    prk.after_all(function() record("outer.after_all") fixture = nil end)

    prk.add_test("first", function()
        record("outer.first")
        prk.expect_equal(fixture.count, 1, "before_each ran once")
    end)

    prk.describe("inner", function()
        prk.before_each(function() record("inner.before_each") end)
        prk.after_each(function() record("inner.after_each") end)
        prk.add_test("second", function()
            record("outer.inner.second")
            prk.expect_equal(fixture.count, 2, "the outer fixture is shared across the suite")
        end)
    end)
end)

prk.describe("failing_setup_is_contained", function()
    prk.add_test("skips_cleanly", function() record("skip") prk.skip_test("only checks that skips run after_each") end)
end)

prk.add_test("hooks_ran_in_order", function()
    local expected = {
        -- Pending before_all hooks run first (outermost first), then before_each hooks.
        "script.before_all", "outer.before_all",
        "script.before_each", "outer.before_each", "outer.first", "outer.after_each",
        "script.after_each",
        "script.before_each", "outer.before_each", "inner.before_each", "outer.inner.second", "inner.after_each",
        "outer.after_each", "script.after_each", "outer.after_all",
        "script.before_each", "skip", "script.after_each",
        "script.before_each",
    }
    prk.expect_deep_equal(log, expected)
end)

prk.add_test("suite_names_are_dotted", function()
    -- The runner reports this test as "suite_names_are_dotted"; nested ones as "outer.inner.second".
    -- Names are checked through the reports in reports_test.lua; here we check registration rules.
    prk.expect_error(function() prk.describe("late", function() end) end, "only be called while the script loads")
    prk.expect_error(function() prk.add_test("late", function() end) end, "only be called while the script loads")
    prk.expect_error(function() prk.after_all(function() end) end, "only be called while the script loads")
end)
