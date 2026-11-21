-- Self-hosted tests for tag selection (#12, #55). CTest runs this file with
-- `--tag selected --exclude-tag excluded`; the tests that must not run fail if they do.
-- Run with:  prk run --tag selected --exclude-tag excluded tests/lua/self/tags_test.lua

prk.add_test("tags.selected_test_runs", function() end, { tags = { "selected" } })

prk.add_test("tags.untagged_test_is_not_selected", function()
    prk.fail_test("selection by --tag should have left this test out")
end)

prk.add_test("tags.excluded_wins_over_selected", function()
    prk.fail_test("--exclude-tag should win over --tag")
end, { tags = { "selected", "excluded" } })

prk.describe("tagged_suite", function()
    prk.before_all(function() end)
    prk.add_test("inherits_the_suite_tag", function() end)
    prk.add_test("skip_option_is_reported_as_skipped", function() error("must not run") end, { skip = "demonstrates skip" })
end, { tags = { "selected" } })

prk.describe("unselected_suite", function()
    prk.before_all(function() error("hooks of suites without selected tests must not run") end)
    prk.add_test("is_not_selected", function() prk.fail_test("must not run") end)
end)
