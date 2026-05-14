#include "pch.hpp"

#include "ui/InventoryHud.hpp"

#include "resources/ResourceSystem.hpp"
#include "ui/UiFont.hpp"

namespace game::ui
{
    namespace
    {
        sf::Color with_alpha(const sf::Color color, const std::uint8_t alpha)
        {
            return { color.r, color.g, color.b, alpha };
        }

        sf::IntRect tile_rect(const std::uint8_t column, const std::uint8_t row, const int tile_size)
        {
            return {
                { static_cast<int>(column) * tile_size, static_cast<int>(row) * tile_size },
                { tile_size, tile_size }
            };
        }

        sf::Text make_counter_text(
            const sf::Font&     font,
            const std::uint32_t value,
            const unsigned int  character_size)
        {
            sf::Text text{ font, std::format("{}", value), character_size };
            text.setFillColor(0xEEF4E5FF_rgba);
            text.setOutlineColor(0x080C10DC_rgba);
            text.setOutlineThickness(1.5f);
            return text;
        }
    }

    Result<void> InventoryHud::initialize_assets()
    {
        if (assets_ready_) return {};

        auto load_texture = [](sf::Texture& texture, const char* path) -> Result<void>
        {
            if (!texture.loadFromFile(path)) return fail("Failed to load texture '{}'", path);

            texture.setSmooth(false);
            return {};
        };

        TRY(load_texture(processed_resource_texture_, "assets/images/ores/processed_ores.png"));
        TRY(load_texture(seed_icon_texture_, "assets/images/vegetation/ground_plants.png"));

        assets_ready_ = true;
        return {};
    }

    void InventoryHud::destroy_graphics_resources()
    {
        processed_resource_texture_ = sf::Texture{};
        seed_icon_texture_          = sf::Texture{};
        assets_ready_               = false;
    }

    void InventoryHud::draw(sf::RenderTarget& target, const resources::HudState& hud_state) const
    {
        if (!assets_ready_) return;

        const auto  target_size   = target.getSize();
        const float icon_center_x = static_cast<float>(target_size.x) - 46.0f;
        const float row_height    = 44.0f;
        const float first_row_y   = 30.0f;

        struct CounterEntry final
        {
            resources::InventoryItem item{ resources::InventoryItem::Rock };
            const sf::Texture* texture{ nullptr };
            sf::IntRect        icon{};
            float              icon_target_size{ 38.0f };
        };

        const std::array entries{
            CounterEntry{ resources::InventoryItem::Rock, &processed_resource_texture_, tile_rect(0u, 0u, 64), 38.0f },
            CounterEntry{ resources::InventoryItem::CopperBar, &processed_resource_texture_, tile_rect(2u, 0u, 64), 38.0f },
            CounterEntry{ resources::InventoryItem::IronBar, &processed_resource_texture_, tile_rect(1u, 0u, 64), 38.0f },
            CounterEntry{ resources::InventoryItem::GoldBar, &processed_resource_texture_, tile_rect(3u, 0u, 64), 38.0f },
            CounterEntry{ resources::InventoryItem::Diamond, &processed_resource_texture_, tile_rect(4u, 0u, 64), 38.0f },
            CounterEntry{ resources::InventoryItem::Seeds, &seed_icon_texture_, tile_rect(7u, 0u, 32), 34.0f }
        };

        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const float row_center_y = first_row_y + static_cast<float>(i) * row_height;

            const auto& [
                count,
                feedback_amount,
                feedback_alpha,
                feedback_offset_y
            ] = hud_state.counters[resources::inventory_item_index(entries[i].item)];

            sf::Sprite icon{ *entries[i].texture, entries[i].icon };

            const auto icon_bounds = icon.getLocalBounds();
            icon.setOrigin(icon_bounds.getCenter());

            const float icon_scale = min(vec2{ entries[i].icon_target_size, entries[i].icon_target_size } / icon_bounds.size);

            icon.setScale({ icon_scale, icon_scale });
            icon.setPosition({ icon_center_x, row_center_y });
            target.draw(icon);

            auto       text        = make_counter_text(ui_font(), count, 28u);
            const auto text_bounds = text.getLocalBounds();

            text.setOrigin(text_bounds.position + vec2{ text_bounds.size.x, text_bounds.size.y * 0.5f });

            text.setPosition({ icon_center_x - 42.0f, row_center_y - 1.0f });

            target.draw(text);

            if (feedback_amount == 0 || feedback_alpha <= 0.0f) continue;

            const float       alpha_value = std::clamp(feedback_alpha * 255.0f, 0.0f, 255.0f);
            const auto        alpha       = static_cast<std::uint8_t>(alpha_value);
            const std::string delta_value = std::format("{}{}", feedback_amount > 0 ? "+" : "",
                                                        feedback_amount);
            sf::Text   delta_text{ ui_font(), delta_value, 20u };
            const auto delta_bounds = delta_text.getLocalBounds();
            delta_text.setOrigin(delta_bounds.position + vec2{ delta_bounds.size.x, delta_bounds.size.y * 0.5f });

            delta_text.setFillColor(feedback_amount > 0
                                        ? with_alpha(0x4FF471_rgb, alpha)
                                        : with_alpha(0xFF5555_rgb, alpha));

            delta_text.setOutlineColor(with_alpha(0x050C08_rgb, static_cast<std::uint8_t>(alpha * 3u / 4u)));
            delta_text.setOutlineThickness(1.4f);
            delta_text.setPosition({ icon_center_x - 112.0f, row_center_y - 1.0f - feedback_offset_y });

            target.draw(delta_text);
        }
    }
}
