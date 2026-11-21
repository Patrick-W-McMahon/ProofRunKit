-- Self-hosted (#55): process spawn from Lua test scripts (#12 over #13).
-- prk test (prk.tests.json) runs this with the working directory set to the folder that holds proofrunkit,
-- so "./proofrunkit" is ProofRunKit itself.

prk.add_test("self.process.runs_a_program_and_captures_stdout", function()
    local result = prk.run_process{program = "./proofrunkit", args = {"--version"}, timeout_ms = 30000}
    prk.expect_exit_code(result, 0)
    prk.expect_true(result.ok)
    prk.expect_match(result.stdout, "^ProofRunKit %d+%.%d+%.%d+")
    prk.expect_equal(result.stderr, "")
end)

prk.add_test("self.process.reports_non_zero_exit_and_stderr", function()
    local result = prk.run_process{program = "./proofrunkit", args = {"--no-such-option"}}
    prk.expect_exit_code(result, 2)
    prk.expect_false(result.ok)
    prk.expect_contains(result.stderr, "unknown option")
end)

prk.add_test("self.process.missing_program_is_a_result_not_an_error", function()
    local result = prk.run_process{program = "prk_no_such_tool_4711"}
    prk.expect_equal(result.outcome, "failed to start")
    prk.expect_equal(result.error, "executable not found: prk_no_such_tool_4711")
    local ok, message = pcall(prk.expect_exit_code, result, 0)
    prk.expect_false(ok)
    prk.expect_contains(tostring(message), "failed to start: executable not found")
end)

prk.add_test("self.process.passes_arguments_exactly", function()
    local result = prk.run_process{program = "./proofrunkit", args = {"list", "--filter", "kit."}}
    prk.expect_exit_code(result, 0)
    prk.expect_match(result.stdout, "kit%.")
end)
