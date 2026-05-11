#include "pch.hpp"

#include "world/PlanetRestorationGoal.hpp"

namespace game::world
{
    bool PlanetRestorationGoal::update(const World& world)
    {
        if (completed_ || !world.ready()) return false;

        const auto& terrain = world.terrain();
        const std::uint64_t field_revision = terrain.field_revision();
        if (field_revision == last_field_revision_) return false;

        last_field_revision_ = field_revision;
        green_surface_coverage_ = terrain.green_surface_coverage();
        if (green_surface_coverage_ < required_green_surface_coverage_) return false;

        completed_ = true;
        return true;
    }

    float PlanetRestorationGoal::progress() const noexcept
    {
        return std::clamp(green_surface_coverage_ / required_green_surface_coverage_, 0.0f, 1.0f);
    }
}
