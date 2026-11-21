// Fixture for the prk test suite fixture (#84): two passing tests.
prk.addTest('fixture.one', () => prk.expectEqual(1, 1));
prk.addTest('fixture.two', () => prk.expectTrue(true));
