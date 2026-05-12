#pragma once

#include "pch.hpp"

#include "render/InstancedSpriteBatch.hpp"

namespace game::resources { class ResourceSystem; }
namespace game::terrain { class PlanetTerrain; }

namespace game::render
{
    class ResourceRenderer final
    {
    public:
        ResourceRenderer() = default;
        ~ResourceRenderer();

        ResourceRenderer(const ResourceRenderer&)                = delete;
        ResourceRenderer& operator=(const ResourceRenderer&)     = delete;
        ResourceRenderer(ResourceRenderer&&) noexcept            = default;
        ResourceRenderer& operator=(ResourceRenderer&&) noexcept = default;

        Result<void> initialize_assets();
        void         destroy_graphics_resources();
        void         draw_ores(
            const sf::View&                  view,
            const terrain::PlanetTerrain&    terrain,
            const resources::ResourceSystem& resources);

    private:
        Result<void> rebuild_ore_instances(
            const terrain::PlanetTerrain&    terrain,
            const resources::ResourceSystem& resources);

        InstancedSpriteBatch        ore_batch_{};
        std::vector<SpriteInstance> cached_instances_{};
        std::uint64_t               last_nodes_revision_{ std::numeric_limits<std::uint64_t>::max() };
        bool                        ore_instances_uploaded_{ false };
    };
}
