// Self-hosted (#84) JavaScript twin of tests/lua/self/process_options_test.lua: prk.runProcess options (env,
// inheritEnv, cwd, stdin, timeoutMs, outputLimit) against the prk_spawn_child helper. prk test (prk.tests.json) runs
// this file from the folder that holds prk_spawn_child. Covers script_process.js_passes_request.

const child = './prk_spawn_child';

function runChild(options) {
    return prk.runProcess({program: child, ...options});
}

function trimLineEnd(text) {
    return text.replace(/[\r\n]+$/, '');
}

prk.describe('js_process_options', () => {
    prk.addTest('sets_environment_variables', () => {
        const result = runChild({args: ['env', 'PRK_SELF_VALUE'], env: {PRK_SELF_VALUE: 'forty two'}});
        prk.expectExitCode(result, 0);
        prk.expectEqual(trimLineEnd(result.stdout), 'forty two');
    });

    prk.addTest('can_drop_the_inherited_environment', () => {
        const result = runChild({args: ['env', 'PRK_OFFLINE'], inheritEnv: false, env: {PRK_SELF_VALUE: 'x'}});
        prk.expectExitCode(result, 0);
        prk.expectEqual(trimLineEnd(result.stdout), '(unset)');
    });

    prk.addTest('runs_in_the_requested_directory', () => {
        const here = trimLineEnd(runChild({args: ['cwd']}).stdout);
        prk.expectTrue(here.length > 0, 'the child should print its working directory');
        prk.expectEqual(trimLineEnd(runChild({args: ['cwd'], cwd: here}).stdout), here);
        const parent = prk.runProcess({program: here + '/prk_spawn_child', args: ['cwd'], cwd: '..'});
        prk.expectExitCode(parent, 0);
        const parentFolder = trimLineEnd(parent.stdout);
        prk.expectTrue(parentFolder.length > 0 && parentFolder.length < here.length, parentFolder);
        prk.expectEqual(here.slice(0, parentFolder.length), parentFolder);
        const missing = runChild({args: ['cwd'], cwd: 'no_such_folder_prk'});
        prk.expectFalse(missing.ok);
        prk.expectEqual(missing.outcome, 'failed to start');
    });

    prk.addTest('feeds_standard_input_exactly', () => {
        const input = 'line one\nline two\nthree';
        const result = runChild({args: ['cat'], stdin: input});
        prk.expectExitCode(result, 0);
        prk.expectEqual(result.stdout, input);
    });

    prk.addTest('kills_a_process_that_times_out', () => {
        const result = runChild({args: ['sleep', '10000'], timeoutMs: 300});
        prk.expectEqual(result.outcome, 'timed out');
        prk.expectFalse(result.ok);
        prk.expectLessThan(result.elapsedMs, 8000);
    });

    prk.addTest('limits_captured_output', () => {
        const result = runChild({args: ['flood', '200000'], outputLimit: 1000});
        prk.expectExitCode(result, 0);
        prk.expectEqual(result.stdout.length, 1000);
        prk.expectTrue(result.stdoutTruncated && result.stderrTruncated);
    });

    prk.addTest('rejects_unknown_options', () => {
        prk.expectError(() => prk.runProcess({program: child, colour: 'red'}), /colour/);
        prk.expectError(() => prk.runProcess({program: child, timeoutMs: 86400001}), /timeoutMs/);
    });
});
