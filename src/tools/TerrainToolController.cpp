#include "pch.hpp"
#include "tools/TerrainToolController.hpp"

#include "Input.hpp"

namespace game::tools
{
	void TerrainToolController::update(const TerrainToolContext& context, const float dt)
	{
		if (context.terrain == nullptr) return;

		sync_active_tool();
		active_tool_->update(context, target_resolver_, dt);
	}

	void TerrainToolController::handle_mouse_pressed(const TerrainToolContext& context, const MouseButton button)
	{
		if (context.terrain == nullptr) return;

		sync_active_tool();
		active_tool_->handle_mouse_pressed(context, target_resolver_, button);
	}

	void TerrainToolController::export_current_chunk_field(const TerrainToolContext& context) const
	{
		if (context.terrain == nullptr) return;

		const auto output_path = context.terrain->save_chunk_field_image(context.player_world_position);
		if (output_path.has_value())
		{
			std::println("Saved chunk field image to {}", output_path->string());
		}
		else
		{
			std::println(std::cerr, "Failed to save chunk field image");
		}
	}

	void TerrainToolController::sync_active_tool()
	{
		IToolStrategy* next_tool = is_water_modifier_active() ? static_cast<IToolStrategy*>(&water_tool_)
			: static_cast<IToolStrategy*>(&terrain_tool_);
		if (next_tool == active_tool_) return;

		active_tool_->deactivate();
		active_tool_ = next_tool;
		active_tool_->activate();
	}

	bool TerrainToolController::is_water_modifier_active() const
	{
		return Input::is_pressed(Key::LControl) || Input::is_pressed(Key::RControl);
	}
}
