// ProofRunKit testing its JavaScript platform (#11) with its own JavaScript API.
// Run with:  prk run tests/js/prk_js_api_test.js
// Every test here must pass (one deliberate skip); prk test (prk.tests.json) runs this file through the shipped prk executable.

prk.addTest('js_api.expect_equal_uses_strict_equality', () => {
    prk.expectEqual(6 * 7, 42);
    prk.expectEqual('prk', 'p' + 'rk');
    prk.expectNotEqual(1, '1', 'strict: a number is not a string');
});

prk.addTest('js_api.reports_versions', () => {
    prk.expectEqual(prk.jsEngine, 'QuickJS-ng 0.18.0');
    prk.expectMatch(prk.version, /^\d+\.\d+\.\d+$/, 'prk.version is semantic');
});

prk.addTest('js_api.assertion_failures_are_catchable_error_objects', () => {
    let caught = null;
    try {
        prk.expectEqual(1, 2);
    } catch (error) {
        caught = error;
    }
    prk.expectTrue(caught instanceof prk.AssertionError, 'expectEqual throws prk.AssertionError');
    prk.expectMatch(caught.message, /prk_js_api_test\.js:\d+: values differ: expected 2, got 1/);
});

prk.addTest('js_api.deep_equal_and_contains', () => {
    prk.expectDeepEqual({ name: 'prk', tags: ['fast', { level: 1 }] }, { name: 'prk', tags: ['fast', { level: 1 }] });
    prk.expectContains('hello world', 'o w');
    prk.expectContains(new Set([1, 2]), 2);
    const message = prk.expectError(() => { throw new RangeError('out of range: 7'); }, /out of range/);
    prk.expectEqual(message, 'out of range: 7');
});

prk.addTest('js_api.async_tests_are_awaited', async () => {
    const value = await Promise.resolve(21).then((half) => half * 2);
    prk.expectEqual(value, 42);
});

prk.addTest('js_api.skip_test_marks_the_test_skipped', () => {
    prk.skipTest('demonstrates a skip; the runner counts it as skipped, not failed');
});

prk.addTest('js_sandbox.has_no_ambient_file_process_or_module_access', async () => {
    for (const name of ['require', 'std', 'os', 'process', 'scriptArgs', 'setTimeout', 'fetch', 'XMLHttpRequest']) {
        prk.expectEqual(typeof globalThis[name], 'undefined', name + ' must not be reachable');
    }
    const imported = await import('qjs:std').then(() => true, () => false);
    prk.expectFalse(imported, 'dynamic import must fail');
});

prk.describe('js_suite', () => {
    const order = [];
    prk.beforeAll(() => order.push('before_all'));
    prk.beforeEach(() => order.push('before_each'));
    prk.addTest('hooks_run_in_order', () => {
        prk.expectDeepEqual(order, ['before_all', 'before_each']);
    });
});
