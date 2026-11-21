// A deliberately failing JavaScript test: CTest checks the exit code and the failure message.
prk.addTest('sample.js_failure', () => {
    prk.expectEqual(1 + 1, 3, 'deliberate failure');
});
