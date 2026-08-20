#pragma once
// Minimal no-op spdlog stub for the standalone RelocPointerTable unit test, so
// the test links without pulling the full spdlog/fmt dependency. The table's
// logging calls (warn/error/info) are irrelevant to what the test verifies.
namespace spdlog {
template <class... A> inline void warn(const char*, A...) {}
template <class... A> inline void error(const char*, A...) {}
template <class... A> inline void info(const char*, A...) {}
} // namespace spdlog
