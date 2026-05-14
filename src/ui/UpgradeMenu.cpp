#include "pch.hpp"

#include "ui/UpgradeMenu.hpp"

namespace game::ui
{
    namespace
    {
        constexpr vec2  preferred_upgrade_panel_size{ 1160.0f, 610.0f };
        constexpr float upgrade_menu_margin = 16.0f;

        struct UpgradeMenuLayout final
        {
            vec2  panel_position{};
            vec2  panel_size{};
            float scale{ 1.0f };
        };

        UpgradeMenuLayout make_upgrade_menu_layout(const uvec2 target_size)
        {
            const vec2 target_extent = static_cast<vec2>(target_size);
            const vec2 available     = vec2_each(target_extent - upgrade_menu_margin * 2.0f, [](const float value, const float floor)
            {
                return std::max(value, floor);
            }, 1.0f);

            const float scale = std::min(min(available / preferred_upgrade_panel_size), 1.0f);

            const vec2 panel_size = preferred_upgrade_panel_size * scale;

            return {
                .panel_position = (target_extent - panel_size) * 0.5f,
                .panel_size = panel_size,
                .scale      = scale
            };
        }

        vec2 card_position_for(const UpgradeMenuLayout& layout, const std::size_t card_index)
        {
            return layout.panel_position + vec2{ 34.0f + static_cast<float>(card_index) * 550.0f, 108.0f } * layout.scale;
        }

        std::string roman_tier(const std::size_t level_index)
        {
            static constexpr std::array tiers{ "I", "II", "III" };
            return tiers[std::min(level_index, tiers.size() - 1zu)];
        }
    }

    void UpgradeMenu::draw(
        sf::RenderTarget&                  target,
        const sf::Font&                    font,
        const sf::Texture&                 tools_texture,
        const std::span<const UpgradeCard> cards) const
    {
        const auto target_size = target.getSize();
        const auto layout      = make_upgrade_menu_layout(target_size);

        const auto scaled = [&](const float value) { return value * layout.scale; };

        const auto panel_point = [&](const float x, const float y)
        {
            return layout.panel_position + vec2{ scaled(x), scaled(y) };
        };

        const auto scaled_size = [&](const float x, const float y) { return vec2{ scaled(x), scaled(y) }; };

        const auto scaled_thickness = [&](const float value) { return std::max(1.0f, scaled(value)); };

        sf::RectangleShape dim{ static_cast<vec2>(target_size) };

        dim.setFillColor(0x020508B0_rgba);
        target.draw(dim);

        sf::RectangleShape panel{ layout.panel_size };
        panel.setPosition(layout.panel_position);
        panel.setFillColor(0x0F1215FA_rgba);
        panel.setOutlineColor(0xD2A443FF_rgba);
        panel.setOutlineThickness(scaled_thickness(3.0f));
        target.draw(panel);

        sf::RectangleShape header_band{ { layout.panel_size.x, scaled(82.0f) } };
        header_band.setPosition(layout.panel_position);
        header_band.setFillColor(0x231D14BE_rgba);
        target.draw(header_band);

        auto draw_text = [&](
            const std::string& value,
            const vec2         position,
            const unsigned int size,
            const sf::Color    color,
            const bool         centered = false,
            const bool         bold     = false)
        {
            const auto scaled_character_size = static_cast<std::uint32_t>(std::max(
                12.0f,
                std::round(static_cast<float>(size) * layout.scale)));

            sf::Text text{ font, value, scaled_character_size };

            if (bold) text.setStyle(sf::Text::Bold);
            text.setFillColor(color);
            text.setOutlineColor(0x03080CD2_rgba);
            text.setOutlineThickness(size >= 20u
                                         ? std::max(1.0f, scaled(1.7f))
                                         : std::max(0.75f, scaled(1.0f)));

            if (centered)
            {
                text.setOrigin(text.getLocalBounds().getCenter());
            }

            text.setPosition(position);
            target.draw(text);
        };

        auto draw_roman_badge = [&](const vec2 center, const std::size_t level_index)
        {
            draw_text(roman_tier(level_index), center + 2.0f, 28u, 0x000000BE_rgba, true, true);
            draw_text(roman_tier(level_index), center, 28u, 0xFFDF5BFF_rgba, true, true);
        };

        auto draw_arrow = [&](
            const vec2  center,
            const float shaft_length,
            const float shaft_height,
            const float head_length,
            const float head_half_height)
        {
            sf::RectangleShape shaft{ { shaft_length, shaft_height } };
            shaft.setOrigin(shaft.getSize() * 0.5f);
            shaft.setPosition(center + vec2{ -head_length * 0.38f, 0.0f });
            shaft.setFillColor(0xEBBE4AFF_rgba);
            target.draw(shaft);

            sf::ConvexShape head{ 3u };
            head.setPoint(0u, center + vec2{ shaft_length * 0.5f, 0.0f });
            head.setPoint(1u, center + vec2{ shaft_length * 0.5f - head_length, -head_half_height });
            head.setPoint(2u, center + vec2{ shaft_length * 0.5f - head_length, head_half_height });
            head.setFillColor(0xFFD352FF_rgba);
            head.setOutlineColor(0x52370CDC_rgba);
            head.setOutlineThickness(1.5f);
            target.draw(head);
        };

        auto draw_icon_box = [&](const sf::IntRect icon_rect, const vec2 center, const std::size_t level_index)
        {
            sf::RectangleShape shadow{ scaled_size(162.0f, 162.0f) };
            shadow.setOrigin(shadow.getSize() * 0.5f);
            shadow.setPosition(center + vec2{ scaled(4.0f), scaled(5.0f) });
            shadow.setFillColor(0x00000058_rgba);
            target.draw(shadow);

            sf::RectangleShape box{ scaled_size(162.0f, 162.0f) };
            box.setOrigin(box.getSize() * 0.5f);
            box.setPosition(center);
            box.setFillColor(0x192126FC_rgba);
            box.setOutlineColor(0x657F87F5_rgba);
            box.setOutlineThickness(scaled_thickness(2.5f));
            target.draw(box);

            sf::Sprite icon{ tools_texture, icon_rect };
            const auto bounds = icon.getLocalBounds();
            icon.setOrigin(bounds.getCenter());
            const float icon_scale = min(vec2{ scaled(108.0f), scaled(108.0f) } / bounds.size);
            icon.setScale({ icon_scale, icon_scale });
            icon.setPosition(center + vec2{ 0.0f, -scaled(9.0f) });
            target.draw(icon);
            draw_roman_badge(center + vec2{ 0.0f, scaled(63.0f) }, level_index);
        };

        draw_text(
            "Upgrade Menu",
            panel_point(preferred_upgrade_panel_size.x * 0.5f, 43.0f),
            46u, 0xFFE797FF_rgba,
            true, true);

        for (std::size_t index = 0; index < cards.size(); ++index)
        {
            const auto& [
                title,
                current_icon,
                current_level,
                next_icon,
                next_level,
                stats,
                cost_text,
                maxed
            ] = cards[index];

            const vec2         card_position = card_position_for(layout, index);
            sf::RectangleShape card{ scaled_size(525.0f, 458.0f) };
            card.setPosition(card_position);
            card.setFillColor(0x171D22F6_rgba);
            card.setOutlineColor(0x576869F5_rgba);
            card.setOutlineThickness(scaled_thickness(2.0f));
            target.draw(card);

            draw_text(
                title,
                card_position + vec2{ scaled(262.5f), scaled(33.0f) },
                34u, 0xF3F5E0FF_rgba,
                true, true);

            draw_icon_box(
                current_icon,
                card_position + vec2{ scaled(149.0f), scaled(139.0f) },
                current_level);

            draw_arrow(
                card_position + vec2{ scaled(262.5f), scaled(139.0f) },
                scaled(56.0f),
                scaled(7.0f),
                scaled(22.0f),
                scaled(12.0f));

            draw_icon_box(
                next_icon,
                card_position + vec2{ scaled(376.0f), scaled(139.0f) },
                next_level);

            const vec2 table_position = card_position + vec2{ scaled(40.0f), scaled(244.0f) };

            for (std::size_t row = 0u; row < stats.size(); ++row)
            {
                const float y = table_position.y + static_cast<float>(row) * scaled(48.0f);

                sf::RectangleShape row_background{ scaled_size(445.0f, 40.0f) };
                row_background.setPosition({ table_position.x, y - scaled(20.0f) });
                row_background.setFillColor(row % 2u == 0u ? 0x1F282DE8_rgba : 0x1B2227E8_rgba);
                target.draw(row_background);

                draw_text(
                    stats[row].label,
                    table_position + vec2{ scaled(70.0f), static_cast<float>(row) * scaled(48.0f) },
                    22u, 0xE0E9E2FF_rgba,
                    true, true);

                draw_text(
                    stats[row].current,
                    table_position + vec2{ scaled(210.0f), static_cast<float>(row) * scaled(48.0f) },
                    23u, 0xCDDADBFF_rgba,
                    true, true);

                draw_arrow(
                    table_position + vec2{ scaled(292.0f), static_cast<float>(row) * scaled(48.0f) },
                    scaled(42.0f),
                    scaled(5.0f),
                    scaled(15.0f),
                    scaled(8.0f));

                draw_text(
                    stats[row].next,
                    table_position + vec2{ scaled(386.0f), static_cast<float>(row) * scaled(48.0f) },
                    23u, 0xFFE270FF_rgba,
                    true, true);
            }

            const auto         action_button_rect = button_rect(target_size, index);
            sf::RectangleShape button{ action_button_rect.size };
            button.setPosition(action_button_rect.position);
            button.setFillColor(maxed ? 0x313435F5_rgba : 0xAC741DFC_rgba);
            button.setOutlineColor(maxed ? 0x676C6CE6_rgba : 0xFFDB5CFF_rgba);
            button.setOutlineThickness(scaled_thickness(2.0f));
            target.draw(button);

            draw_text(
                maxed ? "MAXED" : "UPGRADE",
                action_button_rect.getCenter() + vec2{ 0.0f, -scaled(7.0f) },
                24u,
                maxed ? 0xAEB5B5FF_rgba : 0xFFF2BEFF_rgba,
                true, true);

            draw_text(
                maxed ? "" : cost_text,
                action_button_rect.getCenter() + vec2{ 0.0f, scaled(16.0f) },
                17u,
                0xFFE28EFF_rgba,
                true, true);
        }
    }

    sf::FloatRect UpgradeMenu::button_rect(const uvec2 target_size, const std::size_t action_index)
    {
        const auto  layout        = make_upgrade_menu_layout(target_size);
        const float scale         = layout.scale;
        const vec2  card_position = card_position_for(layout, action_index);

        return {
            card_position + vec2{ 125.0f, 389.0f } * scale,
            vec2{ 275.0f, 58.0f } * scale
        };
    }
}
