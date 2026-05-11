#pragma once

#include "pch.hpp"

#include "world/World.hpp"

namespace game::world
{
    class PlanetRestorationGoal final
    {
    public:
        bool update(const World& world);

        [[nodiscard]] bool completed() const noexcept { return completed_; }
        [[nodiscard]] float progress() const noexcept;
        [[nodiscard]] float green_surface_coverage() const noexcept { return green_surface_coverage_; }

    private:
        static constexpr float required_green_surface_coverage_{ 0.82f };

        std::uint64_t last_field_revision_{ std::numeric_limits<std::uint64_t>::max() };
        float green_surface_coverage_{ 0.0f };
        bool completed_{ false };
    };
}
