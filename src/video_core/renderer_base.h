// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "core/frontend/framebuffer_layout.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"

namespace Core::Frontend {
class EmuWindow;
class GraphicsContext;
} // namespace Core::Frontend

namespace VideoCore {

struct RendererSettings {
    // Screenshot
    std::atomic<bool> screenshot_requested{false};
    void* screenshot_bits{};
    std::function<void(bool)> screenshot_complete_callback;
    Layout::FramebufferLayout screenshot_framebuffer_layout;
    Service::Nvnflinger::LayerStackId screenshot_layer_stack{Service::Nvnflinger::LayerStackId::Default};
};

/// Copy-only, bounded diagnostic snapshot. Not a GPU-duration measurement.
struct BenchmarkRendererCounters {
    u64 frame_count{};
    u64 pipeline_creates{};
    u64 pipeline_failures{};
    u64 runtime_pipeline_map_hits{};
    u64 runtime_pipeline_map_misses{};
    u64 pipeline_create_count{};
    u64 pipeline_create_p95_upper_ns{};
    u64 pipeline_block_count{};
    u64 pipeline_block_p95_upper_ns{};
    u64 pipeline_queue_residence_count{};
    u64 pipeline_queue_residence_p95_upper_ns{};
    u64 shader_decode_count{};
    u64 shader_decode_p95_upper_ns{};
    u64 spirv_generation_count{};
    u64 spirv_generation_p95_upper_ns{};
    u64 queue_submits{};
    u64 host_waits{};
    u64 timeline_waits{};
    u64 bcn_gpu_decode_dispatches{};
    u64 bcn_cpu_fallbacks{};
    u64 descriptor_stalls{};
    u64 image_native_copies{};
    u64 dref_unresolved_bindings{};
    bool aggregate_telemetry_enabled{};
};

class RendererBase {
public:
    YUZU_NON_COPYABLE(RendererBase);
    YUZU_NON_MOVEABLE(RendererBase);

    explicit RendererBase(Core::Frontend::EmuWindow& window,
                          std::unique_ptr<Core::Frontend::GraphicsContext> context);
    virtual ~RendererBase();

    /// Finalize rendering the guest frame and draw into the presentation texture
    virtual void Composite(std::span<const Tegra::FramebufferConfig> layers) = 0;

    /// Get the tiled applet layer capture buffer
    virtual std::vector<u8> GetAppletCaptureBuffer() = 0;

    [[nodiscard]] virtual RasterizerInterface* ReadRasterizer() = 0;

    [[nodiscard]] virtual std::string GetDeviceVendor() const = 0;

    /// Optional, read-only renderer stats. Never submits GPU work or resets counters.
    [[nodiscard]] virtual std::optional<BenchmarkRendererCounters>
    GetBenchmarkRendererCounters() const {
        return std::nullopt;
    }

    // Getter/setter functions:
    // ------------------------

    [[nodiscard]] f32 GetCurrentFPS() const {
        return m_current_fps;
    }

    [[nodiscard]] int GetCurrentFrame() const {
        return m_current_frame;
    }

    [[nodiscard]] Core::Frontend::GraphicsContext& Context() {
        return *context;
    }

    [[nodiscard]] const Core::Frontend::GraphicsContext& Context() const {
        return *context;
    }

    [[nodiscard]] Core::Frontend::EmuWindow& GetRenderWindow() {
        return render_window;
    }

    [[nodiscard]] const Core::Frontend::EmuWindow& GetRenderWindow() const {
        return render_window;
    }

    [[nodiscard]] RendererSettings& Settings() {
        return renderer_settings;
    }

    [[nodiscard]] const RendererSettings& Settings() const {
        return renderer_settings;
    }

    /// Refreshes the settings common to all renderers
    void RefreshBaseSettings();

    /// Returns true if a screenshot is being processed
    bool IsScreenshotPending() const;

    /// Request a screenshot of the next frame.
    void RequestScreenshot(void* data, std::function<void(bool)> callback,
                           const Layout::FramebufferLayout& layout,
                           Service::Nvnflinger::LayerStackId layer_stack =
                               Service::Nvnflinger::LayerStackId::Default);

protected:
    Core::Frontend::EmuWindow& render_window; ///< Reference to the render window handle.
    std::unique_ptr<Core::Frontend::GraphicsContext> context;
    f32 m_current_fps = 0.0f; ///< Current framerate, should be set by the renderer
    int m_current_frame = 0;  ///< Current frame, should be set by the renderer

    RendererSettings renderer_settings;

private:
    /// Updates the framebuffer layout of the contained render window handle.
    void UpdateCurrentFramebufferLayout();
};

} // namespace VideoCore
