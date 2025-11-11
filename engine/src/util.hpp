// engine/src/util.hpp
#pragma once
#include <cstdio>
#include <cstdlib>

namespace DSEngine::Util
{

[[noreturn]] inline void panic(const char *file, int line, const char *func, const char *msg = nullptr) noexcept
{
    if (msg)
        std::fprintf(stderr, "[PANIC] %s:%d (%s): %s\n", file, line, func, msg);
    else
        std::fprintf(stderr, "[PANIC] %s:%d (%s)\n", file, line, func);
    std::abort();
}

template <class... Args>
[[noreturn]] inline void panic_fmt(const char *file, int line, const char *func, const char *fmt, Args &&...args) noexcept
{
    std::fprintf(stderr, "[PANIC] %s:%d (%s): ", file, line, func);
    std::fprintf(stderr, fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "\n");
    std::abort();
}

} // namespace DSEngine::Util

#define PANIC() \
    ::DSEngine::Util::panic(__FILE__, __LINE__, __func__)

#define PANIC_MSG(...) \
    ::DSEngine::Util::panic_fmt(__FILE__, __LINE__, __func__, __VA_ARGS__)

#define DS_ASSERT(cond)                                                                        \
    do                                                                                         \
    {                                                                                          \
        if (!(cond))                                                                           \
        {                                                                                      \
            ::DSEngine::Util::panic(__FILE__, __LINE__, __func__, "Assertion failed: " #cond); \
        }                                                                                      \
    } while (0)