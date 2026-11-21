/**
 * @file builtin_checks.hpp
 * @brief Environment checks that `proofrunkit` runs when no test scripts are given.
 */
#pragma once

#include "proofrunkit/prk.h"

namespace prk {

/**
 * @brief Registers the kit's built-in environment checks (names start with "kit.").
 * @param registry Registry to add to.
 * @return PRK_STATUS_OK, or the first registration error.
 */
[[nodiscard]] PrkStatus register_builtin_checks(PrkRegistry& registry);

}  // namespace prk
