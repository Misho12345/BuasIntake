#include "pch.hpp"

#include "TerrainGenerator.hpp"

#include "gfx/ComputeDispatcher.hpp"
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

        struct GpuBoundaryVertexCounter final
        {
            std::uint32_t count{};
            std::uint32_t overflowed{};
        };

        struct GpuCounters final
        {
            std::uint32_t vertex_count{};
            std::uint32_t index_count{};
            std::uint32_t edge_count{};
            std::uint32_t overflowed{};
        };

        constexpr GLuint image_binding                   = 0;
        constexpr GLuint boundary_vertices_binding       = 1;
        constexpr GLuint horizontal_edge_ids_binding     = 2;
        constexpr GLuint vertical_edge_ids_binding       = 3;
        constexpr GLuint boundary_vertex_counter_binding = 4;
        constexpr GLuint mesh_vertices_binding           = 4;
        constexpr GLuint mesh_indices_binding            = 5;
        constexpr GLuint boundary_edges_binding          = 6;
        constexpr GLuint counters_binding                = 7;

        std::uint32_t generated_cell_count(const ChunkSettings& settings)
        {
            return (settings.field_size.x - 1u + settings.field_padding.x) * (settings.field_size.y - 1u + settings.
                field_padding.y);
        }

        std::uint32_t max_boundary_vertex_count(const ChunkSettings& settings)
        {
            const auto padded_size = padded_field_size(settings);
            const auto width       = padded_size.x;
            const auto height      = padded_size.y;
            return height * (width - 1u) + width * (height - 1u);
        }

        std::uint32_t max_mesh_vertex_count(const ChunkSettings& settings)
        {
            return generated_cell_count(settings) * 12u;
        }

        std::uint32_t max_mesh_index_count(const ChunkSettings& settings)
        {
            return generated_cell_count(settings) * 12u;
        }

        std::uint32_t max_boundary_edge_count(const ChunkSettings& settings)
        {
            return generated_cell_count(settings) * 2u;
        }

        constexpr float    terrain_iso     = 0.0f;
        constexpr GLuint64 wait_timeout_ns = 1000000000ull;
    }

    Result<void> TerrainGenerator::initialize(const ChunkSettings& settings)
    {
        // FieldSample is uploaded and read back as raw RGBA32F data, so its CPU layout must stay exact.
        static_assert(sizeof(FieldSample) == sizeof(float) * 4);
        settings_ = settings;

        auto load_compute_shader = [](const fs::path& path, gfx::Shader& shader) -> Result<void>
        {
            auto loaded_shader = gfx::Shader::from_compute_file(path);
            if (!loaded_shader) return fail(loaded_shader.error());
            shader = std::move(*loaded_shader);
            return {};
        };

        TRY(load_compute_shader("assets/shaders/terrain/terrain_gen.comp", terrain_shader_));
        TRY(load_compute_shader("assets/shaders/terrain/cave_gen.comp", cave_shader_));
        TRY(load_compute_shader("assets/shaders/terrain/pond_gen.comp", pond_shader_));
        TRY(load_compute_shader("assets/shaders/water/water_smooth.comp", water_smooth_shader_));
        TRY(load_compute_shader("assets/shaders/terrain/chunk_edges_gen.comp", edge_shader_));
        TRY(load_compute_shader("assets/shaders/terrain/chunk_mesh_gen.comp", mesh_shader_));

        // The field texture is the shared source of truth for generation, smoothing, and later mesh extraction.
        TRY(field_texture_.create(padded_field_size(settings_), gfx::TextureFormat::RGBA32F));
        TRY(scratch_field_texture_.create(padded_field_size(settings_), gfx::TextureFormat::RGBA32F));

        const auto padded_size = padded_field_size(settings_);
        // These sizes are the worst-case marching-squares output, so the rebuild can stay fully on the GPU.
        boundary_vertices_buffer_.allocate_persistent_read<vec2>(max_boundary_vertex_count(settings_));
        horizontal_edge_ids_buffer_.resize<std::int32_t>(padded_size.y * (padded_size.x - 1u));
        vertical_edge_ids_buffer_.resize<std::int32_t>(padded_size.y * padded_size.x);
        boundary_vertex_counter_buffer_.allocate_persistent_read<GpuBoundaryVertexCounter>(1u);
        mesh_vertices_buffer_.allocate_persistent_read<vec2>(max_mesh_vertex_count(settings_));
        mesh_indices_buffer_.allocate_persistent_read<std::uint32_t>(max_mesh_index_count(settings_));
        boundary_edges_buffer_.allocate_persistent_read<GpuBoundaryEdge>(max_boundary_edge_count(settings_));
        counters_buffer_.allocate_persistent_read<GpuCounters>(1);
        return {};
    }

    TerrainGenerator::~TerrainGenerator() { clear_completion_fence(); }

    TerrainGenerator::TerrainGenerator(TerrainGenerator&& other) noexcept
        : settings_{ other.settings_ },
          terrain_shader_{ std::move(other.terrain_shader_) },
          cave_shader_{ std::move(other.cave_shader_) },
          pond_shader_{ std::move(other.pond_shader_) },
          water_smooth_shader_{ std::move(other.water_smooth_shader_) },
          edge_shader_{ std::move(other.edge_shader_) },
          mesh_shader_{ std::move(other.mesh_shader_) },
          field_texture_{ std::move(other.field_texture_) },
          scratch_field_texture_{ std::move(other.scratch_field_texture_) },
          boundary_vertices_buffer_{ std::move(other.boundary_vertices_buffer_) },
          horizontal_edge_ids_buffer_{ std::move(other.horizontal_edge_ids_buffer_) },
          vertical_edge_ids_buffer_{ std::move(other.vertical_edge_ids_buffer_) },
          boundary_vertex_counter_buffer_{ std::move(other.boundary_vertex_counter_buffer_) },
          mesh_vertices_buffer_{ std::move(other.mesh_vertices_buffer_) },
          mesh_indices_buffer_{ std::move(other.mesh_indices_buffer_) },
          boundary_edges_buffer_{ std::move(other.boundary_edges_buffer_) },
          counters_buffer_{ std::move(other.counters_buffer_) },
          completion_fence_{ std::exchange(other.completion_fence_, nullptr) },
          pending_readback_{ std::exchange(other.pending_readback_, false) } {}

    TerrainGenerator& TerrainGenerator::operator=(TerrainGenerator&& other) noexcept
    {
        if (this == &other) return *this;

        clear_completion_fence();

        settings_                       = other.settings_;
        terrain_shader_                 = std::move(other.terrain_shader_);
        cave_shader_                    = std::move(other.cave_shader_);
        pond_shader_                    = std::move(other.pond_shader_);
        water_smooth_shader_            = std::move(other.water_smooth_shader_);
        edge_shader_                    = std::move(other.edge_shader_);
        mesh_shader_                    = std::move(other.mesh_shader_);
        field_texture_                  = std::move(other.field_texture_);
        scratch_field_texture_          = std::move(other.scratch_field_texture_);
        boundary_vertices_buffer_       = std::move(other.boundary_vertices_buffer_);
        horizontal_edge_ids_buffer_     = std::move(other.horizontal_edge_ids_buffer_);
        vertical_edge_ids_buffer_       = std::move(other.vertical_edge_ids_buffer_);
        boundary_vertex_counter_buffer_ = std::move(other.boundary_vertex_counter_buffer_);
        mesh_vertices_buffer_           = std::move(other.mesh_vertices_buffer_);
        mesh_indices_buffer_            = std::move(other.mesh_indices_buffer_);
        boundary_edges_buffer_          = std::move(other.boundary_edges_buffer_);
        counters_buffer_                = std::move(other.counters_buffer_);
        completion_fence_               = std::exchange(other.completion_fence_, nullptr);
        pending_readback_               = std::exchange(other.pending_readback_, false);
        return *this;
    }

    // generation is staged on purpose
    // base shell first then caves then ponds then smoothing and only after that do we extract the renderable surface
    Result<void> TerrainGenerator::dispatch()
    {
        if (pending_readback_) { return fail("TerrainGenerator dispatch called before previous readback completed"); }

        const auto layout = field_layout();
        const auto groups = gfx::ComputeDispatcher::groups_for(layout.padded_size, 16, 16);

        // Build the chunk in stages: base shell first, then caves, then ponds.
        const std::array generation_passes{
            gfx::ComputeDispatcher::Pass{
                .shader        = &terrain_shader_,
                .groups        = groups,
                .configure     = [this, layout](const gfx::Shader& shader) { bind_generation_pass(shader, layout); },
                .barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
            },
            gfx::ComputeDispatcher::Pass{
                .shader        = &cave_shader_,
                .groups        = groups,
                .configure     = [this, layout](const gfx::Shader& shader) { bind_cave_pass(shader, layout); },
                .barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
            },
            gfx::ComputeDispatcher::Pass{
                .shader        = &pond_shader_,
                .groups        = groups,
                .configure     = [this, layout](const gfx::Shader& shader) { bind_pond_pass(shader, layout); },
                .barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
            }
        };

        TRY(gfx::ComputeDispatcher::run(generation_passes));
        // Smooth the raw water field before extracting any contours from it.
        TRY(smooth_water_field());

        return dispatch_surface_rebuild(terrain_channel_index, terrain_iso);
    }

    // the smoothing pass ping pongs between textures because each iteration needs a stable previous state to read from
    Result<void> TerrainGenerator::smooth_water_field(const std::uint32_t iterations)
    {
        if (iterations == 0u) return {};

        const auto size   = field_texture_.size();
        const auto groups = gfx::ComputeDispatcher::groups_for(size, 16, 16);
        for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            // Ping-pong between two textures so each pass reads stable values from the previous step.
            const bool  write_to_scratch = (iteration % 2u) == 0u;
            const auto& input_texture    = write_to_scratch ? field_texture_ : scratch_field_texture_;
            const auto& output_texture   = write_to_scratch ? scratch_field_texture_ : field_texture_;

            const std::array smooth_passes{
                gfx::ComputeDispatcher::Pass{
                    .shader    = &water_smooth_shader_,
                    .groups    = groups,
                    .configure = [this, &input_texture, &output_texture](const gfx::Shader& shader)
                    {
                        bind_water_smooth_pass(shader, input_texture, output_texture);
                    },
                    .barrier_after = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                }
            };

            if (auto res = gfx::ComputeDispatcher::run(smooth_passes);
                !res)
                return fail(res.error());
        }

        if ((iterations % 2u) != 0u)
        {
            const auto size = field_texture_.size();
            glCopyImageSubData(
                scratch_field_texture_.native_handle(),
                GL_TEXTURE_2D,
                0,
                0,
                0,
                0,
                field_texture_.native_handle(),
                GL_TEXTURE_2D,
                0,
                0,
                0,
                0,
                static_cast<GLsizei>(size.x),
                static_cast<GLsizei>(size.y),
                1);

            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }

        return {};
    }

    Result<void> TerrainGenerator::dispatch_surface_rebuild(const std::uint32_t channel_index, const float iso)
    {
        reset_surface_buffers();

        const auto layout = field_layout();
        const auto groups = gfx::ComputeDispatcher::groups_for(layout.padded_size, 16, 16);

        // First find the contour crossings, then let the mesh pass turn those crossings into triangles and edge links.
        const std::array rebuild_passes{
            gfx::ComputeDispatcher::Pass{
                .shader    = &edge_shader_,
                .groups    = groups,
                .configure = [this, layout, channel_index, iso](const gfx::Shader& shader)
                {
                    bind_surface_edge_pass(shader, layout, channel_index, iso);
                },
                .barrier_after = GL_SHADER_STORAGE_BARRIER_BIT
            },
            gfx::ComputeDispatcher::Pass{
                .shader    = &mesh_shader_,
                .groups    = groups,
                .configure = [this, layout, channel_index, iso](const gfx::Shader& shader)
                {
                    bind_surface_mesh_pass(shader, layout, channel_index, iso);
                },
                .barrier_after = GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT
            }
        };

        if (auto res = gfx::ComputeDispatcher::run(rebuild_passes); !res)
            return fail(res.error());

        return replace_completion_fence();
    }

    Result<void> TerrainGenerator::upload_field(const std::span<const FieldSample> field_samples)
    {
        if (!field_texture_.valid()) { return fail("TerrainGenerator field texture is not initialized"); }

        const auto size           = field_texture_.size();
        const auto expected_count = static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y);
        if (field_samples.size() != expected_count)
        {
            return fail("Uploaded field data size {} does not match texture extent sample count {}",
                        field_samples.size(), expected_count);
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
        if (!field_texture_.valid()) { return fail("TerrainGenerator field texture is not initialized"); }

        const auto               size = field_texture_.size();
        std::vector<FieldSample> field_samples(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y));
        if (field_samples.empty()) return field_samples;

        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        glGetTextureImage(
            field_texture_.native_handle(),
            0,
            GL_RGBA,
            GL_FLOAT,
            static_cast<GLsizei>(field_samples.size() * sizeof(FieldSample)),
            field_samples.data());

        return field_samples;
    }

    Result<TerrainGenerator::RawPipelineResult> TerrainGenerator::readback()
    {
        if (!pending_readback_) return RawPipelineResult{};

        if (auto res = wait_for_completion(); !res)
            return fail(res.error());

        return consume_readback();
    }

    TerrainGenerator::FieldLayout TerrainGenerator::field_layout() const
    {
        return {
            .padded_size   = padded_field_size(settings_),
            .cell_size     = cell_size(settings_),
            .field_origin  = field_origin(settings_),
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

    void TerrainGenerator::bind_water_smooth_pass(
        const gfx::Shader&    shader,
        const gfx::Texture2D& input_texture,
        const gfx::Texture2D& output_texture) const
    {
        input_texture.bind_image(0u, GL_READ_ONLY);
        output_texture.bind_image(1u, GL_WRITE_ONLY);
        static_cast<void>(shader);
    }

    void TerrainGenerator::bind_surface_edge_pass(
        const gfx::Shader&  shader,
        const FieldLayout&  layout,
        const std::uint32_t channel_index,
        const float         iso) const
    {
        field_texture_.bind_image(image_binding, GL_READ_ONLY);
        boundary_vertices_buffer_.bind_base(boundary_vertices_binding);
        horizontal_edge_ids_buffer_.bind_base(horizontal_edge_ids_binding);
        vertical_edge_ids_buffer_.bind_base(vertical_edge_ids_binding);
        boundary_vertex_counter_buffer_.bind_base(boundary_vertex_counter_binding);
        bind_chunk_uniforms(shader, layout);
        shader.set_uniform("uIso", iso);
        shader.set_uniform("uChannelIndex", static_cast<std::int32_t>(channel_index));
        shader.set_uniform("uMaxBoundaryVertices", max_boundary_vertex_count(settings_));
    }

    void TerrainGenerator::bind_surface_mesh_pass(
        const gfx::Shader&  shader,
        const FieldLayout&  layout,
        const std::uint32_t channel_index,
        const float         iso) const
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

        shader.set_uniform("uFieldSize",
                           ivec2{
                               static_cast<std::int32_t>(settings_.field_size.x),
                               static_cast<std::int32_t>(settings_.field_size.y)
                           });

        shader.set_uniform("uMaxBoundaryEdges", max_boundary_edge_count(settings_));
        shader.set_uniform("uMaxMeshVertices", max_mesh_vertex_count(settings_));
        shader.set_uniform("uMaxMeshIndices", max_mesh_index_count(settings_));
    }

    void TerrainGenerator::reset_surface_buffers()
    {
        static constexpr std::int32_t  minus_one = -1;
        static constexpr std::uint32_t zero      = 0;

        // The compute passes append into these buffers, so every rebuild starts from a clean set of ids and counters.
        glClearNamedBufferData(horizontal_edge_ids_buffer_.id(), GL_R32I, GL_RED_INTEGER, GL_INT, &minus_one);
        glClearNamedBufferData(vertical_edge_ids_buffer_.id(), GL_R32I, GL_RED_INTEGER, GL_INT, &minus_one);
        glClearNamedBufferData(boundary_vertex_counter_buffer_.id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
        glClearNamedBufferData(counters_buffer_.id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    }

    Result<void> TerrainGenerator::replace_completion_fence()
    {
        clear_completion_fence();
        completion_fence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (completion_fence_ == nullptr) { return fail("Failed to create terrain generator GPU completion fence"); }

        // Readback is delayed until the caller actually asks for it, which lets multiple chunks overlap GPU work.
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
            const auto wait_result = glClientWaitSync(
                completion_fence_,
                GL_SYNC_FLUSH_COMMANDS_BIT,
                wait_timeout_ns);

            if (wait_result == GL_ALREADY_SIGNALED ||
                wait_result == GL_CONDITION_SATISFIED)
                return {};

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

        // The buffers are persistently mapped, so this barrier is the handoff point from compute writes to CPU reads.
        glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

        const auto [boundary_vertex_count, boundary_vertex_overflowed] =
                boundary_vertex_counter_buffer_.read_one<GpuBoundaryVertexCounter>();
        const auto [vertex_count, index_count, edge_count, overflowed] = counters_buffer_.read_one<GpuCounters>();

        clear_completion_fence();
        pending_readback_ = false;

        const auto boundary_vertex_capacity = max_boundary_vertex_count(settings_);
        const auto mesh_vertex_capacity     = max_mesh_vertex_count(settings_);
        const auto mesh_index_capacity      = max_mesh_index_count(settings_);
        const auto boundary_edge_capacity   = max_boundary_edge_count(settings_);

        if (boundary_vertex_overflowed != 0u || overflowed != 0u)
        {
            return fail(
                "TerrainGenerator surface rebuild exceeded GPU buffer capacity (boundary vertices: {}/{}, mesh vertices: {}/{}, "
                "mesh indices: {}/{}, boundary edges: {}/{})",
                boundary_vertex_count,
                boundary_vertex_capacity,
                vertex_count,
                mesh_vertex_capacity,
                index_count,
                mesh_index_capacity,
                edge_count,
                boundary_edge_capacity);
        }

        if (boundary_vertex_count > boundary_vertex_capacity || vertex_count > mesh_vertex_capacity || index_count >
            mesh_index_capacity ||
            edge_count > boundary_edge_capacity)
        {
            return fail(
                "TerrainGenerator surface rebuild reported invalid GPU counts (boundary vertices: {}/{}, mesh vertices: {}/{}, "
                "mesh indices: {}/{}, boundary edges: {}/{})",
                boundary_vertex_count,
                boundary_vertex_capacity,
                vertex_count,
                mesh_vertex_capacity,
                index_count,
                mesh_index_capacity,
                edge_count,
                boundary_edge_capacity);
        }

        if (boundary_vertex_count > 0)
        {
            const auto gpu_boundary_vertices = boundary_vertices_buffer_.read<vec2>(boundary_vertex_count);
            result.boundary_vertices.reserve(gpu_boundary_vertices.size());
            for (const auto& vertex : gpu_boundary_vertices) { result.boundary_vertices.push_back(vertex); }
        }

        if (vertex_count > 0)
        {
            const auto gpu_mesh_vertices = mesh_vertices_buffer_.read<vec2>(vertex_count);
            result.mesh_vertices.reserve(gpu_mesh_vertices.size());
            for (const auto& vertex : gpu_mesh_vertices) { result.mesh_vertices.push_back(vertex); }
        }

        if (index_count > 0) { result.mesh_indices = mesh_indices_buffer_.read<std::uint32_t>(index_count); }

        if (edge_count > 0)
        {
            const auto gpu_boundary_edges = boundary_edges_buffer_.read<GpuBoundaryEdge>(edge_count);
            result.boundary_edges.reserve(gpu_boundary_edges.size());
            for (const auto& [a, b] : gpu_boundary_edges) { result.boundary_edges.emplace_back(a, b); }
        }

        return result;
    }
}
