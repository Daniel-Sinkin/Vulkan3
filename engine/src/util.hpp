// engine/src/util.hpp
#pragma once
#include <cstdlib>
#include <print>
#include <utility>

namespace DSEngine::Util
{

[[noreturn]] inline void panic_base(const char *file, int line, const char *func, std::string_view msg = {}) noexcept
{
    if (!msg.empty())
        std::println(stderr, "[PANIC] {}:{} ({}): {}", file, line, func, msg);
    else
        std::println(stderr, "[PANIC] {}:{} ({})", file, line, func);
    std::abort();
}

template <class... Args>
[[noreturn]] inline void panic_fmt(const char *file, int line, const char *func, std::format_string<Args...> fmt, Args &&...args) noexcept
{
    std::print(stderr, "[PANIC] {}:{} ({}): ", file, line, func);
    std::print(stderr, fmt, std::forward<Args>(args)...);
    std::fputc('\n', stderr);
    std::abort();
}

} // namespace DSEngine::Util

// unified macro
#define PANIC(...)                                                                  \
    do                                                                              \
    {                                                                               \
        if constexpr (sizeof(#__VA_ARGS__) == 1)                                    \
        {                                                                           \
            ::DSEngine::Util::panic_base(__FILE__, __LINE__, __func__);             \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            ::DSEngine::Util::panic_fmt(__FILE__, __LINE__, __func__, __VA_ARGS__); \
        }                                                                           \
    } while (0)

#define DS_ASSERT(cond)                                                                             \
    do                                                                                              \
    {                                                                                               \
        if (!(cond))                                                                                \
        {                                                                                           \
            ::DSEngine::Util::panic_base(__FILE__, __LINE__, __func__, "Assertion failed: " #cond); \
        }                                                                                           \
    } while (0)