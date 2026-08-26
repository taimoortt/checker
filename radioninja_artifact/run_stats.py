from __future__ import annotations

import gzip
import json
import os
import re
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Set, TextIO


FLOW_RE = re.compile(
    r"\bflow\s+(?P<ue>\d+)\s+cell:\s+(?P<cell>\d+)\s+slice:\s+(?P<slice>\d+)\s+"
    r"nb_of_rbs:\s+(?P<rbs>\d+)\s+eff_sinr:\s+(?P<sinr>\S+)\s+tbs_size:\s+(?P<tbs>\d+)"
)
TTI_RE = re.compile(r"\bTTI:\s*(\d+)")
INTERNET_USER_RE = re.compile(
    r"CREATED InternetFlow APPLICATION, ID\s+(?P<ue>\d+)"
)
CREATION_PRIORITY_RE = re.compile(r"\bPriority:\s*(?P<priority>\S+)")
RUNTIME_PRIORITY_RE = re.compile(
    r"\bpriority:\s*(?P<priority>\S+)\s+flow_id:\s*(?P<ue>\d+)"
)
INITIAL_SLICE_RE = re.compile(
    r"\bSetting User:\s*(?P<ue>\d+)\s+to Slice:\s*(?P<slice>\d+)"
)
REASSIGNMENT_SLICE_RE = re.compile(
    r"\bReassigning UE:\s*(?P<ue>\d+)\s+to Slice:\s*(?P<slice>\d+)"
)
FULL_DURATION_STOP_MARKER = "SIMULATOR_DEBUG: Stop ()"
# Preserve the paper notebook convention: a run ending at TTI N is divided by
# N - 99 (TTIs 0 through 99 form the warm-up window).
WARMUP_TTIS = 99
STATS_SCHEMA_VERSION = 1


class RunStatsError(RuntimeError):
    pass


def open_log_text(path: Path) -> TextIO:
    if path.suffix == ".gz":
        return gzip.open(path, mode="rt", encoding="utf-8", errors="replace")
    return path.open(encoding="utf-8", errors="replace")


def atomic_write_json(path: Path, data: Dict[str, object]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def _priority_value(value: str) -> float | str:
    try:
        return float(value)
    except ValueError:
        return value


def collect_run_stats(
    path: Path,
    *,
    scenario: str,
    algorithm: str,
    seed: int,
    duration: int,
    config_hash: str,
    simulator_hash: str,
) -> Dict[str, object]:
    if not path.is_file():
        raise RunStatsError(f"Missing simulator log: {path}")

    max_tti = -1
    record_count = 0
    full_duration_stop = False
    ue_tbs: Dict[int, int] = defaultdict(int)
    initial_ue_slice: Dict[int, int] = {}
    reassigned_ue_slice: Dict[int, int] = {}
    flow_ue_slice: Dict[int, int] = {}
    slice_tbs: Dict[int, int] = defaultdict(int)
    internet_users: Set[int] = set()
    configured_internet_priorities: Dict[int, Set[float | str]] = defaultdict(set)
    runtime_internet_priorities: Dict[int, Set[float | str]] = defaultdict(set)

    with open_log_text(path) as handle:
        for line in handle:
            full_duration_stop = full_duration_stop or FULL_DURATION_STOP_MARKER in line
            tti_match = TTI_RE.search(line)
            if tti_match:
                max_tti = max(max_tti, int(tti_match.group(1)))

            internet_match = INTERNET_USER_RE.search(line)
            if internet_match:
                ue = int(internet_match.group("ue"))
                internet_users.add(ue)
                creation_priority = CREATION_PRIORITY_RE.search(line)
                if creation_priority is not None:
                    configured_internet_priorities[ue].add(
                        _priority_value(creation_priority.group("priority"))
                    )

            priority_match = RUNTIME_PRIORITY_RE.search(line)
            if priority_match:
                runtime_internet_priorities[int(priority_match.group("ue"))].add(
                    _priority_value(priority_match.group("priority"))
                )

            initial_slice_match = INITIAL_SLICE_RE.search(line)
            if initial_slice_match:
                ue = int(initial_slice_match.group("ue"))
                slice_id = int(initial_slice_match.group("slice"))
                previous = initial_ue_slice.setdefault(ue, slice_id)
                if previous != slice_id:
                    raise RunStatsError(
                        f"{path}: conflicting initial slice assignments for UE {ue}: "
                        f"{previous} and {slice_id}"
                    )

            reassignment_match = REASSIGNMENT_SLICE_RE.search(line)
            if reassignment_match:
                ue = int(reassignment_match.group("ue"))
                slice_id = int(reassignment_match.group("slice"))
                previous = reassigned_ue_slice.setdefault(ue, slice_id)
                if previous != slice_id:
                    raise RunStatsError(
                        f"{path}: conflicting final slice assignments for UE {ue}: "
                        f"{previous} and {slice_id}"
                    )

            flow_match = FLOW_RE.search(line)
            if not flow_match:
                continue
            record_count += 1
            ue = int(flow_match.group("ue"))
            slice_id = int(flow_match.group("slice"))
            tbs = int(flow_match.group("tbs"))
            previous = flow_ue_slice.setdefault(ue, slice_id)
            if previous != slice_id:
                raise RunStatsError(f"{path}: UE {ue} changes slice from {previous} to {slice_id}")
            ue_tbs[ue] += tbs
            slice_tbs[slice_id] += tbs

    # Simulator initialization enumerates every UE, including UEs that receive
    # no transport block.  A later per-cell reassignment is the authoritative
    # final mapping when present; flow records only cover scheduled UEs.
    ue_slice = dict(initial_ue_slice)
    ue_slice.update(reassigned_ue_slice)
    for ue, slice_id in flow_ue_slice.items():
        assigned_slice = ue_slice.setdefault(ue, slice_id)
        if assigned_slice != slice_id:
            raise RunStatsError(
                f"{path}: UE {ue} is assigned to slice {assigned_slice} "
                f"but has flow records for slice {slice_id}"
            )
    for ue in ue_slice:
        ue_tbs.setdefault(ue, 0)
    for slice_id in set(ue_slice.values()):
        slice_tbs.setdefault(slice_id, 0)

    measured_ttis = duration * 1000 - WARMUP_TTIS
    return {
        "schema_version": STATS_SCHEMA_VERSION,
        "scenario": scenario,
        "algorithm": algorithm,
        "seed": seed,
        "duration": duration,
        "warmup_ttis": WARMUP_TTIS,
        "measured_ttis": measured_ttis,
        "config_hash": config_hash,
        "simulator_hash": simulator_hash,
        "completion_marker_found": full_duration_stop,
        "max_observed_tti": max_tti,
        "record_count": record_count,
        "per_ue_tbs_kbits": {str(key): value for key, value in sorted(ue_tbs.items())},
        "ue_slice": {str(key): value for key, value in sorted(ue_slice.items())},
        "per_slice_tbs_kbits": {str(key): value for key, value in sorted(slice_tbs.items())},
        "internet_user_ids": sorted(internet_users),
        "internet_user_configured_priorities": {
            str(key): sorted(values, key=str)
            for key, values in sorted(configured_internet_priorities.items())
        },
        "internet_user_priorities": {
            str(key): sorted(values, key=str)
            for key, values in sorted(runtime_internet_priorities.items())
        },
    }


def validate_run_stats(
    stats: Dict[str, object],
    *,
    expected_users: int | None,
    expected_slices: int,
    expected_internet_users: int | None = None,
    expected_internet_priority: float | None = None,
) -> List[str]:
    errors: List[str] = []
    ue_tbs = stats.get("per_ue_tbs_kbits", {})
    ue_slice = stats.get("ue_slice", {})
    slice_tbs = stats.get("per_slice_tbs_kbits", {})
    if not stats.get("completion_marker_found"):
        errors.append(f"missing full-duration marker {FULL_DURATION_STOP_MARKER!r}")
    if int(stats.get("measured_ttis", 0)) <= 0:
        errors.append("non-positive measurement duration")
    if int(stats.get("record_count", 0)) <= 0:
        errors.append("no flow records")
    if not isinstance(ue_tbs, dict) or not ue_tbs:
        errors.append("missing per-UE throughput totals")
    elif expected_users is not None and len(ue_tbs) != expected_users:
        errors.append(f"expected throughput totals for {expected_users} UEs, found {len(ue_tbs)}")
    if not isinstance(ue_slice, dict) or set(ue_slice) != set(ue_tbs):
        errors.append("UE slice assignments do not match UE throughput totals")
    if not isinstance(slice_tbs, dict) or len(slice_tbs) != expected_slices:
        errors.append(f"expected totals for {expected_slices} slices, found {len(slice_tbs)}")
    if isinstance(ue_tbs, dict) and isinstance(slice_tbs, dict):
        if sum(int(value) for value in ue_tbs.values()) != sum(int(value) for value in slice_tbs.values()):
            errors.append("per-UE and per-slice TBS totals differ")
    if expected_internet_users is not None:
        ids = stats.get("internet_user_ids", [])
        if not isinstance(ids, list) or len(ids) != expected_internet_users:
            errors.append(f"expected {expected_internet_users} Internet users, found {len(ids)}")
        if expected_internet_priority is not None and isinstance(ids, list):
            expected_ids = {str(value) for value in ids}
            configured = stats.get("internet_user_configured_priorities", {})
            if not isinstance(configured, dict) or expected_ids != set(configured):
                errors.append("not every Internet user has a configured priority record")
            elif any(
                not values or any(float(value) != expected_internet_priority for value in values)
                for values in configured.values()
            ):
                errors.append(f"Internet user configured priorities are not all {expected_internet_priority:g}")
            runtime = stats.get("internet_user_priorities", {})
            if not isinstance(runtime, dict) or not set(runtime).issubset(expected_ids):
                errors.append("runtime priority records contain unknown Internet users")
            elif any(
                not values or any(float(value) != expected_internet_priority for value in values)
                for values in runtime.values()
            ):
                errors.append(f"observed Internet user runtime priorities are not all {expected_internet_priority:g}")
    return errors


def load_run_stats(path: Path) -> Dict[str, object]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RunStatsError(f"Cannot read run statistics {path}: {exc}") from exc
    if data.get("schema_version") != STATS_SCHEMA_VERSION:
        raise RunStatsError(f"Unsupported run statistics schema in {path}")
    return data
