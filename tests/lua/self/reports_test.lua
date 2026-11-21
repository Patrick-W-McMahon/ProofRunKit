-- Self-hosted tests for the runner, reporting, and command-line parsing (#21, #55), checked through
-- report files. CTest first runs prk on fixtures/report_fixture.lua and on the built-in checks with
-- --json-file / --junit-file (fixture tests prk_self_reports_*), then runs this script in the
-- folder that holds the reports.
-- Run with (after the fixture runs):  prk run tests/lua/self/reports_test.lua

local function read_report(name)
    return prk.read_text_file("self-reports/" .. name)
end

prk.describe("json", function()
    prk.add_test("header_and_totals", function()
        local json = read_report("fixture.json")
        prk.expect_match(json, '^{\n  "schema_version": 1,\n  "tool": "proofrunkit",\n  "version": "%d+%.%d+%.%d+",')
        prk.expect_contains(json, '"summary": {"total": 4, "passed": 2, "failed": 1, "skipped": 1}')
    end)

    prk.add_test("one_entry_per_selected_test_in_order", function()
        local json = read_report("fixture.json")
        local names = {}
        for name, outcome in json:gmatch('"name": "([^"]+)", "outcome": "(%a+)"') do
            names[#names + 1] = name .. "=" .. outcome
        end
        prk.expect_deep_equal(names, {
            "fixture.passes=passed", "fixture.fails=failed", "fixture.skips=skipped", "fixture.group.nested=passed",
        })
    end)

    prk.add_test("filters_and_tags_were_applied", function()
        local json = read_report("fixture.json")
        prk.expect_false(json:find("fixture.slow", 1, true), "--exclude-tag slow removed fixture.slow")
        prk.expect_false(json:find("other.filtered_out", 1, true), "--filter fixture. removed other.*")
    end)

    prk.add_test("messages_are_escaped", function()
        local json = read_report("fixture.json")
        prk.expect_contains(json, 'quote \\" backslash \\\\ tab \\t newline \\n end')
    end)

    prk.add_test("builtin_checks_report", function()
        local json = read_report("builtin.json")
        prk.expect_contains(json, '"name": "kit.cycle_counter.reads_nonzero", "outcome": "passed"')
        prk.expect_contains(json, '"failed": 0')
    end)
end)

prk.describe("junit", function()
    prk.add_test("document_and_totals", function()
        local xml = read_report("fixture.xml")
        prk.expect_match(xml, '^<%?xml version="1%.0" encoding="UTF%-8"%?>\n<testsuites name="proofrunkit" ')
        prk.expect_contains(xml, 'tests="4" failures="1" errors="0" skipped="1"')
        prk.expect_match(xml, "</testsuites>\n$")
    end)

    prk.add_test("test_cases_map_names_and_outcomes", function()
        local xml = read_report("fixture.xml")
        prk.expect_match(xml, '<testcase classname="fixture" name="passes" time="%d+%.%d%d%d"/>')
        prk.expect_match(xml, '<testcase classname="fixture%.group" name="nested" time="[%d.]+"/>')
        prk.expect_match(xml, '<testcase classname="fixture" name="fails" time="[%d.]+">\n      <failure message="')
        prk.expect_contains(xml, '<skipped message="fixture skip reason"/>')
    end)

    prk.add_test("text_is_escaped", function()
        local xml = read_report("fixture.xml")
        prk.expect_contains(xml, "&lt;tag&gt; &amp; &quot;quotes&quot;")
        prk.expect_false(xml:find("<tag>", 1, true), "raw markup must not appear")
    end)
end)
