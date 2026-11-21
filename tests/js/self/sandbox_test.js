// Self-hosted (#55) twin of the C++ cases js.sandbox_has_no_ambient_access and js.reports_engine_version.
// Run with:  prk run tests/js/self/sandbox_test.js

prk.describe('js_sandbox', () => {
    prk.addTest('has_no_ambient_access', () => {
        for (const name of ['require', 'std', 'os', 'process', 'scriptArgs', 'setTimeout', 'fetch']) {
            prk.expectEqual(typeof globalThis[name], 'undefined', name);
        }
    });

    prk.addTest('dynamic_import_fails', async () => {
        const failed = await import('qjs:os').then(() => false, () => true);
        prk.expectTrue(failed, 'import must fail');
    });

    prk.addTest('static_import_is_a_syntax_error', () => {
        try {
            eval("import 'qjs:std'");
            prk.failTest('imported');
        } catch (error) {
            prk.expectTrue(error instanceof SyntaxError, String(error));
        }
    });

    prk.addTest('reports_versions', () => {
        prk.expectEqual(prk.jsEngine, 'QuickJS-ng 0.18.0');
        prk.expectMatch(prk.version, /^\d+\.\d+\.\d+/);
    });
});
