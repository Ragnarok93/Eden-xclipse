#!/usr/bin/env python3
"""Offline, fail-closed comparison of Eden Xclipse device-lab evidence.

This module does not read the emulator, connect to ADB, or infer measurements.
All inputs are purported observations; caller must independently verify hashes,
source identity, signatures and raw device evidence before accepting a result.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import statistics
import sys

HEX64 = re.compile(r"[a-f0-9]{64}\Z")
RUN_ID = re.compile(r"[a-f0-9]{32}\Z")
MAX_SIZE = 2 * 1024 * 1024
MIN_TRIALS = 3
COMMON_KEYS = {
    "serial", "model", "soc", "gpu", "android_fingerprint", "vulkan_driver",
    "vulkan_driver_version", "pipeline_cache_uuid", "title_id", "title_version",
    "mods_sha256", "checkpoint_sha256", "cache_policy", "workload_version",
}
RUN_KEYS = {
    "run_id", "cache_state", "native_game_frames", "elapsed_ns",
    "game_frametime_p95_ns", "game_frametime_p99_ns", "correctness_errors",
    "crashed", "thermal_start_c", "thermal_end_c", "evidence_sha256",
    "frametime_source", "measurement_origin", "device_fingerprint",
    "vulkan_driver_version", "title_id", "checkpoint_sha256",
    "binary_sha256", "effective_policy_sha256",
}


class EvidenceError(ValueError):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise EvidenceError(message)


def sha(value: object, name: str) -> None:
    check(type(value) is str and HEX64.fullmatch(value) is not None,
          "Missing/invalid SHA256: " + name)


def number(value: object, name: str, low: float, high: float) -> None:
    check(type(value) in (int, float) and low <= value <= high,
          "Out of range: " + name)


def run_valid(run: object, seen: set[str], variant: dict, common: dict) -> None:
    check(type(run) is dict and set(run) == RUN_KEYS, "Invalid trial schema")
    key = run["run_id"]
    check(type(key) is str and RUN_ID.fullmatch(key) is not None and key not in seen,
          "Invalid or reused trial ID")
    seen.add(key)
    check(run["cache_state"] in ("cold", "warm"), "Invalid cache state")
    check(run["frametime_source"] in ("game_frame_timestamps",),
          "Frametime percentiles need measured game-frame timestamps")
    check(run["measurement_origin"] == "physical_device",
          "Synthetic or unknown evidence origin")
    for k in ("native_game_frames", "elapsed_ns", "game_frametime_p95_ns",
              "game_frametime_p99_ns", "correctness_errors"):
        check(type(run[k]) is int, "Invalid count: " + k)
    number(run["native_game_frames"], "native_game_frames", 1, 100000000)
    number(run["elapsed_ns"], "elapsed_ns", 1_000_000_000, 3_600_000_000_000)
    number(run["game_frametime_p95_ns"], "game_frametime_p95_ns", 1, 5_000_000_000)
    number(run["game_frametime_p99_ns"], "game_frametime_p99_ns",
           run["game_frametime_p95_ns"], 10_000_000_000)
    number(run["correctness_errors"], "correctness_errors", 0, 10**9)
    check(type(run["crashed"]) is bool, "Invalid crash flag")
    for key in ("thermal_start_c", "thermal_end_c"):
        number(run[key], key, -30, 130)
    sha(run["evidence_sha256"], "evidence_sha256")
    attested = {
        "device_fingerprint": common["android_fingerprint"],
        "vulkan_driver_version": common["vulkan_driver_version"],
        "title_id": common["title_id"],
        "checkpoint_sha256": common["checkpoint_sha256"],
        "binary_sha256": variant["binary_sha256"],
        "effective_policy_sha256": variant["effective_policy_sha256"],
    }
    for key, expected in attested.items():
        check(type(run[key]) is str and run[key] == expected,
              "Runtime attestation mismatch: " + key)


def validate(campaign: object) -> dict:
    check(type(campaign) is dict and set(campaign) ==
          {"schema_version", "common", "variants"}, "Invalid campaign schema")
    check(type(campaign["schema_version"]) is int and
          campaign["schema_version"] == 1, "Unsupported schema version")
    common = campaign["common"]
    check(type(common) is dict and set(common) == COMMON_KEYS,
          "Missing or extra common controls")
    for key in COMMON_KEYS:
        check(type(common[key]) is str and 0 < len(common[key]) <= 256,
              "Missing common control: " + key)
    for key in ("mods_sha256", "checkpoint_sha256"):
        sha(common[key], key)
    check(re.fullmatch(r"[A-Fa-f0-9]{16}", common["title_id"]) is not None,
          "Invalid title id")
    variants = campaign["variants"]
    check(type(variants) is dict and set(variants) == {"baseline", "candidate"},
          "Missing baseline or candidate")
    seen: set[str] = set()
    all_binary_ids = []
    for kind, variant in variants.items():
        check(type(variant) is dict and set(variant) ==
              {"commit_sha", "binary_sha256", "effective_policy_sha256", "runs"},
              "Invalid variant schema: " + kind)
        check(type(variant["commit_sha"]) is str and
              re.fullmatch("[a-f0-9]{40}", variant["commit_sha"]) is not None,
              "Commit hash not pinned")
        for key in ("binary_sha256", "effective_policy_sha256"):
            sha(variant[key], kind + "." + key)
        all_binary_ids.append((variant["commit_sha"], variant["binary_sha256"],
                               variant["effective_policy_sha256"]))
        runs = variant["runs"]
        check(type(runs) is list and len(runs) <= 100, "Invalid trial list")
        for run in runs:
            run_valid(run, seen, variant, common)
        for cache_state in ("cold", "warm"):
            check(sum(r["cache_state"] == cache_state for r in runs) >= MIN_TRIALS,
                  "Insufficient " + cache_state + " trials for " + kind)
        check(not any(r["crashed"] or r["correctness_errors"] for r in runs),
              "Crashes or correctness faults invalidate " + kind)
    check(all_binary_ids[0] != all_binary_ids[1], "Unchanged baseline/candidate")
    return campaign


def summarize(runs: list[dict], cache_state: str) -> dict:
    filtered = [r for r in runs if r["cache_state"] == cache_state]
    median = statistics.median
    return {
        "trials": len(filtered),
        "game_fps": median(1e9 * r["native_game_frames"] / r["elapsed_ns"]
                           for r in filtered),
        "game_frametime_p95_ms": median(r["game_frametime_p95_ns"] / 1e6
                                       for r in filtered),
        "game_frametime_p99_ms": median(r["game_frametime_p99_ns"] / 1e6
                                       for r in filtered),
        "thermal_start_c": median(r["thermal_start_c"] for r in filtered),
        "thermal_end_c": median(r["thermal_end_c"] for r in filtered),
    }


def compare(campaign: object) -> dict:
    obj = validate(campaign)
    grouped = {
        mode: {state: summarize(variant["runs"], state)
               for state in ("cold", "warm")}
        for mode, variant in obj["variants"].items()
    }
    decisions = []
    details = {}
    for state in ("cold", "warm"):
        base, candidate = grouped["baseline"][state], grouped["candidate"][state]
        def change(key: str) -> float:
            return 100 * (candidate[key] / base[key] - 1)
        fps_pct = change("game_fps")
        p95_pct = change("game_frametime_p95_ms")
        p99_pct = change("game_frametime_p99_ms")
        temp_delta = candidate["thermal_start_c"] - base["thermal_start_c"]
        details[state] = {"fps_delta_pct": round(fps_pct, 3),
                          "p95_delta_pct": round(p95_pct, 3),
                          "p99_delta_pct": round(p99_pct, 3),
                          "start_temperature_delta_c": round(temp_delta, 3)}
        if abs(temp_delta) > 2.0:
            decisions.append("thermally_mismatched")
        elif fps_pct < -3.0 or p95_pct > 5.0 or p99_pct > 5.0:
            decisions.append("candidate_regression")
        elif fps_pct >= 3.0 and p95_pct <= 0 and p99_pct <= 5.0:
            decisions.append("candidate_improvement_needs_review")
        else:
            decisions.append("inconclusive")
    priority = ("thermally_mismatched", "candidate_regression",
                "inconclusive", "candidate_improvement_needs_review")
    verdict = next(x for x in priority if x in decisions)
    return {
        "verdict": verdict,
        "automatically_promotable": False,
        "measurement_claim": "unverified_external_evidence",
        "conditions": details,
        "summaries": grouped,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.campaign.stat().st_size > MAX_SIZE:
            raise EvidenceError("Campaign exceeds size limit")
        result = compare(json.loads(args.campaign.read_text()))
        rendered = json.dumps(result, indent=2, sort_keys=True)
        if args.output:
            args.output.write_text(rendered + "\n")
        print(rendered)
        return 0
    except (OSError, EvidenceError, ValueError) as exc:
        print(json.dumps({"verdict": "invalid", "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
