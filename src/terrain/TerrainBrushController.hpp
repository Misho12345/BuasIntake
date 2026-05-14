#pragma once

#include "pch.hpp"

#include "terrain/TerrainBrushSystem.hpp"
#include "terrain/TerrainField.hpp"

namespace game::terrain
{
    class TerrainBrushController final
    {
    public:
        using TerrainEdit = TerrainGenerator::TerrainEdit;
        using EditResult  = TerrainBrushSystem::Result;

        struct ApplyOutcome final
        {
            std::uint32_t units{ 0u };
            bool          changed{ false };
            bool          water_changed{ false };
        };

        struct FlushCallbacks final
        {
            std::function<Result<void>(const std::vector<bool>&, bool)> rebuild_pending{};
            std::function<std::vector<bool>()>                          make_dirty_chunks{};
            std::function<void(const std::vector<ivec2>&, std::vector<bool>&)> recompute_wetness{};
            std::function<Result<void>(const std::vector<bool>&)>        rebuild_deferred_wetness{};
            std::function<void(const std::vector<ivec2>&)>               refresh_surface_attachments{};
        };

        using ApplyEditCallback = std::function<EditResult(
            const TerrainEdit&,
            std::vector<bool>&,
            std::vector<ivec2>&,
            std::uint32_t,
            const std::optional<GroundBrushBlocker>&)>;

        ApplyOutcome apply_ground_brush(
            const TerrainField&                     field,
            std::size_t                             chunk_count,
            const TerrainEdit&                      edit,
            std::uint32_t                           unit_budget,
            const std::optional<GroundBrushBlocker>& blocker,
            const ApplyEditCallback&                apply_edit);

        void flush_pending_edits(const FlushCallbacks& callbacks);

        bool        has_pending_dirty_chunk_size_mismatch(std::size_t chunk_count) const;
        std::size_t pending_dirty_chunk_count() const;

    private:
        void flush_pending_ground_brush_changes(const FlushCallbacks& callbacks);
        void flush_deferred_ground_brush_wetness(const FlushCallbacks& callbacks);

        std::vector<ivec2> pending_changed_coords_{};
        std::vector<bool>  pending_dirty_chunks_{};

        bool pending_changed_water_{ false };
        bool pending_requires_wetness_rebuild_{ false };
        int  pending_rebuild_delay_frames_{ 0 };

        std::vector<ivec2> deferred_wetness_coords_{};
        int                deferred_wetness_delay_frames_{ 0 };
    };
}
