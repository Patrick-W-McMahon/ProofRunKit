-- Sample used by CTest: an endless loop must be stopped by the instruction budget.
prk.add_test("sample.spins_forever", function()
    while true do end
end)
