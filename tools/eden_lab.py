#!/usr/bin/env python3
"""Eden Xclipse lab Phase 1: pinned ADB transport and sandbox-only transactions.

No game is launched. No application, system, save, NAND or shader-cache setting
is modified. A matching instrumented RelWithDebInfo build is required.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

PACKAGE = "dev.eden.eden_emulator.debug"
RUNNER = PACKAGE + ".test/androidx.test.runner.AndroidJUnitRunner"
REQUEST_RE = re.compile(r"[a-f0-9]{32}\Z")
HEX = re.compile(r"[a-f0-9]{64}\Z")
SERIAL_RE = re.compile(r"[A-Za-z0-9._:-]{1,128}\Z")
SANDBOX_KEYS = {"measurement_tag": str, "aggregate_telemetry": bool}
MAX_REQUEST_BYTES = 65536


class LabError(RuntimeError):
    pass


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise LabError(reason)


def validate_request(value: object) -> dict:
    """Reject unknown operations, keys, types, paths and non-canonical IDs."""
    require(type(value) is dict, "Request must be an object")
    common = {"protocol_version", "run_id", "operation"}
    require(common <= set(value), "Missing mandatory request fields")
    require(type(value["protocol_version"]) is int and
            value["protocol_version"] == 1, "Unsupported protocol")
    require(type(value["run_id"]) is str and
            bool(REQUEST_RE.fullmatch(value["run_id"])), "Invalid run ID")
    operation = value["operation"]
    require(type(operation) is str and operation in
            {"probe", "sandbox-roundtrip", "recover"}, "Operation not authorized")
    expected = common | ({"config"} if operation == "sandbox-roundtrip" else set())
    require(set(value) == expected, "Unknown or missing request keys")
    if operation == "sandbox-roundtrip":
        config = value["config"]
        require(type(config) is dict and set(config) == set(SANDBOX_KEYS),
                "Sandbox config requires all and only allowed keys")
        for key, cls in SANDBOX_KEYS.items():
            require(type(config[key]) is cls, "Wrong type for " + key)
        require(0 <= len(config["measurement_tag"]) <= 64 and
                all(32 <= ord(c) <= 126 for c in config["measurement_tag"]),
                "Invalid measurement tag")
    require(len(json.dumps(value, separators=(",", ":"), allow_nan=False).encode()) <
            MAX_REQUEST_BYTES, "Oversized request")
    return value


def parse_result(output: str, run_id: str) -> dict:
    # Android instrumentation encodes sendStatus("stream") as a status line.
    matched = re.findall(r"LAB_RESULT=(\{[^\n]*\})", output)
    require(len(matched) == 1, "Missing or duplicated bridge result")
    try:
        result = json.loads(matched[0])
    except ValueError as exc:
        raise LabError("Malformed bridge JSON") from exc
    require(type(result) is dict and result.get("run_id") == run_id and
            result.get("protocol_version") == 1, "Bridge identity mismatch")
    require(result.get("status") == "complete",
            "Lab operation failed: " + str(result.get("error", "unspecified")))
    return result


class Device:
    def __init__(self, serial: str, adb: str = "adb"):
        require(type(serial) is str and bool(SERIAL_RE.fullmatch(serial)),
                "Explicit safe ADB serial required")
        self.serial, self.adb = serial, adb

    def call(self, *args: str, data: bytes | None = None, timeout: int = 30,
             allow_failure: bool = False) -> str:
        try:
            result = subprocess.run([self.adb, "-s", self.serial, *args],
                                    input=data, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=timeout,
                                    check=False)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise LabError("ADB transport failed: " + str(exc)) from exc
        text = result.stdout.decode("utf-8", "replace")
        if result.returncode and not allow_failure:
            raise LabError("ADB failed (" + str(result.returncode) + "): " +
                           result.stderr.decode("utf-8", "replace")[:600])
        return text

    @contextmanager
    def lease(self):
        root = Path.home() / ".cache" / "eden-xclipse-lab"
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        lock_path = root / (self.serial.replace(":", "_") + ".lock")
        with lock_path.open("a+b") as fd:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as exc:
                raise LabError("Device is reserved by another lab process") from exc
            try:
                yield
            finally:
                fcntl.flock(fd, fcntl.LOCK_UN)

    def info(self) -> dict:
        require(self.call("get-state").strip() == "device",
                "Selected device is not online")
        fields = {"model": "ro.product.model", "soc": "ro.soc.model",
                  "android": "ro.build.version.release",
                  "fingerprint": "ro.build.fingerprint"}
        result = {key: self.call("shell", "getprop", prop).strip()
                  for key, prop in fields.items()}
        result["serial"] = self.serial
        return result

    def assert_idle_debug(self) -> None:
        # Phase 1 conservative guard: no app process may be present before
        # launching instrumentation. Full UID/process tracking remains pending.
        active = self.call("shell", "pidof", PACKAGE, allow_failure=True).strip()
        require(not active, "Eden process is running; close it before lab instrumentation")
        identity = self.call("shell", "run-as", PACKAGE, "id", "-u").strip()
        require(identity.isdigit(), "Matching debuggable Eden package not installed")

    def bridge(self, request: dict) -> dict:
        validate_request(request)
        run_id = request["run_id"]
        path = "cache/eden-lab-" + run_id + ".json"
        raw = json.dumps(request, separators=(",", ":"), allow_nan=False).encode()
        # The destination is generated from a validated UUID; user content is
        # transferred on stdin, never interpolated into a shell command.
        command = "cat > " + path
        self.call("shell", "-T", "run-as", PACKAGE, "sh", "-c", command,
                  data=raw, timeout=30)
        try:
            response = self.call("shell", "am", "instrument", "-w", "-r",
                                 "-e", "lab_request", path, RUNNER, timeout=180)
            return parse_result(response, run_id)
        finally:
            self.call("shell", "run-as", PACKAGE, "rm", "-f", path,
                      allow_failure=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", help="Required explicit ADB device serial")
    parser.add_argument("--adb", default="adb")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("device-info")
    sub.add_parser("probe")
    sub.add_parser("recover")
    validate = sub.add_parser("validate-request")
    validate.add_argument("--file", required=True)
    roundtrip = sub.add_parser("sandbox-roundtrip")
    roundtrip.add_argument("--tag", default="")
    roundtrip.add_argument("--aggregate-telemetry", action="store_true")
    roundtrip.add_argument("--allow-test-writes", action="store_true",
                           help="Authorize sandbox-only app-private writes")
    args = parser.parse_args(argv)
    try:
        if args.command == "validate-request":
            result = validate_request(json.loads(Path(args.file).read_text()))
        else:
            require(bool(args.serial), "--serial is required for device operations")
            device = Device(args.serial, args.adb)
            with device.lease():
                if args.command == "device-info":
                    result = device.info()
                else:
                    if args.command == "sandbox-roundtrip":
                        require(args.allow_test_writes,
                                "Sandbox mutation requires --allow-test-writes")
                    device.info()
                    device.assert_idle_debug()
                    op = args.command
                    request = {"protocol_version": 1, "run_id": uuid.uuid4().hex,
                               "operation": op}
                    if op == "sandbox-roundtrip":
                        request["config"] = {
                            "measurement_tag": args.tag,
                            "aggregate_telemetry": args.aggregate_telemetry}
                    result = device.bridge(request)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (LabError, ValueError, OSError) as exc:
        print(json.dumps({"status": "failed", "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
