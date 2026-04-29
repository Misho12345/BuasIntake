#pragma once

#include "pch.hpp"

namespace game::tools
{
	struct TerrainToolHudSlotData final
	{
		const sf::Texture* texture{ nullptr };
		sf::IntRect icon_rect{};
		bool selected{ false };
		bool show_bar{ false };
		float fill_ratio{ 0.0f };
		std::optional<float> overlay_ratio{ std::nullopt };
		sf::Color bar_fill{ sf::Color::Transparent };
		sf::Color bar_frame{ sf::Color::Transparent };
		sf::Color bar_background{ sf::Color::Transparent };
		bool show_aim_ring{ false };
	};

	class TerrainToolHudRenderer final
	{
	public:
		static void draw(sf::RenderTarget& target, std::span<const TerrainToolHudSlotData> slots);
	};
}
