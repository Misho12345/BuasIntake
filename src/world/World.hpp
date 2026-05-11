#pragma once

#include "pch.hpp"

#include "player/Player.hpp"
#include "resources/ResourceSystem.hpp"
#include "terrain/PlanetTerrain.hpp"
#include "vegetation/VegetationSystem.hpp"
#include "water/WaterSystem.hpp"

namespace game::world
{
    class World final
    {
    public:
        Result<void> initialize(b2WorldId physics_world, const player::PlayerConfig& player_config);
        void destroy();
        void update(float dt);
        void validate() const;

        bool ready() const noexcept { return terrain_.has_value() && player_.valid(); }
        player::Player& player() noexcept { return player_; }
        const player::Player& player() const noexcept { return player_; }
        terrain::PlanetTerrain& terrain() noexcept { return *terrain_; }
        const terrain::PlanetTerrain& terrain() const noexcept { return *terrain_; }
        water::WaterSystem& water() noexcept { return water_; }
        const water::WaterSystem& water() const noexcept { return water_; }
        resources::ResourceSystem& resources() noexcept { return resources_; }
        const resources::ResourceSystem& resources() const noexcept { return resources_; }
        vegetation::VegetationSystem& vegetation() noexcept { return vegetation_; }
        const vegetation::VegetationSystem& vegetation() const noexcept { return vegetation_; }

    private:
        b2WorldId physics_world_{ b2_nullWorldId };
        resources::ResourceSystem resources_{};
        vegetation::VegetationSystem vegetation_{};
        water::WaterSystem water_{};
        std::optional<terrain::PlanetTerrain> terrain_{ std::nullopt };
        player::Player player_{};
    };
}
