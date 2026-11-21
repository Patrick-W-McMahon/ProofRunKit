/**
 * @file test_registry.cpp
 * @brief Tests for prk_create_registry, prk_register_test_case, prk_count_test_cases, prk_get_test_case_name.
 */
#include <cstring>
#include <memory>
#include <string>

#include "selftest_support.hpp"

namespace prk::selftest {

namespace {

using RegistryPointer = std::unique_ptr<PrkRegistry, decltype(&prk_destroy_registry)>;

RegistryPointer create_owned_registry() {
    return RegistryPointer(prk_create_registry(), &prk_destroy_registry);
}

void do_nothing(PrkTestContext*, void*) {}

void test_new_registry_is_empty(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    PRK_EXPECT(registry != nullptr);
    PRK_EXPECT(prk_count_test_cases(registry.get()) == 0);
    PRK_EXPECT(prk_get_test_case_name(registry.get(), 0) == nullptr);
}

void test_registers_in_order_and_copies_names(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    std::string first_name = "alpha";
    const PrkTestCase first{first_name.c_str(), &do_nothing, nullptr};
    const PrkTestCase second{"beta", &do_nothing, nullptr};
    PRK_EXPECT(prk_register_test_case(registry.get(), &first) == PRK_STATUS_OK);
    PRK_EXPECT(prk_register_test_case(registry.get(), &second) == PRK_STATUS_OK);
    first_name = "changed";  // The registry must hold its own copy.
    PRK_EXPECT(prk_count_test_cases(registry.get()) == 2);
    PRK_EXPECT(std::strcmp(prk_get_test_case_name(registry.get(), 0), "alpha") == 0);
    PRK_EXPECT(std::strcmp(prk_get_test_case_name(registry.get(), 1), "beta") == 0);
    PRK_EXPECT(prk_get_test_case_name(registry.get(), 2) == nullptr);
}

void test_rejects_duplicate_names(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    const PrkTestCase test_case{"same", &do_nothing, nullptr};
    PRK_EXPECT(prk_register_test_case(registry.get(), &test_case) == PRK_STATUS_OK);
    PRK_EXPECT(prk_register_test_case(registry.get(), &test_case) == PRK_STATUS_DUPLICATE_NAME);
    PRK_EXPECT(prk_count_test_cases(registry.get()) == 1);
}

void test_rejects_invalid_arguments(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    const PrkTestCase empty_name{"", &do_nothing, nullptr};
    const PrkTestCase null_name{nullptr, &do_nothing, nullptr};
    const PrkTestCase null_body{"no_body", nullptr, nullptr};
    PRK_EXPECT(prk_register_test_case(registry.get(), &empty_name) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_register_test_case(registry.get(), &null_name) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_register_test_case(registry.get(), &null_body) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_register_test_case(registry.get(), nullptr) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_register_test_case(nullptr, &null_body) == PRK_STATUS_INVALID_ARGUMENT);
    PRK_EXPECT(prk_count_test_cases(registry.get()) == 0);
    PRK_EXPECT(prk_count_test_cases(nullptr) == 0);
}

void test_grows_past_initial_capacity(PrkTestContext* context, void*) {
    const RegistryPointer registry = create_owned_registry();
    for (int index = 0; index < 100; ++index) {
        const std::string name = "case_" + std::to_string(index);
        const PrkTestCase test_case{name.c_str(), &do_nothing, nullptr};
        PRK_EXPECT(prk_register_test_case(registry.get(), &test_case) == PRK_STATUS_OK);
    }
    PRK_EXPECT(prk_count_test_cases(registry.get()) == 100);
    PRK_EXPECT(std::strcmp(prk_get_test_case_name(registry.get(), 99), "case_99") == 0);
}

void test_destroy_accepts_null(PrkTestContext*, void*) {
    prk_destroy_registry(nullptr);  // Must not crash; reaching the end of the body is the pass condition.
}

}  // namespace

PrkStatus register_registry_tests(PrkRegistry& registry) {
    const PrkTestCase test_cases[] = {
        {"core.registry.new_registry_is_empty", &test_new_registry_is_empty, nullptr},
        {"core.registry.registers_in_order_and_copies_names", &test_registers_in_order_and_copies_names, nullptr},
        {"core.registry.rejects_duplicate_names", &test_rejects_duplicate_names, nullptr},
        {"core.registry.rejects_invalid_arguments", &test_rejects_invalid_arguments, nullptr},
        {"core.registry.grows_past_initial_capacity", &test_grows_past_initial_capacity, nullptr},
        {"core.registry.destroy_accepts_null", &test_destroy_accepts_null, nullptr},
    };
    return register_test_cases(registry, test_cases);
}

}  // namespace prk::selftest
