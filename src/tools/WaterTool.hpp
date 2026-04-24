#pragma once

#include "pch.hpp"

#include "gfx/Mesh.hpp"
#include "tools/ToolStrategy.hpp"
#include "water/WaterRenderable.hpp"

namespace game::tools
{
	class WaterTool final : public IToolStrategy
	{
	public:
		WaterTool() = default;
		~WaterTool() override = default;

		void deactivate() override;
		void update(const TerrainToolContext& context, const TerrainTargetResolver& resolver, float dt) override;
		void handle_mouse_pressed(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
			MouseButton button) override;

		void adjust_placement_amount(float delta);
		void upgrade();
		void fill_to_capacity();
		void cancel_placement();
		void draw_world_preview(const TerrainToolContext& context, const TerrainTargetResolver& resolver,
			const sf::View& view) const;

		[[nodiscard]] bool is_placement_mode() const;
		[[nodiscard]] std::size_t tier_index() const;
		[[nodiscard]] std::uint32_t current_amount() const;
		[[nodiscard]] std::uint32_t current_capacity() const;
		[[nodiscard]] std::uint32_t desired_place_amount() const;

	private:
		struct BucketTier final
		{
			std::uint32_t capacity{ 0u };
		};

		[[nodiscard]] const BucketTier& current_tier() const;

		std::size_t tier_index_{ 0u };
		std::uint32_t current_amount_{ 24u };
		bool placement_mode_{ false };
		std::uint32_t desired_place_amount_{ 0u };
		mutable std::optional<water::WaterRenderable> preview_renderable_{ std::nullopt };
	};
}
