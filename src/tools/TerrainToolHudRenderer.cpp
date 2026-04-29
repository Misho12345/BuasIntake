#include "pch.hpp"
#include "tools/TerrainToolHudRenderer.hpp"

namespace game::tools
{
	namespace
	{
		void draw_panel(
			sf::RenderTarget& target,
			const sf::Vector2f position,
			const sf::Vector2f size,
			const sf::Color fill,
			const sf::Color outline,
			const float outline_thickness = 1.5f)
		{
			sf::RectangleShape shape{ size };
			shape.setPosition(position);
			shape.setFillColor(fill);
			shape.setOutlineColor(outline);
			shape.setOutlineThickness(outline_thickness);
			target.draw(shape);
		}

		void draw_vertical_bar(
			sf::RenderTarget& target,
			const sf::Vector2f position,
			const sf::Vector2f size,
			const float ratio,
			const sf::Color fill_color,
			const sf::Color frame_color,
			const sf::Color background_color,
			const std::optional<float> overlay_ratio = std::nullopt,
			const sf::Color overlay_color = sf::Color::Transparent)
		{
			sf::RectangleShape frame{ size };
			frame.setPosition(position);
			frame.setFillColor(background_color);
			frame.setOutlineColor(frame_color);
			frame.setOutlineThickness(1.5f);
			target.draw(frame);

			const sf::Vector2f inner_size{
				std::max(size.x - 4.0f, 0.0f),
				std::max(size.y - 4.0f, 0.0f)
			};
			const sf::Vector2f inner_position{ position.x + 2.0f, position.y + 2.0f };

			sf::RectangleShape gutter{ inner_size };
			gutter.setPosition(inner_position);
			gutter.setFillColor(sf::Color(15, 20, 28, 210));
			target.draw(gutter);

			const float clamped_ratio = std::clamp(ratio, 0.0f, 1.0f);
			if (clamped_ratio > 0.0f)
			{
				const float fill_height = inner_size.y * clamped_ratio;
				sf::RectangleShape fill{ { inner_size.x, fill_height } };
				fill.setPosition({ inner_position.x, inner_position.y + inner_size.y - fill_height });
				fill.setFillColor(fill_color);
				target.draw(fill);
			}

			if (!overlay_ratio.has_value()) return;

			const float clamped_overlay = std::clamp(*overlay_ratio, 0.0f, clamped_ratio);
			if (clamped_overlay <= 0.0f) return;

			const float overlay_height = inner_size.y * clamped_overlay;
			sf::RectangleShape overlay{
				{ std::max(inner_size.x - 4.0f, 0.0f), overlay_height }
			};
			overlay.setPosition({ inner_position.x + 2.0f, inner_position.y + inner_size.y - overlay_height });
			overlay.setFillColor(overlay_color);
			target.draw(overlay);
		}
	}

	void TerrainToolHudRenderer::draw(sf::RenderTarget& target, const std::span<const TerrainToolHudSlotData> slots)
	{
		if (slots.empty()) return;

		const auto target_size = target.getSize();
		const float width = static_cast<float>(target_size.x);
		const float height = static_cast<float>(target_size.y);
		const float slot_size = 74.0f;
		const float slot_gap = 12.0f;
		const float panel_padding_x = 14.0f;
		const float panel_padding_y = 14.0f;
		const float bar_width = 22.0f;
		const float bar_height = 108.0f;
		const float bar_gap = 10.0f;
		const float hotbar_width =
			panel_padding_x * 2.0f +
			slot_size * static_cast<float>(slots.size()) +
			slot_gap * static_cast<float>(slots.size() - 1u);
		const float hotbar_height = panel_padding_y * 2.0f + slot_size + bar_gap + bar_height;
		const sf::Vector2f hotbar_position{
			width - hotbar_width - 22.0f,
			height - hotbar_height - 22.0f
		};

		draw_panel(
			target,
			hotbar_position,
			{ hotbar_width, hotbar_height },
			sf::Color(13, 20, 28, 228),
			sf::Color(63, 82, 97, 236),
			2.0f);

		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			const auto& slot = slots[i];
			const sf::Vector2f slot_position{
				hotbar_position.x + panel_padding_x + static_cast<float>(i) * (slot_size + slot_gap),
				hotbar_position.y + panel_padding_y + bar_height + bar_gap
			};
			const sf::Vector2f bar_position{
				slot_position.x + (slot_size - bar_width) * 0.5f,
				hotbar_position.y + panel_padding_y
			};
			const sf::Color frame = slot.selected ? sf::Color(247, 221, 142, 255) : sf::Color(72, 90, 103, 230);
			const sf::Color fill = slot.selected ? sf::Color(50, 63, 73, 245) : sf::Color(26, 36, 47, 235);

			if (slot.show_bar)
			{
				draw_vertical_bar(
					target,
					bar_position,
					{ bar_width, bar_height },
					slot.fill_ratio,
					slot.bar_fill,
					slot.bar_frame,
					slot.bar_background,
					slot.overlay_ratio,
					sf::Color(191, 245, 255, 210));
			}

			draw_panel(target, slot_position, { slot_size, slot_size }, fill, frame, 2.0f);

			if (slot.texture != nullptr)
			{
				sf::Sprite icon{ *slot.texture, slot.icon_rect };
				const auto bounds = icon.getLocalBounds();
				icon.setOrigin(bounds.getCenter());
				const float scale = std::min(46.0f / bounds.size.x, 46.0f / bounds.size.y);
				icon.setScale({ scale, scale });
				icon.setPosition({ slot_position.x + slot_size * 0.5f, slot_position.y + slot_size * 0.5f });
				target.draw(icon);
			}

			if (slot.show_aim_ring)
			{
				sf::RectangleShape aim_ring{ { slot_size - 12.0f, slot_size - 12.0f } };
				aim_ring.setPosition({ slot_position.x + 6.0f, slot_position.y + 6.0f });
				aim_ring.setFillColor(sf::Color::Transparent);
				aim_ring.setOutlineThickness(1.5f);
				aim_ring.setOutlineColor(sf::Color(194, 244, 255, 220));
				target.draw(aim_ring);
			}
		}
	}
}
