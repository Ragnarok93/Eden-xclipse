# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic comparator fixtures test rejection rules, not real device speed."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import eden_lab_campaign as lab


def fixture():
    common = {key: "reference" for key in lab.COMMON_KEYS}
    common.update({"title_id": "0100F2C0115B6000",
                   "mods_sha256": "a" * 64, "checkpoint_sha256": "b" * 64})

    def runs(which):
        result = []
        for state in ("cold", "warm"):
            for i in range(3):
                is_candidate = which == "candidate"
                delta = int(is_candidate) * 10
                result.append({
                    "run_id": ("%032x" % (len(result) + 1 + (100 if is_candidate else 0))),
                    "cache_state": state,
                    "native_game_frames": 600 + delta * 3,
                    "elapsed_ns": 20_000_000_000,
                    "game_frametime_p95_ns": 30_000_000 - delta * 100_000,
                    "game_frametime_p99_ns": 40_000_000 - delta * 100_000,
                    "correctness_errors": 0,
                    "crashed": False,
                    "thermal_start_c": 39.0,
                    "thermal_end_c": 41.0,
                    "evidence_sha256": ("%064x" % (len(result) + 1 +
                                                     (100 if is_candidate else 0))),
                    "frametime_source": "game_frame_timestamps",
                    "measurement_origin": "physical_device",
                    "device_fingerprint": common["android_fingerprint"],
                    "vulkan_driver_version": common["vulkan_driver_version"],
                    "title_id": common["title_id"],
                    "checkpoint_sha256": common["checkpoint_sha256"],
                    "binary_sha256": "d" * 64 if is_candidate else "c" * 64,
                    "effective_policy_sha256": "f" * 64 if is_candidate else "e" * 64,
                })
        return result

    return {
        "schema_version": 1,
        "common": common,
        "variants": {
            "baseline": {
                "commit_sha": "a" * 40, "binary_sha256": "c" * 64,
                "effective_policy_sha256": "e" * 64, "runs": runs("baseline")},
            "candidate": {
                "commit_sha": "b" * 40, "binary_sha256": "d" * 64,
                "effective_policy_sha256": "f" * 64, "runs": runs("candidate")},
        },
    }


class EvidenceValidationTests(unittest.TestCase):
    def test_eligible_result_requires_review(self):
        result = lab.compare(fixture())
        self.assertEqual(result["verdict"], "candidate_improvement_needs_review")
        self.assertFalse(result["automatically_promotable"])
        self.assertEqual(result["measurement_claim"], "unverified_external_evidence")

    def test_missing_trials(self):
        data = fixture()
        data["variants"]["candidate"]["runs"].pop()
        with self.assertRaises(lab.EvidenceError):
            lab.compare(data)

    def test_duplicate_trial_id(self):
        data = fixture()
        data["variants"]["candidate"]["runs"][0]["run_id"] = (
            data["variants"]["baseline"]["runs"][0]["run_id"])
        with self.assertRaises(lab.EvidenceError):
            lab.compare(data)

    def test_missing_metrics_and_non_native_source(self):
        for mutate in (
            lambda run: run.pop("game_frametime_p95_ns"),
            lambda run: run.update(frametime_source="surfaceflinger"),
            lambda run: run.update(measurement_origin="synthetic"),
            lambda run: run.update(elapsed_ns=0),
            lambda run: run.update(crashed=1),
        ):
            with self.subTest(mutate=mutate):
                data = fixture()
                mutate(data["variants"]["candidate"]["runs"][0])
                with self.assertRaises(lab.EvidenceError):
                    lab.compare(data)

    def test_crash_or_correctness_failure(self):
        for key, value in (("crashed", True), ("correctness_errors", 1)):
            data = fixture()
            data["variants"]["candidate"]["runs"][0][key] = value
            with self.assertRaises(lab.EvidenceError):
                lab.compare(data)

    def test_thermal_mismatch_overrules_improvement(self):
        data = fixture()
        for run in data["variants"]["candidate"]["runs"]:
            run["thermal_start_c"] = 45
        self.assertEqual(lab.compare(data)["verdict"], "thermally_mismatched")

    def test_detect_regression(self):
        data = fixture()
        for run in data["variants"]["candidate"]["runs"]:
            run["native_game_frames"] = 400
        self.assertEqual(lab.compare(data)["verdict"], "candidate_regression")

    def test_noop_variant_not_comparable(self):
        data = fixture()
        data["variants"]["candidate"]["commit_sha"] = "a" * 40
        data["variants"]["candidate"]["binary_sha256"] = "c" * 64
        data["variants"]["candidate"]["effective_policy_sha256"] = "e" * 64
        with self.assertRaises(lab.EvidenceError):
            lab.compare(data)

    def test_reject_runtime_attestation_mismatch(self):
        for key, value in (
            ("binary_sha256", "0" * 64),
            ("device_fingerprint", "another-device"),
            ("vulkan_driver_version", "older-driver"),
            ("effective_policy_sha256", "9" * 64),
            ("checkpoint_sha256", "1" * 64),
        ):
            data = fixture()
            data["variants"]["candidate"]["runs"][0][key] = value
            with self.subTest(key=key), self.assertRaises(lab.EvidenceError):
                lab.compare(data)

    def test_unknown_controls_not_accepted(self):
        data = fixture()
        data["common"]["extra"] = "unexpected"
        with self.assertRaises(lab.EvidenceError):
            lab.compare(data)


if __name__ == "__main__":
    unittest.main()
