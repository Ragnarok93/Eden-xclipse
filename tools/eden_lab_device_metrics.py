#!/usr/bin/env python3
"""Read-only Android process, battery and thermal-service observation.

The battery sensor does not represent GPU die temperature. Restricted or
unparseable readings are marked unavailable, never silently set to zero.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import re
import time

from eden_lab import Device, PACKAGE


def metric(raw: str, pattern: str, *, divisor: float = 1.0,
           min_value: float = 0, max_value: float = 1e15) -> dict:
    match = re.search(pattern, raw, re.MULTILINE | re.IGNORECASE)
    if match is None:
        return {"available": False, "value": None}
    try:
        value = float(match.group(1).replace(",", "")) / divisor
    except (ValueError, OverflowError):
        return {"available": False, "value": None}
    if not (min_value <= value <= max_value):
        return {"available": False, "value": None}
    return {"available": True, "value": value}


def digest(raw: str) -> dict:
    return {"sha256": hashlib.sha256(raw.encode("utf-8")).hexdigest(),
            "bytes": len(raw.encode("utf-8")), "available": bool(raw.strip())}


def capture(device: Device) -> dict:
    start = time.monotonic_ns()
    recorded = datetime.now(timezone.utc).isoformat()
    state = device.call("get-state").strip()
    if state != "device":
        raise RuntimeError("Selected ADB device is unavailable")
    # No root, system-property modification, APK installation, process kill
    # or app data access: these calls are shell-level, read-only observations.
    active = device.call("shell", "pidof", PACKAGE, allow_failure=True).strip()
    mem = device.call("shell", "dumpsys", "meminfo", PACKAGE,
                      allow_failure=True)[:262144]
    thermal = device.call("shell", "dumpsys", "thermalservice",
                          allow_failure=True)[:262144]
    battery = device.call("shell", "dumpsys", "battery",
                          allow_failure=True)[:65536]
    uptime = device.call("shell", "cat", "/proc/uptime",
                         allow_failure=True)[:4096]
    end = time.monotonic_ns()
    return {
        "schema_version": 1,
        "serial": device.serial,
        "package": PACKAGE,
        "sample_utc": recorded,
        "host_monotonic_start_ns": start,
        "host_monotonic_end_ns": end,
        "device_uptime_seconds": metric(
            uptime, r"^\s*([0-9]+(?:[.][0-9]+)?)",
            max_value=1e10),
        "package_process_present": bool(active),
        "package_pss_kib": metric(
            mem, r"^\s*TOTAL PSS:\s*([0-9,]+)", max_value=100e6),
        "package_rss_kib": metric(
            mem, r"^\s*TOTAL RSS:\s*([0-9,]+)", max_value=100e6),
        "battery_temperature_c": metric(
            battery, r"^\s*temperature:\s*([0-9]+)\s*$", divisor=10,
            min_value=0, max_value=110),
        "battery_level_pct": metric(
            battery, r"^\s*level:\s*([0-9]+)\s*$", max_value=100),
        "thermal_severity_android": metric(
            thermal, r"^\s*(?:Thermal Status|mCurrentStatus):\s*([0-9]+)\s*$",
            max_value=6),
        "gpu_die_temperature_c": {"available": False, "value": None,
                                   "reason": "no_validated_privileged_gpu_sensor"},
        "raw_dumpsys_digests": {
            "memory": digest(mem), "thermal_service": digest(thermal),
            "battery": digest(battery)},
        "clock_domain_note": (
            "host_monotonic_ns is not Android elapsedRealtimeNanos; "
            "cross-clock alignment requires measured synchronization"),
        "collector_has_mutated_device": False,
    }
