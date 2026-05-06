#pragma once

#include "pch.hpp"

#include <mutex>

#include "core/Log.hpp"

namespace game::core
{
#ifndef NDEBUG
    class ScopedProfiler final
    {
    public:
        explicit ScopedProfiler(const std::string_view name)
            : name_{ name }, start_{ std::chrono::steady_clock::now() } {}

        ~ScopedProfiler()
        {
            using clock = std::chrono::steady_clock;
            using milliseconds = std::chrono::duration<double, std::milli>;

            const double elapsed_ms = milliseconds(clock::now() - start_).count();

            static std::mutex mutex;
            static std::unordered_map<std::string, Stats> stats_by_name;

            std::scoped_lock lock{ mutex };
            auto& stats = stats_by_name[std::string{ name_ }];
            stats.total_ms += elapsed_ms;
            stats.max_ms = std::max(stats.max_ms, elapsed_ms);
            ++stats.samples;

            const auto now = clock::now();
            if (now - stats.last_report < std::chrono::seconds{ 5 } || stats.samples == 0u) return;

            const double average_ms = stats.total_ms / static_cast<double>(stats.samples);
            Log::info("[profile] {} avg={:.2f}ms max={:.2f}ms samples={}", name_, average_ms, stats.max_ms, stats.samples);
            stats.total_ms = 0.0;
            stats.max_ms = 0.0;
            stats.samples = 0u;
            stats.last_report = now;
        }

    private:
        struct Stats final
        {
            double total_ms{ 0.0 };
            double max_ms{ 0.0 };
            std::uint64_t samples{ 0u };
            std::chrono::steady_clock::time_point last_report{ std::chrono::steady_clock::now() };
        };

        std::string_view name_{};
        std::chrono::steady_clock::time_point start_{};
    };
#else
    class ScopedProfiler final
    {
    public:
        explicit ScopedProfiler(std::string_view /*name*/) {}
    };
#endif
}
