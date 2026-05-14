#include "pch.hpp"

#include "terrain/TerrainBrushController.hpp"

namespace game::terrain
{
    TerrainBrushController::ApplyOutcome TerrainBrushController::apply_ground_brush(
        const TerrainField&                      field,
        const std::size_t                        chunk_count,
        const TerrainEdit&                       edit,
        const std::uint32_t                      unit_budget,
        const std::optional<GroundBrushBlocker>& blocker,
        const ApplyEditCallback&                 apply_edit)
    {
        if (field.empty() || unit_budget == 0u || !apply_edit) return {};
        if (pending_dirty_chunks_.empty()) pending_dirty_chunks_.assign(chunk_count, false);

        const bool had_pending_changes = !pending_changed_coords_.empty();

        const auto result = apply_edit(
            edit,
            pending_dirty_chunks_,
            pending_changed_coords_,
            unit_budget,
            blocker);

        if (!result.changed) return {};

        pending_changed_water_ = pending_changed_water_ || result.water_changed;
        pending_requires_wetness_rebuild_ = pending_requires_wetness_rebuild_ || result.requires_wetness_rebuild;

        if (!had_pending_changes) pending_rebuild_delay_frames_ = 1;

        return {
            .units         = result.units,
            .changed       = true,
            .water_changed = result.water_changed
        };
    }

    void TerrainBrushController::flush_pending_edits(const FlushCallbacks& callbacks)
    {
        flush_pending_ground_brush_changes(callbacks);
        flush_deferred_ground_brush_wetness(callbacks);
    }

    bool TerrainBrushController::has_pending_dirty_chunk_size_mismatch(const std::size_t chunk_count) const
    {
        return !pending_dirty_chunks_.empty() && pending_dirty_chunks_.size() != chunk_count;
    }

    std::size_t TerrainBrushController::pending_dirty_chunk_count() const
    {
        return pending_dirty_chunks_.size();
    }

    void TerrainBrushController::flush_pending_ground_brush_changes(const FlushCallbacks& callbacks)
    {
        if (pending_changed_coords_.empty() || pending_dirty_chunks_.empty()) return;

        if (pending_rebuild_delay_frames_ > 0)
        {
            --pending_rebuild_delay_frames_;
            return;
        }

        if (pending_requires_wetness_rebuild_)
        {
            deferred_wetness_coords_.insert(
                deferred_wetness_coords_.end(),
                pending_changed_coords_.begin(),
                pending_changed_coords_.end());
            deferred_wetness_delay_frames_ = 2;
        }

        if (callbacks.rebuild_pending)
        {
            if (const auto rebuild_result = callbacks.rebuild_pending(pending_dirty_chunks_, pending_changed_water_);
                !rebuild_result)
                Log::error(rebuild_result.error());
        }

        if (callbacks.refresh_surface_attachments)
        {
            callbacks.refresh_surface_attachments(pending_changed_coords_);
        }

        pending_changed_coords_.clear();
        std::fill(pending_dirty_chunks_.begin(), pending_dirty_chunks_.end(), false);
        pending_changed_water_            = false;
        pending_requires_wetness_rebuild_ = false;
        pending_rebuild_delay_frames_     = 0;
    }

    void TerrainBrushController::flush_deferred_ground_brush_wetness(const FlushCallbacks& callbacks)
    {
        if (deferred_wetness_coords_.empty()) return;
        if (!pending_changed_coords_.empty()) return;
        if (deferred_wetness_delay_frames_ > 0)
        {
            --deferred_wetness_delay_frames_;
            return;
        }

        if (!callbacks.make_dirty_chunks || !callbacks.recompute_wetness || !callbacks.rebuild_deferred_wetness) return;

        auto dirty_chunks = callbacks.make_dirty_chunks();
        callbacks.recompute_wetness(deferred_wetness_coords_, dirty_chunks);
        if (const auto rebuild_result = callbacks.rebuild_deferred_wetness(dirty_chunks);
            !rebuild_result)
            Log::error(rebuild_result.error());

        deferred_wetness_coords_.clear();
    }
}
