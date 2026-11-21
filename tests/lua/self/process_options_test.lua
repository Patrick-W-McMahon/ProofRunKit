-- Self-hosted twin of the C++ process.* spawn cases (#55): environment, working directory, standard
-- input, timeout kill, and output limits, through prk.run_process. CTest runs this file from the folder
-- that holds the prk_spawn_child helper, so "./prk_spawn_child" works on every platform.

local child = "./prk_spawn_child"

local function run_child(options)
    options.program = child
    return prk.run_process(options)
end

local function trim_line_end(text)
    return (text:gsub("[\r\n]+$", ""))
end

prk.describe("process_options", function()
    prk.add_test("sets_environment_variables", function()
        local result = run_child{args = {"env", "PRK_SELF_VALUE"}, env = {PRK_SELF_VALUE = "forty two"}}
        prk.expect_exit_code(result, 0)
        prk.expect_equal(trim_line_end(result.stdout), "forty two")
    end)

    prk.add_test("can_drop_the_inherited_environment", function()
        local result = run_child{args = {"env", "PRK_OFFLINE"}, inherit_env = false, env = {PRK_SELF_VALUE = "x"}}
        prk.expect_exit_code(result, 0)
        prk.expect_equal(trim_line_end(result.stdout), "(unset)")
    end)

    -- "../selftest" names the same folder another way, so "./prk_spawn_child" resolves the same whether
    -- the platform looks it up from the old or the new working directory.
    prk.add_test("runs_in_the_requested_directory", function()
        local here = run_child{args = {"cwd"}}
        local same = run_child{args = {"cwd"}, cwd = "../selftest"}
        prk.expect_exit_code(same, 0)
        prk.expect_equal(trim_line_end(same.stdout), trim_line_end(here.stdout))
        local missing = run_child{args = {"cwd"}, cwd = "no_such_folder_prk"}
        prk.expect_false(missing.ok)
        prk.expect_equal(missing.outcome, "failed to start")
    end)

    prk.add_test("feeds_standard_input_exactly", function()
        local input = "line one\nline two\0with a zero byte"
        local result = run_child{args = {"cat"}, stdin = input}
        prk.expect_exit_code(result, 0)
        prk.expect_equal(result.stdout, input)
    end)

    prk.add_test("kills_a_process_that_times_out", function()
        local result = run_child{args = {"sleep", "10000"}, timeout_ms = 300}
        prk.expect_equal(result.outcome, "timed out")
        prk.expect_false(result.ok)
        prk.expect_true(result.elapsed_ms < 8000, "the child should be stopped early")
    end)

    prk.add_test("limits_captured_output", function()
        local result = run_child{args = {"flood", "200000"}, output_limit = 1000}
        prk.expect_exit_code(result, 0)
        prk.expect_equal(#result.stdout, 1000)
        prk.expect_true(result.stdout_truncated)
        prk.expect_true(result.stderr_truncated)
    end)
end)
