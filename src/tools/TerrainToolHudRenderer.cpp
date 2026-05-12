#include "pch.hpp"

#include "tools/TerrainToolHudRenderer.hpp"

namespace game::tools
{
    namespace
    {
        void draw_panel(
            sf::RenderTarget& target,
            const vec2        position,
            const vec2        size,
            const sf::Color   fill,
            const sf::Color   outline,
            const float       outline_thickness = 1.5f)
        {
            sf::RectangleShape shape{ size };
            shape.setPosition(position);
            shape.setFillColor(fill);
            shape.setOutlineColor(outline);
            shape.setOutlineThickness(outline_thickness);
            target.draw(shape);
        }

        void draw_slot_meter(
            sf::RenderTarget&          target,
            const vec2                 position,
            const vec2                 size,
            const float                ratio,
            const sf::Color            fill_color,
            const sf::Color            frame_color,
            const sf::Color            background_color,
            const std::optional<float> overlay_ratio = std::nullopt,
            const sf::Color            overlay_color = sf::Color::Transparent)
        {
            sf::RectangleShape frame{ size };
            frame.setPosition(position);
            frame.setFillColor(background_color);
            frame.setOutlineColor(frame_color);
            frame.setOutlineThickness(1.5f);
            target.draw(frame);

            const vec2 inner_size{
                std::max(size.x - 4.0f, 0.0f),
                std::max(size.y - 4.0f, 0.0f)
            };

            const vec2 inner_position{
                position.x + 2.0f,
                position.y + 2.0f
            };

            sf::RectangleShape gutter{ inner_size };
            gutter.setPosition(inner_position);
            gutter.setFillColor(0x0F141CD2_rgba);
            target.draw(gutter);

            const float clamped_ratio = std::clamp(ratio, 0.0f, 1.0f);
            if (clamped_ratio > 0.0f)
            {
                const float        fill_width = inner_size.x * clamped_ratio;
                sf::RectangleShape fill{ { fill_width, inner_size.y } };
                fill.setPosition(inner_position);
                fill.setFillColor(fill_color);
                target.draw(fill);
            }

            if (!overlay_ratio.has_value()) return;

            const float clamped_overlay = std::clamp(*overlay_ratio, 0.0f, clamped_ratio);
            if (clamped_overlay <= 0.0f) return;

            const float        overlay_width = inner_size.x * clamped_overlay;
            sf::RectangleShape overlay{ { overlay_width, std::max(inner_size.y - 2.0f, 0.0f) } };
            overlay.setPosition({ inner_position.x, inner_position.y + 1.0f });
            overlay.setFillColor(overlay_color);
            target.draw(overlay);
        }
    }

    void TerrainToolHudRenderer::draw(
        sf::RenderTarget&                             target,
        const std::span<const TerrainToolHudSlotData> slots)
    {
        if (slots.empty()) return;

        const auto      target_size     = target.getSize();

        const float     width           = static_cast<float>(target_size.x);
        const float     height          = static_cast<float>(target_size.y);

        constexpr float slot_size       = 72.0f;
        constexpr float slot_gap        = 9.0f;
        constexpr float panel_padding_x = 10.0f;
        constexpr float panel_padding_y = 10.0f;
        constexpr float meter_height    = 9.0f;

        const float hotbar_width =
                panel_padding_x * 2.0f +
                slot_size * static_cast<float>(slots.size()) +
                slot_gap * static_cast<float>(slots.size() - 1u);

        const float hotbar_height = panel_padding_y * 2.0f + slot_size;

        const vec2  hotbar_position{
            width - hotbar_width - 22.0f,
            height - hotbar_height - 22.0f
        };

        draw_panel(
            target,
            hotbar_position,
            { hotbar_width, hotbar_height },
            0x0D141CE4_rgba,
            0x3F5261EC_rgba,
            2.0f);

        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const auto& slot = slots[i];
            const vec2  slot_position{
                hotbar_position.x + panel_padding_x + static_cast<float>(i) * (slot_size + slot_gap),
                hotbar_position.y + panel_padding_y
            };
            const sf::Color frame = slot.selected ? 0xF8DD89FF_rgba : 0x495C6BDC_rgba;
            const sf::Color fill  = slot.selected ? 0x2A3743F4_rgba : 0x111922E4_rgba;

            if (slot.selected)
            {
                sf::RectangleShape glow{ { slot_size + 8.0f, slot_size + 8.0f } };
                glow.setPosition({ slot_position.x - 4.0f, slot_position.y - 4.0f });
                glow.setFillColor(0xF7DD8E1C_rgba);
                glow.setOutlineColor(0xF7DD8E5A_rgba);
                glow.setOutlineThickness(1.0f);
                target.draw(glow);
            }

            draw_panel(target, slot_position, { slot_size, slot_size }, fill, frame, 2.0f);

            if (slot.selected)
            {
                sf::RectangleShape accent{ { slot_size - 12.0f, 3.0f } };
                accent.setPosition({ slot_position.x + 6.0f, slot_position.y + 6.0f });
                accent.setFillColor(0xF7DD8EDC_rgba);
                target.draw(accent);
            }

            if (slot.texture != nullptr)
            {
                sf::Sprite icon{ *slot.texture, slot.icon_rect };

                const auto bounds = icon.getLocalBounds();
                icon.setOrigin(bounds.getCenter());

                const float scale = std::min(48.0f / bounds.size.x, 48.0f / bounds.size.y);
                icon.setScale({ scale, scale });
                icon.setColor(slot.selected ? sf::Color::White : 0xCDDADEDC_rgba);
                icon.setPosition({ slot_position.x + slot_size * 0.5f, slot_position.y + slot_size * 0.47f });

                target.draw(icon);
            }

            if (slot.show_bar)
            {
                draw_slot_meter(
                    target,
                    { slot_position.x + 8.0f, slot_position.y + slot_size - 14.0f },
                    { slot_size - 16.0f, meter_height },
                    slot.fill_ratio,
                    slot.bar_fill,
                    slot.bar_frame,
                    slot.bar_background,
                    slot.overlay_ratio,
                    0xBFF5FFD2_rgba);
            }

            if (slot.show_aim_ring)
            {
                sf::RectangleShape aim_ring{ { slot_size - 12.0f, slot_size - 12.0f } };

                aim_ring.setPosition({ slot_position.x + 6.0f, slot_position.y + 6.0f });
                aim_ring.setFillColor(sf::Color::Transparent);
                aim_ring.setOutlineThickness(1.5f);
                aim_ring.setOutlineColor(0xC2F4FFDC_rgba);

                target.draw(aim_ring);
            }
        }
    }
}
