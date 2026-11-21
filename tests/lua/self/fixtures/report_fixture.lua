-- Fixture for reports_test.lua (#21, #55): a known mix of outcomes. CTest runs it with
-- --filter fixture. --exclude-tag slow --json-file ... --junit-file ...; it exits 1 by design.
prk.add_test("fixture.passes", function() prk.expect_true(true) end)
prk.add_test("fixture.fails", function()
    prk.fail_test('<tag> & "quotes"; quote " backslash \\ tab \t newline \n end')
end)
prk.add_test("fixture.skips", function() prk.skip_test("fixture skip reason") end)
prk.add_test("fixture.slow", function() end, { tags = { "slow" } })
prk.add_test("other.filtered_out", function() end)
prk.describe("fixture", function()
    prk.describe("group", function()
        prk.add_test("nested", function() end)
    end)
end)
