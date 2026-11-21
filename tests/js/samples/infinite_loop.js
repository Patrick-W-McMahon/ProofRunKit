// An infinite loop that also tries to swallow the interrupt: the budget must still stop it.
prk.addTest('sample.js_infinite_loop', () => {
    for (;;) {
        try {
            for (;;) {}
        } catch (ignored) {
            // The instruction-budget interrupt is uncatchable, so this never runs.
        }
    }
});
