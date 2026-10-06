// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>

#include "common/common_types.h"

namespace Common {

struct CorrectnessTelemetrySnapshot {
    u64 host_memory_bounds_violations{};
    u64 scheduler_context_guard_failures{};
    u64 unmapped_gpu_reads{};
    u64 unmapped_gpu_writes{};
};

class CorrectnessTelemetry {
public:
    static CorrectnessTelemetry& Get() noexcept {
        static CorrectnessTelemetry telemetry;
        return telemetry;
    }

    void RecordHostMemoryBoundsViolation() noexcept {
        host_memory_bounds_violations.fetch_add(1, std::memory_order_relaxed);
    }
    void RecordSchedulerContextGuardFailure() noexcept {
        scheduler_context_guard_failures.fetch_add(1, std::memory_order_relaxed);
    }
    void RecordUnmappedGpuRead() noexcept {
        unmapped_gpu_reads.fetch_add(1, std::memory_order_relaxed);
    }
    void RecordUnmappedGpuWrite() noexcept {
        unmapped_gpu_writes.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] CorrectnessTelemetrySnapshot Snapshot() const noexcept {
        return {
            .host_memory_bounds_violations =
                host_memory_bounds_violations.load(std::memory_order_relaxed),
            .scheduler_context_guard_failures =
                scheduler_context_guard_failures.load(std::memory_order_relaxed),
            .unmapped_gpu_reads = unmapped_gpu_reads.load(std::memory_order_relaxed),
            .unmapped_gpu_writes = unmapped_gpu_writes.load(std::memory_order_relaxed),
        };
    }

private:
    std::atomic<u64> host_memory_bounds_violations{};
    std::atomic<u64> scheduler_context_guard_failures{};
    std::atomic<u64> unmapped_gpu_reads{};
    std::atomic<u64> unmapped_gpu_writes{};
};

} // namespace Common
