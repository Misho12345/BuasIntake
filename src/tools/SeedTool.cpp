#include "pch.hpp"

#include "tools/SeedTool.hpp"

#include "terrain/PlanetTerrain.hpp"
#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
    namespace
    {
        void log_throttled_seed_warning(const std::string& message)
        {
            using clock = std::chrono::steady_clock;
            using namespace std::chrono_literals;

            static std::string       last_message;
            static clock::time_point last_logged_at{};

            const auto now = clock::now();
            if (message == last_message && now - last_logged_at < 2s) return;

            last_message   = message;
            last_logged_at = now;
            Log::warn("{}", message);
        }
    }

    void SeedTool::update(
        const TerrainToolContext& /*context*/,
        const TerrainTargetResolver& /*resolver*/,
        const float /*dt*/) {}

    void SeedTool::handle_mouse_pressed(
        const TerrainToolContext&    context,
        const TerrainTargetResolver& resolver,
        const MouseButton            button)
    {
        if (button != MouseButton::Left || context.terrain == nullptr) return;

        const auto world_position = resolver.terrain_tool_hit_world_position(context);
        if (!world_position.has_value()) return;

        if (const auto result = context.terrain->plant_seed(*world_position);
            !result)
            log_throttled_seed_warning(result.error().message);
    }
}
