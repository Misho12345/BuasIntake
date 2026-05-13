#include "pch.hpp"

#include "world/World.hpp"

namespace game::world
{
    // world startup is annoyingly order sensitive
    // terrain has to exist before we can ask for a spawn point and the player has to spawn against the final planet center
    Result<void> World::initialize(const b2WorldId physics_world, const player::PlayerConfig& player_config)
    {
        destroy();
        if (!b2World_IsValid(physics_world)) return fail("World requires a valid Box2D world");

        terrain_.emplace(physics_world, resources_, vegetation_);
        if (const auto result = terrain_->initialize(); !result)
        {
            terrain_.reset();
            return fail("Failed to initialize planet terrain: {}", result.error().message);
        }

        const auto spawn = terrain_->spawn_point_from_top_center(
            player_config.capsule_half_height +
            player_config.spawn_air_clearance);

        if (const auto result = player_.create(physics_world, spawn, terrain_->planet_center(), player_config);
            !result)
        {
            terrain_.reset();
            return fail(result.error());
        }

        return {};
    }

    void World::destroy()
    {
        terrain_.reset();
        player_.destroy();
        resources_     = resources::ResourceSystem{};
        vegetation_    = vegetation::VegetationSystem{};
        water_         = water::WaterSystem{};
    }

    // flush terrain edits first so vegetation is not looking at stale ground data for this frame
    // if vegetation changes then terrain visuals get another pass right away because greenness is derived from the plant layout
    void World::update(const float dt)
    {
        if (!terrain_.has_value()) return;

        // flush terrain edits first so plants and resource state see the final field for this frame
        terrain_->flush_pending_edits();
        if (vegetation_.update(dt, *terrain_)) terrain_->rebuild_after_vegetation_change();
        resources_.update(dt);
    }

    void World::validate() const
    {
        if (!terrain_.has_value()) return;
        terrain_->validate();
        resources_.validate();
        vegetation_.validate();
    }
}
