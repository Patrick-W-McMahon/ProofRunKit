// Process spawn from JavaScript test scripts (#12 over #13).
// prk test (prk.tests.json) runs this with the working directory set to the folder that holds proofrunkit,
// so './proofrunkit' is ProofRunKit itself.

prk.addTest('js_process.runs_a_program_and_captures_stdout', () => {
    const result = prk.runProcess({ program: './proofrunkit', args: ['--version'], timeoutMs: 30000 });
    prk.expectExitCode(result, 0);
    prk.expectMatch(result.stdout, /^ProofRunKit \d+\.\d+\.\d+/);
    prk.expectMatch(result.stdout, /QuickJS-ng/);
});

prk.addTest('js_process.reports_non_zero_exit_and_stderr', () => {
    const result = prk.runProcess({ program: './proofrunkit', args: ['help', 'no-such-command'] });
    prk.expectExitCode(result, 2);
    prk.expectContains(result.stderr, 'unknown command');
});

prk.addTest('js_process.missing_program_is_a_result_not_an_error', () => {
    const result = prk.runProcess({ program: 'prk_no_such_tool_4711' });
    prk.expectEqual(result.outcome, 'failed to start');
    prk.expectEqual(result.exitCode, undefined);
    prk.expectError(() => prk.expectExitCode(result, 0), 'executable not found: prk_no_such_tool_4711');
});
