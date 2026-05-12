#pragma once

#include "pch.hpp"

#include "player/Player.hpp"
#include "resources/ResourceSystem.hpp"
#include "terrain/PlanetTerrain.hpp"
#include "vegetation/VegetationSystem.hpp"
#include "water/WaterSystem.hpp"

namespace game::world
{
    // groups the live gameplay systems that need to know about each other during a run
    // game code talks through this instead of passing terrain player resources water and vegetation separately
    class World final
    {
    public:
        // this wires terrain player water resources and vegetation together in the only order that really makes sense
        Result<void> initialize(b2WorldId physics_world, const player::PlayerConfig& player_config);

        void destroy();
        // this is the world level update order for the frame after terrain edits and before render code looks at the result
        void update(float dt);
        void validate() const;

        bool ready() const { return terrain_.has_value() && player_.valid(); }

        // these are the shared access points the rest of the game uses once the world is live
        auto& player(this auto& self) { return self.player_; }
        auto& terrain(this auto& self) { return *self.terrain_; }
        auto& water(this auto& self) { return self.water_; }
        auto& resources(this auto& self) { return self.resources_; }
        auto& vegetation(this auto& self) { return self.vegetation_; }

    private:
        resources::ResourceSystem             resources_{};
        vegetation::VegetationSystem          vegetation_{};
        water::WaterSystem                    water_{};
        std::optional<terrain::PlanetTerrain> terrain_{ std::nullopt };
        player::Player                        player_{};
    };
}
