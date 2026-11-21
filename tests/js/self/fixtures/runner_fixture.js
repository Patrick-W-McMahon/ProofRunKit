// Fixture for tests/js/self/cli_test.js.in (#55): run by `prk run --json`; two tests fail and one skips on purpose.
print('printed while loading');
prk.addTest('runner.first', () => {});
prk.addTest('runner.second_fails', () => {
    prk.expectEqual(1, 2, 'first failure');
    prk.expectEqual(3, 4, 'second failure');
});
prk.addTest('runner.third_skips', () => prk.skipTest('not today'));
prk.addTest('runner.fourth_escapes', () => prk.failTest('quote " backslash \\ newline \n tab \t accent \u00e9 <x&y>'));
prk.addTest('runner.fifth', () => print('printed in a test'));
