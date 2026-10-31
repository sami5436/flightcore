#pragma once

#include <string_view>

namespace fc::core {

// Build version, injected by CMake from project(VERSION ...).
[[nodiscard]] std::string_view version() noexcept;

}  // namespace fc::core
