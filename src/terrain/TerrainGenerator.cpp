#include "pch.hpp"
#include "TerrainGenerator.hpp"

#include "gfx/ComputeDispatcher.hpp"
#include "terrain/TerrainConstants.hpp"
#include "terrain/TerrainGridMath.hpp"

namespace game::terrain
{
	namespace
	{
		struct GpuBoundaryEdge final
		{
			std::uint32_t a{};
			std::uint32_t b{};
		};

		struct GpuCounters final
		{
			std::uint32_t vertex_count{};
			std::uint32_t index_count{};
			std::uint32_t edge_count{};
		};

		constexpr GLuint image_binding = 0;
		constexpr GLuint boundary_vertices_binding = 1;
		constexpr GLuint horizontal_edge_ids_binding = 2;
		constexpr GLuint vertical_edge_ids_binding = 3;
		constexpr GLuint boundary_vertex_counter_binding = 4;
		constexpr GLuint mesh_vertices_binding = 4;
		constexpr GLuint mesh_indices_binding = 5;
		constexpr GLuint boundary_edges_binding = 6;
		constexpr GLuint counters_binding = 7;

		std::uint32_t generated_cell_count(const ChunkSettings& settings)
		{
			return (settings.field_size.x - 1u + settings.field_padding.x) *
				(settings.field_size.y - 1u + settings.field_padding.y);
		}

		std::uint32_t max_boundary_vertex_count(const ChunkSettings& settings)
		{
		const auto padded_size = padded_field_size(settings);
		const auto width = padded_size.x;
		const auto height = padded_size.y;
		return height * (width - 1u) + width * (height - 1u);
		}

		constexpr float terrain_iso = 0.0f;
		constexpr GLuint64 wait_timeout_ns = 1000000000ull;
		constexpr GLuint terrain_edits_binding = 1;
		constexpr GLuint terrain_edit_summary_binding = 2;
		constexpr GLuint terrain_edit_candidates_binding = 3;
		constexpr GLuint terrain_edit_candidate_count_binding = 4;
	}

	Result<void> TerrainGenerator::initialize(const ChunkSettings& settings)
	{
		static_assert(sizeof(FieldSample) == sizeof(float) * 4);
		settings_ = settings;

		auto load_compute_shader = [](const fs::path& path, gfx::Shader& shader) -> Result<void>
		{
			auto loaded_shader = gfx::Shader::from_compute_file(path);
			if (!loaded_shader) return fail(loaded_shader.error());
			shader = std::move(*loaded_shader);
			return {};
		};

		TRY(load_compute_shader("assets/shaders/terrain_gen.comp", terrain_shader_));
		TRY(load_compute_shader("assets/shaders/cave_gen.comp", cave_shader_));
		TRY(load_compute_shader("assets/shaders/pond_gen.comp", pond_shader_));
		TRY(load_compute_shader("assets/shaders/terrain_edit_candidates.comp", terrain_edit_candidates_shader_));
		TRY(load_compute_shader("assets/shaders/terrain_edit.comp", terrain_edit_shader_));
		TRY(load_compute_shader("assets/shaders/water_smooth.comp", water_smooth_shader_));
		TRY(load_compute_shader("assets/shaders/chunk_edges_gen.comp", edge_shader_));
		TRY(load_compute_shader("assets/shaders/chunk_mesh_gen.comp", mesh_shader_));

		TRY(field_texture_.create(padded_field_size(settings_), gfx::TextureFormat::RGBA32F));
		TRY(scratch_field_texture_.create(padded_field_size(settings_), gfx::TextureFormat::RGBA32F));

		const auto padded_size = padded_field_size(settings_);
		const auto max_edit_candidates = static_cast<std::size_t>(padded_size.x) * static_cast<std::size_t>(padded_size.y);
		terrain_edits_buffer_.resize<TerrainEdit>(32u);
		terrain_edit_summary_buffer_.allocate_persistent_read<TerrainEditSummary>(1u);
		terrain_edit_candidates_buffer_.allocate_persistent_read<TerrainEditCandidate>(max_edit_candidates);
		terrain_edit_candidate_count_buffer_.allocate_persistent_read<std::uint32_t>(1u);
		terrain_edit_selected_candidates_buffer_.resize<TerrainEditCandidate>(max_edit_candidates);
		boundary_vertices_buffer_.allocate_persistent_read<vec2>(max_boundary_vertex_count(settings_));
		horizontal_edge_ids_buffer_.resize<std::int32_t>(padded_size.y * (padded_size.x - 1u));
		vertical_edge_ids_buffer_.resize<std::int32_t>(padded_size.y * padded_size.x);
		boundary_vertex_counter_buffer_.allocate_persistent_read<std::uint32_t>(1);
		mesh_vertices_buffer_.allocate_persistent_read<vec2>(generated_cell_count(settings_) * 12u);
		mesh_indices_buffer_.allocate_persistent_read<std::uint32_t>(generated_cell_count(settings_) * 12u);
		boundary_edges_buffer_.allocate_persistent_read<GpuBoundaryEdge>(generated_cell_count(settings_) * 2u);
		counters_buffer_.allocate_persistent_read<GpuCounters>(1);
		return {};
	}

	TerrainGenerator::~TerrainGenerator()
	{
		clear_completion_fence();
		if (edit_summary_fence_ != nullptr)
		{
			glDeleteSync(edit_summary_fence_);
			edit_summary_fence_ = nullptr;
		}
	}

	TerrainGenerator::TerrainGenerator(TerrainGenerator&& other) noexcept :
		settings_{ other.settings_ },
		terrain_shader_{ std::move(other.terrain_shader_) },
		cave_shader_{ std::move(other.cave_shader_) },
		pond_shader_{ std::move(other.pond_shader_) },
		terrain_edit_candidates_shader_{ std::move(other.terrain_edit_candidates_shader_) },
		terrain_edit_shader_{ std::move(other.terrain_edit_shader_) },
		water_smooth_shader_{ std::move(other.water_smooth_shader_) },
		edge_shader_{ std::move(other.edge_shader_) },
		mesh_shader_{ std::move(other.mesh_shader_) },
		field_texture_{ std::move(other.field_texture_) },
		scratch_field_texture_{ std::move(other.scratch_field_texture_) },
		terrain_edits_buffer_{ std::move(other.terrain_edits_buffer_) },
		terrain_edit_summary_buffer_{ std::move(other.terrain_edit_summary_buffer_) },
		terrain_edit_candidates_buffer_{ std::move(other.terrain_edit_candidates_buffer_) },
		terrain_edit_candidate_count_buffer_{ std::move(other.terrain_edit_candidate_count_buffer_) },
		terrain_edit_selected_candidates_buffer_{ std::move(other.terrain_edit_selected_candidates_buffer_) },
		boundary_vertices_buffer_{ std::move(other.boundary_vertices_buffer_) },
		horizontal_edge_ids_buffer_{ std::move(other.horizontal_edge_ids_buffer_) },
		vertical_edge_ids_buffer_{ std::move(other.vertical_edge_ids_buffer_) },
		boundary_vertex_counter_buffer_{ std::move(other.boundary_vertex_counter_buffer_) },
		mesh_vertices_buffer_{ std::move(other.mesh_vertices_buffer_) },
		mesh_indices_buffer_{ std::move(other.mesh_indices_buffer_) },
		boundary_edges_buffer_{ std::move(other.boundary_edges_buffer_) },
		counters_buffer_{ std::move(other.counters_buffer_) },
		completion_fence_{ std::exchange(other.completion_fence_, nullptr) },
		edit_summary_fence_{ std::exchange(other.edit_summary_fence_, nullptr) },
		pending_edit_summary_{ std::exchange(other.pending_edit_summary_, false) },
		pending_readback_{ std::exchange(other.pending_readback_, false) } {}

	TerrainGenerator& TerrainGenerator::operator=(TerrainGenerator&& other) noexcept
	{
		if (this == &other) return *this;

		clear_completion_fence();
		if (edit_summary_fence_ != nullptr)
		{
			glDeleteSync(edit_summary_fence_);
			edit_summary_fence_ = nullptr;
		}

		settings_ = other.settings_;
		terrain_shader_ = std::move(other.terrain_shader_);
		cave_shader_ = std::move(other.cave_shader_);
		pond_shader_ = std::move(other.pond_shader_);
		terrain_edit_candidates_shader_ = std::move(other.terrain_edit_candidates_shader_);
		terrain_edit_shader_ = std::move(other.terrain_edit_shader_);
		water_smooth_shader_ = std::move(other.water_smooth_shader_);
		edge_shader_ = std::move(other.edge_shader_);
		mesh_shader_ = std::move(other.mesh_shader_);
		field_texture_ = std::move(other.field_texture_);
		scratch_field_texture_ = std::move(other.scratch_field_texture_);
		terrain_edits_buffer_ = std::move(other.terrain_edits_buffer_);
		terrain_edit_summary_buffer_ = std::move(other.terrain_edit_summary_buffer_);
		terrain_edit_candidates_buffer_ = std::move(other.terrain_edit_candidates_buffer_);
		terrain_edit_candidate_count_buffer_ = std::move(other.terrain_edit_candidate_count_buffer_);
		terrain_edit_selected_candidates_buffer_ = std::move(other.terrain_edit_selected_candidates_buffer_);
		boundary_vertices_buffer_ = std::move(other.boundary_vertices_buffer_);
		horizontal_edge_ids_buffer_ = std::move(other.horizontal_edge_ids_buffer_);
		vertical_edge_ids_buffer_ = std::move(other.vertical_edge_ids_buffer_);
		boundary_vertex_counter_buffer_ = std::move(other.boundary_vertex_counter_buffer_);
		mesh_vertices_buffer_ = std::move(other.mesh_vertices_buffer_);
		mesh_indices_buffer_ = std::move(other.mesh_indices_buffer_);
		boundary_edges_buffer_ = std::move(other.boundary_edges_buffer_);
		counters_buffer_ = std::move(other.counters_buffer_);
		completion_fence_ = std::exchange(other.completion_fence_, nullptr);
		edit_summary_fence_ = std::exchange(other.edit_summary_fence_, nullptr);
		pending_edit_summary_ = std::exchange(other.pending_edit_summary_, false);
		pending_readback_ = std::exchange(other.pending_readback_, false);
		return *this;
	}

	Result<void> TerrainGenerator::dispatch()
	{
		if (pending_readback_)
		{
			return fail("TerrainGenerator dispatch called before previous readback completed");
		}

		const auto layout = field_layout();
		const auto groups = gfx::ComputeDispatcher::groups_for(layout.padded_size, 16, 16);

		const std::array generation_passes
		{
			gfx::ComputeDispatcher::Pass{
				.shader = &terrain_shader_,
				.groups = groups,
				.configure = [this, layout](const gfx::Shader& shader)
				{
					bind_generation_pass(shader, layout);
				},
				.barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
			},
			gfx::ComputeDispatcher::Pass{
				.shader = &cave_shader_,
				.groups = groups,
				.configure = [this, layout](const gfx::Shader& shader)
				{
					bind_cave_pass(shader, layout);
				},
				.barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
			},
			gfx::ComputeDispatcher::Pass{
				.shader = &pond_shader_,
				.groups = groups,
				.configure = [this, layout](const gfx::Shader& shader)
				{
					bind_pond_pass(shader, layout);
				},
				.barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
			}
		};

		TRY(gfx::ComputeDispatcher::run(generation_passes));
		TRY(smooth_water_field());

		return dispatch_surface_rebuild(terrain_channel_index, terrain_iso);
	}

	Result<void> TerrainGenerator::apply_edits(
		const std::span<const TerrainEdit> edits,
		const std::uint32_t unit_budget,
		const std::optional<GroundBrushBlocker>& blocker)
	{
		if (pending_readback_)
		{
			return fail("TerrainGenerator edit dispatch called before previous readback completed");
		}
		if (pending_edit_summary_)
		{
			return fail("TerrainGenerator edit dispatch called before previous edit summary was consumed");
		}
		if (edits.empty()) return {};
		if (edits.size() != 1u)
		{
			return fail("TerrainGenerator currently supports exactly one terrain edit per dispatch");
		}

		const auto layout = field_layout();
		ivec2 min_pixel{
			static_cast<std::int32_t>(layout.padded_size.x),
			static_cast<std::int32_t>(layout.padded_size.y)
		};
		ivec2 max_pixel{ -1, -1 };

		for (const auto& edit : edits)
		{
			const float radius = std::max(edit.position_radius_strength.z, 0.0f);
			if (radius <= 0.0f) continue;

			const int edit_min_x = static_cast<int>(std::floor((edit.position_radius_strength.x - radius - layout.field_origin.x) / layout.cell_size.x));
			const int edit_min_y = static_cast<int>(std::floor((edit.position_radius_strength.y - radius - layout.field_origin.y) / layout.cell_size.y));
			const int edit_max_x = static_cast<int>(std::ceil((edit.position_radius_strength.x + radius - layout.field_origin.x) / layout.cell_size.x));
			const int edit_max_y = static_cast<int>(std::ceil((edit.position_radius_strength.y + radius - layout.field_origin.y) / layout.cell_size.y));

			min_pixel.x = std::min(min_pixel.x, edit_min_x);
			min_pixel.y = std::min(min_pixel.y, edit_min_y);
			max_pixel.x = std::max(max_pixel.x, edit_max_x);
			max_pixel.y = std::max(max_pixel.y, edit_max_y);
		}

		min_pixel.x = std::clamp(min_pixel.x, 0, static_cast<int>(layout.padded_size.x) - 1);
		min_pixel.y = std::clamp(min_pixel.y, 0, static_cast<int>(layout.padded_size.y) - 1);
		max_pixel.x = std::clamp(max_pixel.x, 0, static_cast<int>(layout.padded_size.x) - 1);
		max_pixel.y = std::clamp(max_pixel.y, 0, static_cast<int>(layout.padded_size.y) - 1);
		if (min_pixel.x > max_pixel.x || min_pixel.y > max_pixel.y) return {};

		const ivec2 dispatch_size{
			max_pixel.x - min_pixel.x + 1,
			max_pixel.y - min_pixel.y + 1
		};
		auto candidates = collect_edit_candidates(edits.front(), min_pixel, dispatch_size, blocker);
		if (!candidates) return fail(candidates.error());
		std::ranges::sort(*candidates, [](const TerrainEditCandidate& lhs, const TerrainEditCandidate& rhs)
		{
			if (std::abs(lhs.distance_to_center - rhs.distance_to_center) > 1e-6f)
			{
				return lhs.distance_to_center < rhs.distance_to_center;
			}
			if (lhs.pixel.y != rhs.pixel.y) return lhs.pixel.y < rhs.pixel.y;
			return lhs.pixel.x < rhs.pixel.x;
		});
		const auto selected_count = std::min<std::size_t>(candidates->size(), unit_budget);
		const auto selected_candidates = std::span{ candidates->data(), selected_count };
		TRY(apply_selected_edit_candidates(edits.front(), selected_candidates, blocker));
		return {};
	}

	Result<void> TerrainGenerator::smooth_water_field(const std::uint32_t iterations)
	{
		if (iterations == 0u) return {};

		const auto size = field_texture_.size();
		const auto groups = gfx::ComputeDispatcher::groups_for(size, 16, 16);
		for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
		{
			const bool write_to_scratch = (iteration % 2u) == 0u;
			const auto& input_texture = write_to_scratch ? field_texture_ : scratch_field_texture_;
			const auto& output_texture = write_to_scratch ? scratch_field_texture_ : field_texture_;

			const std::array smooth_passes
			{
				gfx::ComputeDispatcher::Pass{
					.shader = &water_smooth_shader_,
					.groups = groups,
					.configure = [this, &input_texture, &output_texture](const gfx::Shader& shader)
					{
						bind_water_smooth_pass(shader, input_texture, output_texture);
					},
					.barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
				}
			};

			if (auto res = gfx::ComputeDispatcher::run(smooth_passes); !res) return fail(res.error());
		}

		return {};
	}

	Result<void> TerrainGenerator::dispatch_surface_rebuild(const std::uint32_t channel_index, const float iso)
	{
		reset_surface_buffers();

		const auto layout = field_layout();
		const auto groups = gfx::ComputeDispatcher::groups_for(layout.padded_size, 16, 16);

		const std::array rebuild_passes
		{
			gfx::ComputeDispatcher::Pass{
				.shader = &edge_shader_,
				.groups = groups,
				.configure = [this, layout, channel_index, iso](const gfx::Shader& shader)
				{
					bind_surface_edge_pass(shader, layout, channel_index, iso);
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT
			},
			gfx::ComputeDispatcher::Pass{
				.shader = &mesh_shader_,
				.groups = groups,
				.configure = [this, layout, channel_index, iso](const gfx::Shader& shader)
				{
					bind_surface_mesh_pass(shader, layout, channel_index, iso);
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT
			}
		};

		if (auto res = gfx::ComputeDispatcher::run(rebuild_passes); !res) return fail(res.error());

		return replace_completion_fence();
	}

	Result<void> TerrainGenerator::upload_field(const std::span<const FieldSample> field_samples)
	{
		if (!field_texture_.valid())
		{
			return fail("TerrainGenerator field texture is not initialized");
		}

		const auto size = field_texture_.size();
		const auto expected_count = static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y);
		if (field_samples.size() != expected_count)
		{
			return fail(
				"Uploaded field data size {} does not match texture extent sample count {}",
				field_samples.size(),
				expected_count);
		}

		glTextureSubImage2D(
			field_texture_.native_handle(),
			0,
			0,
			0,
			static_cast<GLsizei>(size.x),
			static_cast<GLsizei>(size.y),
			GL_RGBA,
			GL_FLOAT,
			field_samples.empty() ? nullptr : field_samples.data());
		return {};
	}

	Result<std::vector<TerrainGenerator::FieldSample>> TerrainGenerator::read_field() const
	{
		if (!field_texture_.valid())
		{
			return fail("TerrainGenerator field texture is not initialized");
		}

		const auto size = field_texture_.size();
		std::vector<FieldSample> field_samples(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y));
		if (field_samples.empty()) return field_samples;

		glGetTextureImage(
			field_texture_.native_handle(),
			0,
			GL_RGBA,
			GL_FLOAT,
			static_cast<GLsizei>(field_samples.size() * sizeof(FieldSample)),
			field_samples.data());

		return field_samples;
	}

	Result<TerrainGenerator::TerrainEditSummary> TerrainGenerator::read_edit_summary()
	{
		if (!pending_edit_summary_)
		{
			return TerrainEditSummary{};
		}

		for (;;)
		{
			const auto wait_result = glClientWaitSync(edit_summary_fence_, GL_SYNC_FLUSH_COMMANDS_BIT, wait_timeout_ns);
			if (wait_result == GL_ALREADY_SIGNALED || wait_result == GL_CONDITION_SATISFIED)
			{
				break;
			}

			if (wait_result == GL_WAIT_FAILED)
			{
				return fail("TerrainGenerator failed while waiting for edit summary completion");
			}
		}

		glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
		TerrainEditSummary summary = terrain_edit_summary_buffer_.read_one<TerrainEditSummary>();
		glDeleteSync(edit_summary_fence_);
		edit_summary_fence_ = nullptr;
		pending_edit_summary_ = false;
		return summary;
	}

	Result<std::vector<TerrainGenerator::TerrainEditCandidate>> TerrainGenerator::collect_edit_candidates(
		const TerrainEdit& edit,
		const ivec2 dispatch_origin,
		const ivec2 dispatch_size,
		const std::optional<GroundBrushBlocker>& blocker)
	{
		const auto layout = field_layout();
		const auto max_candidate_count = static_cast<std::uint32_t>(layout.padded_size.x * layout.padded_size.y);
		terrain_edits_buffer_.set_data(std::span{ &edit, 1u });
		const std::uint32_t zero = 0u;
		terrain_edit_candidate_count_buffer_.write(std::span{ &zero, 1u });

		const auto groups = gfx::ComputeDispatcher::groups_for(
			{
				static_cast<std::uint32_t>(dispatch_size.x),
				static_cast<std::uint32_t>(dispatch_size.y)
			},
			16,
			16);

		const std::array candidate_passes
		{
			gfx::ComputeDispatcher::Pass{
				.shader = &terrain_edit_candidates_shader_,
				.groups = groups,
				.configure = [this, layout, dispatch_origin, dispatch_size, max_candidate_count, blocker](const gfx::Shader& shader)
				{
					field_texture_.bind_image(image_binding, GL_READ_ONLY);
					terrain_edits_buffer_.bind_base(terrain_edits_binding);
					terrain_edit_candidates_buffer_.bind_base(terrain_edit_candidates_binding);
					terrain_edit_candidate_count_buffer_.bind_base(terrain_edit_candidate_count_binding);
					bind_chunk_uniforms(shader, layout);
					shader.set_uniform("uDispatchOrigin", dispatch_origin);
					shader.set_uniform("uDispatchSize", dispatch_size);
					shader.set_uniform("uMaxCandidateCount", max_candidate_count);
					shader.set_uniform("uSeed", settings_.seed);
					shader.set_uniform("uPlanetRadius", settings_.planet_radius);
					shader.set_uniform("uHardRockDepthThreshold", constants::hard_rock_depth_threshold);
					shader.set_uniform("uHasBlocker", blocker.has_value() ? std::int32_t{ 1 } : std::int32_t{ 0 });
					shader.set_uniform("uBlockerCenter", blocker.has_value() ? blocker->center : vec2{ 0.0f, 0.0f });
					shader.set_uniform("uBlockerRight", blocker.has_value() ? blocker->right : vec2{ 1.0f, 0.0f });
					shader.set_uniform("uBlockerUp", blocker.has_value() ? blocker->up : vec2{ 0.0f, 1.0f });
					shader.set_uniform("uBlockerHalfExtents", blocker.has_value() ? blocker->half_extents : vec2{ 0.0f, 0.0f });
				},
				.barrier_after = GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT
			}
		};

		TRY(gfx::ComputeDispatcher::run(candidate_passes));
		GLsync candidate_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		if (candidate_fence == nullptr) return fail("Failed to create terrain edit candidate GPU completion fence");
		for (;;)
		{
			const auto wait_result = glClientWaitSync(candidate_fence, GL_SYNC_FLUSH_COMMANDS_BIT, wait_timeout_ns);
			if (wait_result == GL_ALREADY_SIGNALED || wait_result == GL_CONDITION_SATISFIED) break;
			if (wait_result == GL_WAIT_FAILED)
			{
				glDeleteSync(candidate_fence);
				return fail("TerrainGenerator failed while waiting for terrain edit candidate completion");
			}
		}
		glDeleteSync(candidate_fence);
		glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
		const auto candidate_count = terrain_edit_candidate_count_buffer_.read_one<std::uint32_t>();
		return terrain_edit_candidates_buffer_.read<TerrainEditCandidate>(std::min(candidate_count, max_candidate_count));
	}

	Result<void> TerrainGenerator::apply_selected_edit_candidates(
		const TerrainEdit& edit,
		const std::span<const TerrainEditCandidate> selected_candidates,
		const std::optional<GroundBrushBlocker>& blocker)
	{
		terrain_edits_buffer_.set_data(std::span{ &edit, 1u });
		terrain_edit_selected_candidates_buffer_.set_data(selected_candidates);
		const auto layout = field_layout();
		const TerrainEditSummary summary_reset{
			.removed_units = 0u,
			.placed_units = 0u,
			.changed_any = 0u,
			.touched_water_or_wet = 0u,
			.applied_samples = 0u,
			.padding0 = 0u,
			.changed_min = {
				static_cast<std::int32_t>(layout.padded_size.x),
				static_cast<std::int32_t>(layout.padded_size.y)
			},
			.changed_max = { -1, -1 }
		};
		terrain_edit_summary_buffer_.write(std::span{ &summary_reset, 1u });

		const auto groups = gfx::ComputeDispatcher::groups_for(
			{
				static_cast<std::uint32_t>(std::max<std::size_t>(selected_candidates.size(), 1u)),
				1u
			},
			64,
			1);
		const std::array apply_passes
		{
			gfx::ComputeDispatcher::Pass{
				.shader = &terrain_edit_shader_,
				.groups = groups,
				.configure = [this, layout, selected_candidates, blocker](const gfx::Shader& shader)
				{
					field_texture_.bind_image(image_binding, GL_READ_WRITE);
					terrain_edits_buffer_.bind_base(terrain_edits_binding);
					terrain_edit_summary_buffer_.bind_base(terrain_edit_summary_binding);
					terrain_edit_selected_candidates_buffer_.bind_base(terrain_edit_candidates_binding);
					bind_chunk_uniforms(shader, layout);
					shader.set_uniform("uSelectedCount", static_cast<std::uint32_t>(selected_candidates.size()));
					shader.set_uniform("uSeed", settings_.seed);
					shader.set_uniform("uPlanetRadius", settings_.planet_radius);
					shader.set_uniform("uHardRockDepthThreshold", constants::hard_rock_depth_threshold);
					shader.set_uniform("uFieldPadding", layout.field_padding);
					shader.set_uniform("uFieldSize", ivec2{
						static_cast<std::int32_t>(settings_.field_size.x),
						static_cast<std::int32_t>(settings_.field_size.y)
					});
					shader.set_uniform("uHasBlocker", blocker.has_value() ? std::int32_t{ 1 } : std::int32_t{ 0 });
					shader.set_uniform("uBlockerCenter", blocker.has_value() ? blocker->center : vec2{ 0.0f, 0.0f });
					shader.set_uniform("uBlockerRight", blocker.has_value() ? blocker->right : vec2{ 1.0f, 0.0f });
					shader.set_uniform("uBlockerUp", blocker.has_value() ? blocker->up : vec2{ 0.0f, 1.0f });
					shader.set_uniform("uBlockerHalfExtents", blocker.has_value() ? blocker->half_extents : vec2{ 0.0f, 0.0f });
				},
				.barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT
			}
		};

		TRY(gfx::ComputeDispatcher::run(apply_passes));
		if (edit_summary_fence_ != nullptr)
		{
			glDeleteSync(edit_summary_fence_);
			edit_summary_fence_ = nullptr;
		}
		edit_summary_fence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		if (edit_summary_fence_ == nullptr)
		{
			return fail("Failed to create terrain edit summary GPU completion fence");
		}
		pending_edit_summary_ = true;
		return {};
	}

	Result<TerrainGenerator::RawPipelineResult> TerrainGenerator::readback()
	{
		if (!pending_readback_)
		{
			return RawPipelineResult{};
		}

		if (auto res = wait_for_completion(); !res) return fail(res.error());

		return consume_readback();
	}

	TerrainGenerator::FieldLayout TerrainGenerator::field_layout() const
	{
		return {
			.padded_size = padded_field_size(settings_),
			.cell_size = cell_size(settings_),
			.field_origin = field_origin(settings_),
			.field_padding = field_padding(settings_)
		};
	}

	void TerrainGenerator::bind_chunk_uniforms(const gfx::Shader& shader, const FieldLayout& layout) const
	{
		shader.set_uniform("uChunkCoord", settings_.chunk_coord);
		shader.set_uniform("uChunkGridSize", settings_.chunk_grid_size);
		shader.set_uniform("uChunkSize", settings_.chunk_size);
		shader.set_uniform("uWorldCenter", settings_.world_center);
		shader.set_uniform("uFieldOrigin", layout.field_origin);
		shader.set_uniform("uCellSize", layout.cell_size);
	}

	void TerrainGenerator::bind_generation_pass(const gfx::Shader& shader, const FieldLayout& layout) const
	{
		field_texture_.bind_image(image_binding, GL_WRITE_ONLY);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uSeed", settings_.seed);
		shader.set_uniform("uPlanetRadius", settings_.planet_radius);
	}

	void TerrainGenerator::bind_cave_pass(const gfx::Shader& shader, const FieldLayout& layout) const
	{
		field_texture_.bind_image(image_binding, GL_READ_WRITE);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uSeed", settings_.seed);
		shader.set_uniform("uPlanetRadius", settings_.planet_radius);
	}

	void TerrainGenerator::bind_pond_pass(const gfx::Shader& shader, const FieldLayout& layout) const
	{
		field_texture_.bind_image(image_binding, GL_READ_WRITE);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uSeed", settings_.seed);
		shader.set_uniform("uPlanetRadius", settings_.planet_radius);
	}

	void TerrainGenerator::bind_edit_pass(
		const gfx::Shader& shader,
		const FieldLayout& layout,
		const std::span<const TerrainEdit> edits,
		const std::uint32_t unit_budget,
		const ivec2 dispatch_origin,
		const ivec2 dispatch_size,
		const std::optional<GroundBrushBlocker>& blocker) const
	{
		field_texture_.bind_image(image_binding, GL_READ_WRITE);
		terrain_edits_buffer_.bind_base(terrain_edits_binding);
		terrain_edit_summary_buffer_.bind_base(terrain_edit_summary_binding);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uEditCount", static_cast<std::uint32_t>(edits.size()));
		shader.set_uniform("uUnitBudget", unit_budget);
		shader.set_uniform("uDispatchOrigin", dispatch_origin);
		shader.set_uniform("uDispatchSize", dispatch_size);
		shader.set_uniform("uFieldPadding", layout.field_padding);
		shader.set_uniform("uFieldSize", ivec2{
			static_cast<std::int32_t>(settings_.field_size.x),
			static_cast<std::int32_t>(settings_.field_size.y)
		});
		shader.set_uniform("uSeed", settings_.seed);
		shader.set_uniform("uPlanetRadius", settings_.planet_radius);
		shader.set_uniform("uHardRockDepthThreshold", constants::hard_rock_depth_threshold);
		shader.set_uniform("uHasBlocker", blocker.has_value() ? std::int32_t{ 1 } : std::int32_t{ 0 });
		shader.set_uniform("uBlockerCenter", blocker.has_value() ? blocker->center : vec2{ 0.0f, 0.0f });
		shader.set_uniform("uBlockerRight", blocker.has_value() ? blocker->right : vec2{ 1.0f, 0.0f });
		shader.set_uniform("uBlockerUp", blocker.has_value() ? blocker->up : vec2{ 0.0f, 1.0f });
		shader.set_uniform("uBlockerHalfExtents", blocker.has_value() ? blocker->half_extents : vec2{ 0.0f, 0.0f });
	}

	void TerrainGenerator::bind_water_smooth_pass(
		const gfx::Shader& shader,
		const gfx::Texture2D& input_texture,
		const gfx::Texture2D& output_texture) const
	{
		input_texture.bind_image(0u, GL_READ_ONLY);
		output_texture.bind_image(1u, GL_WRITE_ONLY);
		static_cast<void>(shader);
	}

	void TerrainGenerator::bind_surface_edge_pass(
		const gfx::Shader& shader,
		const FieldLayout& layout,
		const std::uint32_t channel_index,
		const float iso) const
	{
		field_texture_.bind_image(image_binding, GL_READ_ONLY);
		boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
		horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
		vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
		boundary_vertex_counter_buffer_.bind_base(boundary_vertex_counter_binding);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uIso", iso);
		shader.set_uniform("uChannelIndex", static_cast<std::int32_t>(channel_index));
	}

	void TerrainGenerator::bind_surface_mesh_pass(
		const gfx::Shader& shader,
		const FieldLayout& layout,
		const std::uint32_t channel_index,
		const float iso) const
	{
		field_texture_.bind_image(image_binding, GL_READ_ONLY);
		boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
		horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
		vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
		mesh_vertices_buffer_.bind_base(mesh_vertices_binding);
		mesh_indices_buffer_.bind_base(mesh_indices_binding);
		boundary_edges_buffer_.bind_base(boundary_edges_binding);
		counters_buffer_.bind_base(counters_binding);
		bind_chunk_uniforms(shader, layout);
		shader.set_uniform("uIso", iso);
		shader.set_uniform("uChannelIndex", static_cast<std::int32_t>(channel_index));
		shader.set_uniform("uFieldPadding", layout.field_padding);
		shader.set_uniform("uFieldSize", ivec2{
			static_cast<std::int32_t>(settings_.field_size.x),
			static_cast<std::int32_t>(settings_.field_size.y)
		});
	}

	void TerrainGenerator::reset_surface_buffers()
	{
		static constexpr std::int32_t minus_one = -1;
		static constexpr std::uint32_t zero = 0;

		glClearNamedBufferData(horizontal_edge_ids_buffer_.id(), GL_R32I, GL_RED_INTEGER, GL_INT, &minus_one);
		glClearNamedBufferData(vertical_edge_ids_buffer_.id(), GL_R32I, GL_RED_INTEGER, GL_INT, &minus_one);
		glClearNamedBufferData(boundary_vertex_counter_buffer_.id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
		glClearNamedBufferData(counters_buffer_.id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
	}

	Result<void> TerrainGenerator::replace_completion_fence()
	{
		clear_completion_fence();
		completion_fence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		if (completion_fence_ == nullptr)
		{
			return fail("Failed to create terrain generator GPU completion fence");
		}

		pending_readback_ = true;
		return {};
	}

	void TerrainGenerator::clear_completion_fence()
	{
		if (completion_fence_ != nullptr)
		{
			glDeleteSync(completion_fence_);
			completion_fence_ = nullptr;
		}
	}

	Result<void> TerrainGenerator::wait_for_completion()
	{
		if (!pending_readback_ || completion_fence_ == nullptr) return {};

		for (;;)
		{
			const auto wait_result = glClientWaitSync(completion_fence_, GL_SYNC_FLUSH_COMMANDS_BIT, wait_timeout_ns);
			if (wait_result == GL_ALREADY_SIGNALED || wait_result == GL_CONDITION_SATISFIED)
			{
				return {};
			}

			if (wait_result == GL_WAIT_FAILED)
			{
				return fail("TerrainGenerator failed while waiting for GPU completion");
			}
		}
	}

	Result<TerrainGenerator::RawPipelineResult> TerrainGenerator::consume_readback()
	{
		RawPipelineResult result{};
		if (!pending_readback_) return result;

		glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

		const auto boundary_vertex_count = boundary_vertex_counter_buffer_.read_one<std::uint32_t>();
		const auto [
			vertex_count,
			index_count,
			edge_count
		] = counters_buffer_.read_one<GpuCounters>();

		clear_completion_fence();
		pending_readback_ = false;

		if (boundary_vertex_count > 0)
		{
			const auto gpu_boundary_vertices = boundary_vertices_buffer_.read<vec2>(boundary_vertex_count);
			result.boundary_vertices.reserve(gpu_boundary_vertices.size());
			for (const auto& vertex : gpu_boundary_vertices)
			{
				result.boundary_vertices.push_back(vertex);
			}
		}

		if (vertex_count > 0)
		{
			const auto gpu_mesh_vertices = mesh_vertices_buffer_.read<vec2>(vertex_count);
			result.mesh_vertices.reserve(gpu_mesh_vertices.size());
			for (const auto& vertex : gpu_mesh_vertices)
			{
				result.mesh_vertices.push_back(vertex);
			}
		}

		if (index_count > 0)
		{
			result.mesh_indices = mesh_indices_buffer_.read<std::uint32_t>(index_count);
		}

		if (edge_count > 0)
		{
			const auto gpu_boundary_edges = boundary_edges_buffer_.read<GpuBoundaryEdge>(edge_count);
			result.boundary_edges.reserve(gpu_boundary_edges.size());
			for (const auto& [a, b] : gpu_boundary_edges)
			{
				result.boundary_edges.emplace_back(a, b);
			}
		}

		return result;
	}
}
