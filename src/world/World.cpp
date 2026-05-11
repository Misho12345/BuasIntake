#include "pch.hpp"

#include "world/World.hpp"

namespace game::world
{
    Result<void> World::initialize(const b2WorldId physics_world, const player::PlayerConfig& player_config)
    {
        destroy();
        if (!b2World_IsValid(physics_world)) return fail("World requires a valid Box2D world");

        physics_world_ = physics_world;
        terrain_.emplace(physics_world_, resources_, vegetation_);
        if (const auto terrain_result = terrain_->initialize(); !terrain_result)
        {
            terrain_.reset();
            return fail("Failed to initialize planet terrain: {}", terrain_result.error().message);
        }

        const auto spawn = terrain_->spawn_point_from_top_center(player_config.capsule_half_height + player_config.spawn_air_clearance);
        if (const auto player_result = player_.create(physics_world_, spawn, terrain_->planet_center(), player_config); !player_result)
        {
            terrain_.reset();
            return fail(player_result.error());
        }

        return {};
    }

    void World::destroy()
    {
        terrain_.reset();
        player_.destroy();
        resources_ = resources::ResourceSystem{};
        vegetation_ = vegetation::VegetationSystem{};
        water_ = water::WaterSystem{};
        physics_world_ = b2_nullWorldId;
    }

    void World::update(const float dt)
    {
        if (!terrain_.has_value())
            return;

        // Flush terrain edits first so plants and resource state see the final field for this frame.
        terrain_->flush_pending_edits();
        if (vegetation_.update(dt, *terrain_))
            terrain_->rebuild_after_vegetation_change();
        resources_.update(dt);
    }

    void World::validate() const
    {
        if (!terrain_.has_value())
            return;
        terrain_->validate();
        resources_.validate();
        vegetation_.validate();
        water_.validate();
    }
}
