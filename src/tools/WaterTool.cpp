#include "pch.hpp"
#include "tools/WaterTool.hpp"

#include "gfx/MeshBuilders.hpp"
#include "tools/TerrainTargetResolver.hpp"

namespace game::tools
{
	void WaterTool::deactivate()
	{
		cancel_placement();
	}

	void WaterTool::update(const TerrainToolContext& /*context*/, const TerrainTargetResolver& /*resolver*/, const float /*dt*/)
	{
	}

	void WaterTool::handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
		const MouseButton button)
	{
		if (context.terrain == nullptr) return;

		if (button == MouseButton::Right)
		{
			if (!placement_mode_) begin_placement();
			else confirm_placement(context, resolver);
			return;
		}

		if (button != MouseButton::Left) return;
		if (placement_mode_)
		{
			cancel_placement();
			return;
		}

		collect_water(context, resolver);
	}

	void WaterTool::begin_placement()
	{
		placement_mode_ = true;
		desired_place_amount_ = current_amount_;
	}

	void WaterTool::confirm_placement(const TerrainToolContext& context, const TerrainTargetResolver& resolver)
	{
		if (current_amount_ == 0u || desired_place_amount_ == 0u)
		{
			cancel_placement();
			return;
		}

		const auto world_position = resolver.water_tool_target_world_position(context);
		if (!world_position.has_value()) return;

		const auto placed = context.terrain->place_water(*world_position, std::min(desired_place_amount_, current_amount_));
		if (placed == 0u) return;

		current_amount_ -= std::min(current_amount_, placed);
		cancel_placement();
	}

	void WaterTool::collect_water(const TerrainToolContext& context, const TerrainTargetResolver& resolver)
	{
		const auto free_space = current_capacity() - std::min(current_amount_, current_capacity());
		if (free_space == 0u) return;

		const auto world_position = resolver.water_tool_target_world_position(context);
		if (!world_position.has_value()) return;

		const auto picked_up = context.terrain->pickup_water(*world_position, free_space);
		current_amount_ = std::min(current_capacity(), current_amount_ + picked_up);
	}

	void WaterTool::adjust_placement_amount(const float delta)
	{
		if (!placement_mode_ || delta == 0.0f) return;

		const int direction = delta > 0.0f ? 1 : -1;
		const int next_value = std::clamp(
			static_cast<int>(desired_place_amount_) + direction,
			0,
			static_cast<int>(current_amount_));
		desired_place_amount_ = static_cast<std::uint32_t>(next_value);
	}

	void WaterTool::upgrade()
	{
		if (tier_index_ + 1u >= 3u) return;
		++tier_index_;
	}

	void WaterTool::fill_to_capacity()
	{
		current_amount_ = current_capacity();
		if (placement_mode_) desired_place_amount_ = current_amount_;
	}

	void WaterTool::cancel_placement()
	{
		placement_mode_ = false;
		desired_place_amount_ = 0u;
	}

	void WaterTool::draw_world_preview(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
		const sf::View& view) const
	{
		if (!placement_mode_ || context.terrain == nullptr || desired_place_amount_ == 0u) return;

		const auto target_position = resolver.water_tool_target_world_position(context);
		if (!target_position.has_value()) return;

		const auto preview = context.terrain->build_water_preview_mesh(*target_position, desired_place_amount_);
		if (!preview.has_value() || preview->future_vertices.empty() || preview->future_indices.empty()) return;

		gfx::Mesh current_mesh;
		gfx::Mesh future_mesh;

		const auto current_vertices = gfx::build_tinted_vertices(
			preview->current_vertices,
			{ 232, 248, 255, 128 });
		const auto future_vertices = gfx::build_tinted_vertices(
			preview->future_vertices,
			{ 214, 252, 255, 96 });

		if (!preview_renderable_.has_value())
		{
			preview_renderable_.emplace();
		}
		auto& preview_renderable = *preview_renderable_;

		current_mesh.set_data(current_vertices, preview->current_indices);
		future_mesh.set_data(future_vertices, preview->future_indices);

		glEnable(GL_STENCIL_TEST);
		glStencilMask(0xFF);
		glClear(GL_STENCIL_BUFFER_BIT);

		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glStencilFunc(GL_ALWAYS, 1, 0xFF);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		if (!current_mesh.empty()) preview_renderable.draw(current_mesh, view);

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glStencilMask(0x00);
		glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		preview_renderable.draw(future_mesh, view);

		glStencilMask(0xFF);
		glDisable(GL_STENCIL_TEST);
	}

	bool WaterTool::is_placement_mode() const
	{
		return placement_mode_;
	}

	std::size_t WaterTool::tier_index() const
	{
		return tier_index_;
	}

	std::uint32_t WaterTool::current_amount() const
	{
		return current_amount_;
	}

	std::uint32_t WaterTool::current_capacity() const
	{
		return current_tier().capacity;
	}

	std::uint32_t WaterTool::desired_place_amount() const
	{
		return desired_place_amount_;
	}

	const WaterTool::BucketTier& WaterTool::current_tier() const
	{
		static constexpr std::array<BucketTier, 3> bucket_tiers{{
			BucketTier{ 24u },
			BucketTier{ 40u },
			BucketTier{ 64u }
		}};

		return bucket_tiers[std::min(tier_index_, bucket_tiers.size() - 1u)];
	}
}
