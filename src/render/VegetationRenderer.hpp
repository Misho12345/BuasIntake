#pragma once

#include "pch.hpp"

#include "render/InstancedSpriteBatch.hpp"

namespace game::resources { class ResourceSystem; }
namespace game::terrain { class PlanetTerrain; }
namespace game::vegetation { class VegetationSystem; }

namespace game::render
{
    class VegetationRenderer final
    {
    public:
        VegetationRenderer()  = default;
        ~VegetationRenderer() = default;

        VegetationRenderer(const VegetationRenderer&)                = delete;
        VegetationRenderer& operator=(const VegetationRenderer&)     = delete;
        VegetationRenderer(VegetationRenderer&&) noexcept            = default;
        VegetationRenderer& operator=(VegetationRenderer&&) noexcept = default;

        // vegetation gets split into a few sprite batches up front because the actual draw path is just cull upload draw
        Result<void> initialize_assets();
        void         destroy_graphics_resources();

        void draw(
            const sf::View&                     view,
            const terrain::PlanetTerrain&       terrain,
            const vegetation::VegetationSystem& vegetation,
            const resources::ResourceSystem&    resources);

    private:
        static constexpr std::size_t batch_count{ 4u };

        // this rebuilds all sprite instances from plant and dead plant state when the source revisions changed
        void rebuild_instances(
            const terrain::PlanetTerrain&       terrain,
            const vegetation::VegetationSystem& vegetation,
            const resources::ResourceSystem&    resources);

        std::array<InstancedSpriteBatch, batch_count> batch_resources_{};

        std::vector<SpriteInstance> cached_live64_instances_{};
        std::vector<SpriteInstance> cached_low_cover_live32_instances_{};
        std::vector<SpriteInstance> cached_woody_live32_instances_{};
        std::vector<SpriteInstance> cached_dead32_instances_{};
        std::vector<SpriteInstance> cached_dead64_instances_{};

        std::vector<SpriteInstance> visible_live64_instances_{};
        std::vector<SpriteInstance> visible_woody_live32_instances_{};
        std::vector<SpriteInstance> visible_low_cover_live32_instances_{};
        std::vector<SpriteInstance> visible_dead32_instances_{};
        std::vector<SpriteInstance> visible_dead64_instances_{};

        std::uint64_t last_vegetation_revision_{ std::numeric_limits<std::uint64_t>::max() };
        std::uint64_t last_resource_revision_{ std::numeric_limits<std::uint64_t>::max() };
    };
}
