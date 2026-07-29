#!/usr/bin/env python3
"""Capture, label, replay and evaluate RSSI motion experiments."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import select
import statistics
import subprocess
import sys
import termios
import time
import uuid

SCHEMA_VERSION = 1
SERIAL_BACKLOG_DRAIN_SECONDS = 0.5
TELEMETRY_FIELDS = (
    "uptime_ms", "sample_ok", "rssi_dbm", "read_error", "rssi_changed",
    "detection_source",
    "algorithm", "baseline_mode", "profile", "query_interval_ms",
    "rssi_change_interval_ms", "rssi_unchanged_ms", "baseline",
    "baseline_spread", "score", "threshold", "release_threshold", "state",
    "transition", "calibrated", "marker_event_id", "marker_active",
    "marker_transition", "csi_valid", "csi_age_ms", "csi_rssi_dbm",
    "csi_length", "csi_subcarriers", "csi_mean_amplitude",
    "csi_amplitude_variance", "csi_amplitude_range", "csi_mean_energy",
    "csi_temporal_delta", "csi_temporal_delta_mean",
    "csi_temporal_delta_peak", "csi_interval_frames",
    "csi_normalized_delta", "csi_normalized_delta_mean",
    "csi_normalized_delta_peak",
    "csi_complex_distance", "csi_complex_distance_mean",
    "csi_complex_distance_peak",
    "csi_frames_received", "csi_frames_processed",
    "csi_frames_dropped", "csi_detector_sampled", "csi_detector_calibrated",
    "csi_detector_score", "csi_detector_threshold", "csi_detector_state",
    "csi_detector_transition", "csi_traffic_running", "csi_traffic_interval_ms",
    "csi_traffic_payload_bytes", "csi_traffic_requests",
    "csi_traffic_replies", "csi_traffic_timeouts", "queries", "samples_ok", "read_errors",
    "schedule_misses", "repeated_samples", "rssi_changes", "disconnects",
    "reconnects", "channel", "bssid",
)
ALGORITHMS = (
    "mean_absolute_difference",
    "standard_deviation",
    "sample_variance",
    "range",
    "median_absolute_deviation",
)
BASELINES = ("mean_stddev", "median_mad")
PROFILES = ("low", "balanced", "high")
REPLAY_SAMPLE_FIELDS = (
    "rssi_dbm",
    "csi_mean_amplitude",
    "csi_amplitude_variance",
    "csi_amplitude_range",
    "csi_mean_energy",
    "csi_temporal_delta",
    "csi_temporal_delta_mean",
    "csi_temporal_delta_peak",
    "csi_normalized_delta",
    "csi_normalized_delta_mean",
    "csi_normalized_delta_peak",
    "csi_complex_distance",
    "csi_complex_distance_mean",
    "csi_complex_distance_peak",
)
METADATA_FIELDS = (
    "experiment_schema",
    "session_id",
    "split",
    "scenario",
    "event_id",
    "event_start_ms",
    "event_end_ms",
    "truth_active",
    "elapsed_ms",
)


def parse_event(value: str) -> tuple[str, int, int]:
    try:
        event_id, start, end = value.split(":", 2)
        start_ms = round(float(start) * 1000)
        end_ms = round(float(end) * 1000)
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError(
            "event must be ID:START_SECONDS:END_SECONDS"
        ) from error
    if not event_id or start_ms < 0 or end_ms <= start_ms:
        raise argparse.ArgumentTypeError("event range is invalid")
    return event_id, start_ms, end_ms


def validate_events(events: list[tuple[str, int, int]]) -> None:
    identifiers: set[str] = set()
    previous_end = -1
    for event_id, start, end in sorted(events, key=lambda item: item[1]):
        if event_id in identifiers:
            raise ValueError(f"duplicate event id: {event_id}")
        if start < previous_end:
            raise ValueError("event ranges must not overlap")
        identifiers.add(event_id)
        previous_end = end


def telemetry_rows(lines) -> tuple[list[str], list[dict[str, str]]]:
    header: list[str] | None = None
    rows: list[dict[str, str]] = []
    for raw_item in lines:
        if isinstance(raw_item, tuple):
            raw_line, capture_elapsed_ms = raw_item
        else:
            raw_line = raw_item
            capture_elapsed_ms = None
        line = raw_line.strip("\r\n")
        if line.startswith("uptime_ms,"):
            header = next(csv.reader([line]))
            continue
        if header is None or not line[:1].isdigit():
            if not line[:1].isdigit():
                continue
            header = list(TELEMETRY_FIELDS)
        values = next(csv.reader([line]))
        if len(values) != len(header):
            continue
        row = dict(zip(header, values))
        try:
            int(row["uptime_ms"])
        except (KeyError, ValueError):
            continue
        if capture_elapsed_ms is not None:
            row["_capture_elapsed_ms"] = str(capture_elapsed_ms)
        rows.append(row)
    if header is None:
        raise ValueError("telemetry header not found")
    if not rows:
        raise ValueError("no valid telemetry rows found")
    return header, rows


def apply_event_command(
    command: str,
    elapsed_ms: int,
    events: list[tuple[str, int, int]],
    active_events: dict[str, int],
) -> str:
    parts = command.strip().split()
    if len(parts) != 2 or parts[0] not in ("event-start", "event-end"):
        raise ValueError("event command must be: event-start ID or event-end ID")
    action, event_id = parts
    if any(character in event_id for character in ",:"):
        raise ValueError("event id cannot contain comma or colon")
    if action == "event-start":
        known = {identifier for identifier, _, _ in events}
        if active_events or event_id in known:
            raise ValueError("an event is already active or the id was already used")
        active_events[event_id] = elapsed_ms
        return f"GUIDE event_started event={event_id} elapsed_ms={elapsed_ms}"
    if event_id not in active_events:
        raise ValueError(f"event is not active: {event_id}")
    start_ms = active_events.pop(event_id)
    if elapsed_ms <= start_ms:
        raise ValueError("event end must be after its start")
    events.append((event_id, start_ms, elapsed_ms))
    return f"GUIDE event_finished event={event_id} elapsed_ms={elapsed_ms}"


def capture_stop_requested(command: str,
                           active_events: dict[str, int]) -> bool:
    if command.strip() != "capture-stop":
        return False
    if active_events:
        raise ValueError("cannot stop capture while an event is active")
    return True


def serial_lines(
    port: str,
    duration_seconds: float,
    guided_events: list[tuple[str, int, int]] | None = None,
    interactive_events: list[tuple[str, int, int]] | None = None,
):
    descriptor = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attributes = termios.tcgetattr(descriptor)
        attributes[0] = 0
        attributes[1] = 0
        attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attributes[3] = 0
        attributes[4] = termios.B115200
        attributes[5] = termios.B115200
        termios.tcsetattr(descriptor, termios.TCSANOW, attributes)
        termios.tcflush(descriptor, termios.TCIFLUSH)

        drain_deadline = time.monotonic() + SERIAL_BACKLOG_DRAIN_SECONDS
        while time.monotonic() < drain_deadline:
            timeout = max(0.0, drain_deadline - time.monotonic())
            readable, _, _ = select.select([descriptor], [], [], timeout)
            if descriptor in readable:
                os.read(descriptor, 4096)

        started = time.monotonic()
        deadline = started + duration_seconds
        event_stages = {event_id: 0 for event_id, _, _ in guided_events or []}
        active_events: dict[str, int] = {}
        command_pending = b""
        stdin_descriptor = (
            sys.stdin.fileno() if interactive_events is not None else None
        )
        if guided_events is not None or interactive_events is not None:
            print(
                f"GUIDE capture_started duration={duration_seconds:g}s",
                file=sys.stderr,
                flush=True,
            )
        yield (",".join(TELEMETRY_FIELDS) + "\n", 0)
        pending = b""
        while time.monotonic() < deadline:
            elapsed_ms = round((time.monotonic() - started) * 1000)
            for event_id, start_ms, end_ms in guided_events or []:
                stage = event_stages[event_id]
                if stage == 0 and elapsed_ms >= max(0, start_ms - 10000):
                    print(
                        f"GUIDE prepare event={event_id} starts_in=10s",
                        file=sys.stderr,
                        flush=True,
                    )
                    event_stages[event_id] = 1
                    stage = 1
                if stage == 1 and elapsed_ms >= start_ms:
                    print(
                        f"GUIDE cross_now event={event_id}",
                        file=sys.stderr,
                        flush=True,
                    )
                    event_stages[event_id] = 2
                    stage = 2
                if stage == 2 and elapsed_ms >= end_ms:
                    print(
                        f"GUIDE event_finished event={event_id}",
                        file=sys.stderr,
                        flush=True,
                    )
                    event_stages[event_id] = 3
            timeout = min(0.5, max(0.0, deadline - time.monotonic()))
            descriptors = [descriptor]
            if stdin_descriptor is not None:
                descriptors.append(stdin_descriptor)
            readable, _, _ = select.select(descriptors, [], [], timeout)
            if not readable:
                continue
            if stdin_descriptor is not None and stdin_descriptor in readable:
                command_chunk = os.read(stdin_descriptor, 4096)
                if not command_chunk:
                    stdin_descriptor = None
                else:
                    command_pending += command_chunk
                    while b"\n" in command_pending:
                        command_line, command_pending = command_pending.split(
                            b"\n", 1
                        )
                        decoded_command = command_line.decode(
                            "utf-8", errors="replace"
                        ).strip()
                        if capture_stop_requested(decoded_command,
                                                  active_events):
                            print(
                                "GUIDE capture_stopped",
                                file=sys.stderr,
                                flush=True,
                            )
                            return
                        elapsed_ms = round(
                            (time.monotonic() - started) * 1000
                        )
                        message = apply_event_command(
                            decoded_command,
                            elapsed_ms,
                            interactive_events,
                            active_events,
                        )
                        print(message, file=sys.stderr, flush=True)
            if descriptor not in readable:
                continue
            chunk = os.read(descriptor, 4096)
            if not chunk:
                continue
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                elapsed_ms = round((time.monotonic() - started) * 1000)
                yield (line.decode("utf-8", errors="replace") + "\n",
                       elapsed_ms)
        if active_events:
            raise ValueError("capture ended with an active interactive event")
        if pending:
            elapsed_ms = round((time.monotonic() - started) * 1000)
            yield (pending.decode("utf-8", errors="replace"), elapsed_ms)
    finally:
        os.close(descriptor)


def label_rows(
    rows: list[dict[str, str]],
    session_id: str,
    split: str,
    scenario: str,
    events: list[tuple[str, int, int]],
) -> list[dict[str, str]]:
    validate_events(events)
    first_uptime = int(rows[0]["uptime_ms"])
    labeled: list[dict[str, str]] = []
    for source in rows:
        if "_capture_elapsed_ms" in source:
            elapsed = int(source["_capture_elapsed_ms"])
        else:
            elapsed = int(source["uptime_ms"]) - first_uptime
        if elapsed < 0:
            raise ValueError("uptime is not monotonic")
        event_name = ""
        event_start = ""
        event_end = ""
        for event_id, start, end in events:
            if start <= elapsed < end:
                event_name = event_id
                event_start = str(start)
                event_end = str(end)
                break
        metadata = {
            "experiment_schema": str(SCHEMA_VERSION),
            "session_id": session_id,
            "split": split,
            "scenario": scenario,
            "event_id": event_name,
            "event_start_ms": event_start,
            "event_end_ms": event_end,
            "truth_active": "1" if event_name else "0",
            "elapsed_ms": str(elapsed),
        }
        telemetry = {
            key: value for key, value in source.items()
            if key != "_capture_elapsed_ms"
        }
        labeled.append(metadata | telemetry)
    return labeled


def marker_events_from_rows(
    rows: list[dict[str, str]],
) -> list[tuple[str, int, int]]:
    required = {"marker_event_id", "marker_active", "_capture_elapsed_ms"}
    if not rows or not required.issubset(rows[0]):
        raise ValueError("physical marker fields not found in live telemetry")

    events: list[tuple[str, int, int]] = []
    active_id: str | None = None
    active_start = 0
    previous_active = False
    for row in rows:
        active = row["marker_active"] == "1"
        elapsed = int(row["_capture_elapsed_ms"])
        if active and not previous_active:
            if active_id is not None:
                raise ValueError("physical marker events overlap")
            marker_id = int(row["marker_event_id"])
            if marker_id <= 0:
                raise ValueError("physical marker event id is invalid")
            active_id = f"boot-{marker_id}"
            active_start = elapsed
        elif not active and previous_active:
            if active_id is None or elapsed <= active_start:
                raise ValueError("physical marker end has no valid start")
            events.append((active_id, active_start, elapsed))
            active_id = None
        previous_active = active

    if active_id is not None:
        raise ValueError("capture ended with an open physical marker event")
    if not events:
        raise ValueError("no complete physical marker events found")
    validate_events(events)
    return events


def capture_command(arguments: argparse.Namespace) -> int:
    output = Path(arguments.output)
    manifest_path = output.with_suffix(".json")
    if not arguments.force and (output.exists() or manifest_path.exists()):
        raise ValueError("output already exists; use --force to replace it")

    events = list(arguments.event)
    if arguments.port:
        guided_events = events if arguments.guided else None
        interactive_events = events if arguments.interactive_events else None
        lines = serial_lines(arguments.port,
                             arguments.duration,
                             guided_events,
                             interactive_events)
        source_description = {
            "port": arguments.port,
            "baud": 115200,
            "event_clock": "host_monotonic",
        }
    elif arguments.input == "-":
        lines = sys.stdin
        source_description = {"input": "stdin", "event_clock": "device_uptime"}
    else:
        input_path = Path(arguments.input)
        lines = input_path.open("r", encoding="utf-8", errors="replace")
        source_description = {
            "input": str(input_path),
            "event_clock": "device_uptime",
        }

    try:
        header, rows = telemetry_rows(lines)
    finally:
        if hasattr(lines, "close") and lines is not sys.stdin:
            lines.close()

    if arguments.duration and not arguments.port:
        first = int(rows[0]["uptime_ms"])
        limit = round(arguments.duration * 1000)
        rows = [row for row in rows if int(row["uptime_ms"]) - first <= limit]
    if arguments.marker_events:
        events = marker_events_from_rows(rows)
    session_id = arguments.session_id or str(uuid.uuid4())
    labeled = label_rows(rows,
                         session_id,
                         arguments.split,
                         arguments.scenario,
                         events)
    output.parent.mkdir(parents=True, exist_ok=True)
    fields = list(METADATA_FIELDS) + header
    with output.open("w", newline="", encoding="utf-8") as destination:
        writer = csv.DictWriter(destination, fieldnames=fields)
        writer.writeheader()
        writer.writerows(labeled)

    successful_rssi = [
        row["rssi_dbm"] for row in labeled if row.get("sample_ok") == "1"
    ]
    digest = hashlib.sha256(
        ("\n".join(successful_rssi) + "\n").encode("ascii")
    ).hexdigest()
    manifest = {
        "experiment_schema": SCHEMA_VERSION,
        "session_id": session_id,
        "split": arguments.split,
        "scenario": arguments.scenario,
        "contains_personal_data": False,
        "source": source_description,
        "events": [
            {"id": event_id, "start_ms": start, "end_ms": end}
            for event_id, start, end in events
        ],
        "geometry": {
            "ap_sensor_distance_m": arguments.distance_m,
            "sensor_height_m": arguments.height_m,
            "orientation": arguments.orientation,
            "placement": arguments.placement,
        },
        "samples": len(labeled),
        "successful_samples": len(successful_rssi),
        "rssi_sha256": digest,
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


def read_experiment(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        required = set(METADATA_FIELDS) | {
            "uptime_ms",
            "sample_ok",
            "rssi_dbm",
            "state",
            "calibrated",
            "baseline",
            "rssi_changed",
        }
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise ValueError(f"{path}: invalid experiment schema")
        rows = list(reader)
    if not rows:
        raise ValueError(f"{path}: empty experiment")
    if any(row["experiment_schema"] != str(SCHEMA_VERSION) for row in rows):
        raise ValueError(f"{path}: unsupported experiment schema")
    return rows


def safe_ratio(numerator: int, denominator: int) -> float | None:
    return numerator / denominator if denominator else None


def is_active_state(state: str) -> bool:
    """Accept the firmware state name and legacy experiment fixtures."""
    return state in ("motion", "active")


def evaluate_rows(rows: list[dict[str, str]]) -> dict:
    rows = sorted(rows, key=lambda row: (row["session_id"], int(row["elapsed_ms"])))
    true_positive = false_positive = true_negative = false_negative = 0
    repeated = successful = 0
    idle_baselines: list[float] = []
    events: dict[tuple[str, str], list[dict[str, str]]] = {}
    sessions: dict[str, list[dict[str, str]]] = {}

    for row in rows:
        truth = row["truth_active"] == "1"
        predicted = is_active_state(row["state"])
        if truth and predicted:
            true_positive += 1
        elif truth:
            false_negative += 1
        elif predicted:
            false_positive += 1
        else:
            true_negative += 1
        if row["sample_ok"] == "1":
            successful += 1
            if row["rssi_changed"] == "0":
                repeated += 1
        if not truth and row["state"] == "idle" and row["calibrated"] == "1":
            idle_baselines.append(float(row["baseline"]))
        if row["event_id"]:
            events.setdefault((row["session_id"], row["event_id"]), []).append(row)
        sessions.setdefault(row["session_id"], []).append(row)

    detected_events = 0
    activation_latencies: list[int] = []
    release_latencies: list[int] = []
    for (session_id, _), event_rows in events.items():
        start = int(event_rows[0]["event_start_ms"])
        end = int(event_rows[0]["event_end_ms"])
        detections = [
            int(row["elapsed_ms"])
            for row in event_rows
            if is_active_state(row["state"])
        ]
        if not detections:
            continue
        detected_events += 1
        activation_latencies.append(max(0, min(detections) - start))
        later_rows = [
            row for row in sessions[session_id]
            if int(row["elapsed_ms"]) > end
        ]
        release = next(
            (int(row["elapsed_ms"]) for row in later_rows
             if not is_active_state(row["state"])),
            None,
        )
        if release is not None:
            release_latencies.append(max(0, release - end))

    false_activations = 0
    idle_duration_ms = 0
    for session_rows in sessions.values():
        session_rows.sort(key=lambda row: int(row["elapsed_ms"]))
        previous_state = session_rows[0]["state"]
        for previous, current in zip(session_rows, session_rows[1:]):
            delta = int(current["elapsed_ms"]) - int(previous["elapsed_ms"])
            if previous["truth_active"] == "0":
                idle_duration_ms += max(0, delta)
            if (current["truth_active"] == "0" and
                    is_active_state(current["state"]) and
                    not is_active_state(previous_state)):
                false_activations += 1
            previous_state = current["state"]

    precision = safe_ratio(true_positive, true_positive + false_positive)
    sample_recall = safe_ratio(true_positive, true_positive + false_negative)
    f1 = (2 * precision * sample_recall / (precision + sample_recall)
          if precision is not None and sample_recall is not None and
          precision + sample_recall > 0 else None)
    event_recall = safe_ratio(detected_events, len(events))
    false_positives_hour = (
        false_activations * 3_600_000 / idle_duration_ms
        if idle_duration_ms else None
    )
    median_activation = (
        statistics.median(activation_latencies) if activation_latencies else None
    )
    median_release = (
        statistics.median(release_latencies) if release_latencies else None
    )
    objectives_eligible = (
        event_recall is not None and false_positives_hour is not None and
        median_activation is not None and len(events) >= 10 and
        idle_duration_ms >= 3_600_000
    )
    objectives_met = (
        objectives_eligible and event_recall >= 0.90 and
        false_positives_hour <= 1.0 and median_activation <= 1000
    )
    return {
        "sessions": len(sessions),
        "events": len(events),
        "detected_events": detected_events,
        "event_recall": event_recall,
        "sample_precision": precision,
        "sample_recall": sample_recall,
        "sample_f1": f1,
        "false_activations": false_activations,
        "idle_duration_hours": idle_duration_ms / 3_600_000,
        "false_positives_per_hour": false_positives_hour,
        "activation_latency_median_ms": median_activation,
        "release_latency_median_ms": median_release,
        "repeated_samples_percent": safe_ratio(repeated * 100, successful),
        "baseline_idle_stddev": (
            statistics.pstdev(idle_baselines) if len(idle_baselines) > 1 else 0.0
        ),
        "baseline_idle_range": (
            max(idle_baselines) - min(idle_baselines) if idle_baselines else None
        ),
        "confusion_samples": {
            "true_positive": true_positive,
            "false_positive": false_positive,
            "true_negative": true_negative,
            "false_negative": false_negative,
        },
        "objectives": {
            "eligible": objectives_eligible,
            "met": bool(objectives_met),
            "event_recall_min": 0.90,
            "false_positives_per_hour_max": 1.0,
            "activation_latency_median_ms_max": 1000,
        },
    }


def write_report(report: dict, output: str | None) -> None:
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if output:
        output_path = Path(output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)


def evaluate_command(arguments: argparse.Namespace) -> int:
    rows: list[dict[str, str]] = []
    for value in arguments.input:
        rows.extend(read_experiment(Path(value)))
    available_splits = sorted({row["split"] for row in rows})
    if arguments.split:
        rows = [row for row in rows if row["split"] == arguments.split]
        if not rows:
            raise ValueError(f"no rows for split {arguments.split}")
    report = {
        "experiment_schema": SCHEMA_VERSION,
        "selected_split": arguments.split or "all",
        "available_splits": available_splits,
        "metrics": evaluate_rows(rows),
    }
    write_report(report, arguments.output)
    return 0


def replay_predictions(
    binary: Path,
    rows: list[dict[str, str]],
    profile: str,
    algorithm: str,
    baseline: str,
    sample_field: str = "rssi_dbm",
    detector_overrides: tuple[int, float, float] | None = None,
) -> list[dict[str, str]]:
    if sample_field not in REPLAY_SAMPLE_FIELDS:
        raise ValueError(f"unsupported replay sample field: {sample_field}")
    successful_rows = [
        row for row in rows
        if row["sample_ok"] == "1" and row.get(sample_field, "") != ""
    ]
    if not successful_rows:
        raise ValueError(f"no valid samples for replay field: {sample_field}")
    input_data = "".join(f'{row[sample_field]}\n' for row in successful_rows)
    command = [str(binary), profile, algorithm, baseline]
    if detector_overrides is not None:
        window, sigma, minimum_threshold = detector_overrides
        command.extend((str(window), str(sigma), str(minimum_threshold)))
    process = subprocess.run(
        command,
        input=input_data,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if process.returncode != 0:
        raise ValueError(f"detector replay failed: {process.stderr.strip()}")
    predictions = list(csv.DictReader(io.StringIO(process.stdout)))
    if len(predictions) != len(successful_rows):
        raise ValueError("detector replay returned an unexpected row count")

    result: list[dict[str, str]] = []
    prediction_index = 0
    last_prediction = {
        "state": "calibrating",
        "baseline": "0",
        "calibrated": "0",
    }
    for source in rows:
        row = dict(source)
        if (source["sample_ok"] == "1" and
                source.get(sample_field, "") != ""):
            last_prediction = predictions[prediction_index]
            prediction_index += 1
        row.update(last_prediction)
        row["algorithm"] = algorithm
        row["baseline_mode"] = baseline
        row["profile"] = profile
        result.append(row)
    return result


def compare_command(arguments: argparse.Namespace) -> int:
    rows: list[dict[str, str]] = []
    for value in arguments.input:
        rows.extend(read_experiment(Path(value)))
    if arguments.split:
        rows = [row for row in rows if row["split"] == arguments.split]
    if not rows:
        raise ValueError("no experiment rows selected")
    binary = Path(arguments.replay_binary)
    if not binary.is_file():
        raise ValueError(f"replay binary not found: {binary}; run tools/test-host.sh")

    comparisons = []
    detector_overrides = (
        arguments.window,
        arguments.sigma,
        arguments.minimum_threshold,
    ) if arguments.window is not None else None
    for profile in PROFILES:
        for algorithm in ALGORITHMS:
            for baseline in BASELINES:
                replayed = replay_predictions(binary,
                                                rows,
                                                profile,
                                                algorithm,
                                                baseline,
                                                arguments.sample_field,
                                                detector_overrides)
                comparisons.append({
                    "sample_field": arguments.sample_field,
                    "profile": profile,
                    "algorithm": algorithm,
                    "baseline_mode": baseline,
                    "metrics": evaluate_rows(replayed),
                })
    report = {
        "experiment_schema": SCHEMA_VERSION,
        "sample_field": arguments.sample_field,
        "detector_overrides": ({
            "window": arguments.window,
            "sigma": arguments.sigma,
            "minimum_threshold": arguments.minimum_threshold,
        } if detector_overrides is not None else None),
        "selected_split": arguments.split or "all",
        "combinations": comparisons,
    }
    write_report(report, arguments.output)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture = subparsers.add_parser("capture", help="capture and label a session")
    source = capture.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", help="raw telemetry log, or - for stdin")
    source.add_argument("--port", help="serial port, for example /dev/ttyACM0")
    capture.add_argument("--output", required=True)
    capture.add_argument("--duration", type=float,
                         help="capture/filter duration in seconds")
    capture.add_argument("--session-id")
    capture.add_argument("--split", choices=("tuning", "evaluation"), required=True)
    capture.add_argument("--scenario", required=True)
    capture.add_argument("--event", type=parse_event, action="append", default=[])
    capture.add_argument("--distance-m", type=float, required=True)
    capture.add_argument("--height-m", type=float, required=True)
    capture.add_argument("--orientation", required=True)
    capture.add_argument("--placement", required=True)
    capture.add_argument(
        "--guided",
        action="store_true",
        help="print timed preparation, crossing and completion prompts",
    )
    capture.add_argument(
        "--interactive-events",
        action="store_true",
        help="read event-start/event-end/capture-stop commands from stdin",
    )
    capture.add_argument(
        "--marker-events",
        action="store_true",
        help="derive event boundaries from the physical BOOT marker telemetry",
    )
    capture.add_argument("--force", action="store_true")
    capture.set_defaults(function=capture_command)

    evaluate = subparsers.add_parser("evaluate", help="evaluate labeled sessions")
    evaluate.add_argument("--input", action="append", required=True)
    evaluate.add_argument("--split", choices=("tuning", "evaluation"))
    evaluate.add_argument("--output")
    evaluate.set_defaults(function=evaluate_command)

    compare = subparsers.add_parser("compare", help="replay all detector variants")
    compare.add_argument("--input", action="append", required=True)
    compare.add_argument("--split", choices=("tuning", "evaluation"))
    compare.add_argument("--replay-binary", default="build-host/detector_replay")
    compare.add_argument("--sample-field", choices=REPLAY_SAMPLE_FIELDS,
                         default="rssi_dbm")
    compare.add_argument("--window", type=int)
    compare.add_argument("--sigma", type=float)
    compare.add_argument("--minimum-threshold", type=float)
    compare.add_argument("--output")
    compare.set_defaults(function=compare_command)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)
    if arguments.command == "capture" and arguments.port and not arguments.duration:
        parser.error("capture with --port requires --duration")
    if arguments.command == "compare":
        overrides = (
            arguments.window,
            arguments.sigma,
            arguments.minimum_threshold,
        )
        if any(value is not None for value in overrides) and not all(
            value is not None for value in overrides
        ):
            parser.error(
                "--window, --sigma and --minimum-threshold must be used together"
            )
    if arguments.command == "capture" and arguments.interactive_events and (
        not arguments.port or arguments.guided or arguments.event or
        arguments.marker_events
    ):
        parser.error(
            "--interactive-events requires --port and cannot be combined "
            "with --guided or --event"
        )
    if arguments.command == "capture" and arguments.marker_events and (
        not arguments.port or arguments.guided or arguments.event or
        arguments.interactive_events
    ):
        parser.error(
            "--marker-events requires --port and cannot be combined with "
            "--guided, --interactive-events or --event"
        )
    if arguments.command == "capture" and (
        arguments.distance_m <= 0 or arguments.height_m < 0
    ):
        parser.error("distance must be positive and height cannot be negative")
    try:
        return arguments.function(arguments)
    except (OSError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
