#pragma once

#include "pch.hpp"

#include "world/World.hpp"

namespace game::world
{
    class PlanetRestorationGoal final
    {
    public:
        bool update(const World& world);

        bool  completed() const { return completed_; }
        float progress() const;
        float green_surface_coverage() const { return green_surface_coverage_; }

    private:
        static constexpr float required_green_surface_coverage_{ 0.62f };

        std::uint64_t last_field_revision_{ std::numeric_limits<std::uint64_t>::max() };
        float         green_surface_coverage_{ 0.0f };
        bool          completed_{ false };
    };
}
