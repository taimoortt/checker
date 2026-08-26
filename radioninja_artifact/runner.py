from __future__ import annotations

import gzip
import hashlib
import json
import os
import shutil
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

from .manifest import MANIFEST_DIR, ROOT, Algorithm, Scenario, scenario_ids
from .run_stats import (
    FULL_DURATION_STOP_MARKER,
    RunStatsError,
    atomic_write_json,
    collect_run_stats,
    load_run_stats,
    validate_run_stats,
)


MAX_JOBS = 5
FIVE_GIB = 5 * 1024**3
THIRTY_GIB = 30 * 1024**3
MIN_FREE_DISK = 3 * 1024**3
FREEZE_SCHEMA_VERSION = 1


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def simulator_hash(path: Path | None = None) -> str:
    simulator = path or ROOT / "LTE-Sim"
    if not simulator.is_file():
        raise RuntimeError("LTE-Sim is missing; run the build command first")
    return _sha256(simulator)


def _hash_files(paths: Iterable[Path]) -> Tuple[str, Dict[str, str]]:
    digest = hashlib.sha256()
    hashes: Dict[str, str] = {}
    for path in sorted(set(paths)):
        relative = str(path.relative_to(ROOT))
        file_hash = _sha256(path)
        hashes[relative] = file_hash
        digest.update(relative.encode("utf-8") + b"\0" + file_hash.encode("ascii") + b"\n")
    return digest.hexdigest(), hashes


def _source_files() -> List[Path]:
    files = [path for path in (ROOT / "src").rglob("*") if path.is_file()]
    files.extend(path for path in (ROOT / "CONFIG").rglob("*") if path.is_file())
    files.extend(path for path in (ROOT / "Debug").glob("*.mk") if path.is_file())
    files.extend(path for path in (ROOT / "Debug").glob("makefile") if path.is_file())
    files.append(ROOT / "Makefile")
    return files


def _simulation_inputs() -> Tuple[str, Dict[str, str], Dict[str, object]]:
    config_paths: List[Path] = []
    normalized_scenarios: Dict[str, object] = {}
    for scenario_id in scenario_ids():
        path = MANIFEST_DIR / f"{scenario_id}.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        normalized_scenarios[scenario_id] = {
            "id": data["id"],
            "duration": data["duration"],
            "topology": data["topology"],
            "algorithms": [
                {"id": item["id"], "config": item["config"]} for item in data["algorithms"]
            ],
        }
        config_paths.extend(ROOT / item["config"] for item in data["algorithms"])
    config_digest, config_hashes = _hash_files(config_paths)
    canonical_scenarios = json.dumps(normalized_scenarios, sort_keys=True, separators=(",", ":"))
    digest = hashlib.sha256()
    digest.update(canonical_scenarios.encode("utf-8"))
    digest.update(config_digest.encode("ascii"))
    return digest.hexdigest(), config_hashes, normalized_scenarios


@dataclass(frozen=True)
class CampaignFreeze:
    directory: Path
    simulator: Path
    simulator_hash: str
    manifest_hash: str

    def config_path(self, live_path: Path) -> Path:
        return self.directory / "configs" / live_path.relative_to(ROOT)


def freeze_campaign(output_root: Path) -> CampaignFreeze:
    """Create or verify immutable simulator/config inputs for a campaign."""
    freeze_dir = output_root / "_campaign" / "frozen"
    manifest_path = output_root / "_campaign" / "freeze_manifest.json"
    freeze_dir.mkdir(parents=True, exist_ok=True)
    live_simulator = ROOT / "LTE-Sim"
    source_digest, source_hashes = _hash_files(_source_files())
    input_digest, config_hashes, normalized_scenarios = _simulation_inputs()

    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        mismatches = []
        if manifest.get("schema_version") != FREEZE_SCHEMA_VERSION:
            mismatches.append("freeze schema")
        if manifest.get("simulator_source_hash") != source_digest:
            mismatches.append("simulator source")
        if manifest.get("simulation_inputs_hash") != input_digest:
            mismatches.append("scenario/config inputs")
        frozen_simulator = freeze_dir / "LTE-Sim"
        if not frozen_simulator.is_file() or manifest.get("simulator_hash") != simulator_hash(frozen_simulator):
            mismatches.append("frozen simulator binary")
        for relative, expected_hash in dict(manifest.get("config_files", {})).items():
            frozen_config = freeze_dir / "configs" / relative
            if not frozen_config.is_file() or _sha256(frozen_config) != expected_hash:
                mismatches.append(f"frozen config {relative}")
        if mismatches:
            raise RuntimeError(
                "Campaign freeze no longer matches "
                + ", ".join(mismatches)
                + f"; use a new run directory and restart all campaigns ({manifest_path})"
            )
        return CampaignFreeze(
            freeze_dir,
            frozen_simulator,
            str(manifest["simulator_hash"]),
            _sha256(manifest_path),
        )

    live_simulator_hash = simulator_hash(live_simulator)
    frozen_simulator = freeze_dir / "LTE-Sim"
    simulator_tmp = frozen_simulator.with_name("LTE-Sim.tmp")
    shutil.copy2(live_simulator, simulator_tmp)
    os.replace(simulator_tmp, frozen_simulator)
    frozen_simulator.chmod(frozen_simulator.stat().st_mode | 0o111)
    for relative in config_hashes:
        source = ROOT / relative
        destination = freeze_dir / "configs" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + ".tmp")
        shutil.copy2(source, temporary)
        os.replace(temporary, destination)

    manifest: Dict[str, object] = {
        "schema_version": FREEZE_SCHEMA_VERSION,
        "created_at": time.time(),
        "simulator_hash": live_simulator_hash,
        "simulator_source_hash": source_digest,
        "source_files": source_hashes,
        "simulation_inputs_hash": input_digest,
        "config_files": config_hashes,
        "scenarios": normalized_scenarios,
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_json(manifest_path, manifest)
    return CampaignFreeze(freeze_dir, frozen_simulator, live_simulator_hash, _sha256(manifest_path))


@dataclass(frozen=True)
class RunSpec:
    scenario: Scenario
    algorithm: Algorithm
    seed: int
    output_root: Path
    duration: Optional[int] = None
    campaign: Optional[CampaignFreeze] = None

    @property
    def run_dir(self) -> Path:
        return self.output_root / self.scenario.id / self.algorithm.id / f"seed_{self.seed:02d}"

    @property
    def effective_duration(self) -> int:
        return self.duration if self.duration is not None else self.scenario.duration

    @property
    def simulator(self) -> Path:
        return self.campaign.simulator if self.campaign else ROOT / "LTE-Sim"

    @property
    def config(self) -> Path:
        return self.campaign.config_path(self.algorithm.config) if self.campaign else self.algorithm.config


def build_simulator() -> None:
    subprocess.run(["make"], cwd=ROOT, check=True)


def simulator_command(spec: RunSpec) -> List[str]:
    topology = spec.scenario.topology
    return [
        str(spec.simulator), "MultiCell", str(spec.scenario.total_cells), "1",
        str(topology["users"]), "0", "0", "1", "0", "8", "1", "0", "0.1", "128",
        str(spec.config), str(topology["macro_cells"]), str(topology["micro_cells_per_macro"]),
        str(topology["inter_micro_distance"]), str(spec.effective_duration),
        str(topology["bandwidth_mhz"]), str(spec.seed),
    ]


def _spec_config_hash(spec: RunSpec) -> str:
    simulation_manifest = {
        "id": spec.scenario.id,
        "duration": spec.scenario.duration,
        "topology": spec.scenario.topology,
        "algorithms": [
            {"id": item.id, "config": str(item.config.relative_to(ROOT))}
            for item in spec.scenario.algorithms
        ],
    }
    digest = hashlib.sha256()
    digest.update(json.dumps(simulation_manifest, sort_keys=True, separators=(",", ":")).encode("utf-8"))
    digest.update(spec.config.read_bytes())
    return digest.hexdigest()


def _metadata_path(spec: RunSpec) -> Path:
    return spec.run_dir / "metadata.json"


def _stats_path(spec: RunSpec) -> Path:
    return spec.run_dir / "run_stats.json"


def _stdout_path(spec: RunSpec) -> Path:
    return spec.run_dir / "stdout.log.gz"


def _stderr_path(spec: RunSpec) -> Path:
    return spec.run_dir / "stderr.log.gz"


def _stats_errors(spec: RunSpec, stats: Dict[str, object]) -> List[str]:
    full_duration = spec.effective_duration == spec.scenario.duration
    errors = validate_run_stats(
        stats,
        expected_users=int(spec.scenario.topology["users"]) if full_duration else None,
        expected_slices=int(spec.scenario.topology["slices"]),
    )
    expected = {
        "scenario": spec.scenario.id,
        "algorithm": spec.algorithm.id,
        "seed": spec.seed,
        "duration": spec.effective_duration,
        "config_hash": _spec_config_hash(spec),
        "simulator_hash": simulator_hash(spec.simulator),
    }
    for key, value in expected.items():
        if stats.get(key) != value:
            errors.append(f"{key} mismatch: expected {value!r}, found {stats.get(key)!r}")
    return errors


def is_complete(spec: RunSpec) -> bool:
    metadata_path = _metadata_path(spec)
    stats_path = _stats_path(spec)
    if not metadata_path.is_file() or not stats_path.is_file():
        return False
    try:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        stats = load_run_stats(stats_path)
    except (OSError, json.JSONDecodeError, RunStatsError):
        return False
    return metadata.get("status") == "success" and metadata.get("return_code") == 0 and not _stats_errors(spec, stats)


def _remove_success_diagnostics(spec: RunSpec) -> None:
    # Cleanup is deliberately limited to a single run directory and cannot
    # reach the protected reference corpus under ROOT/results.
    if spec.seed == 0:
        return
    for path in (_stdout_path(spec), _stderr_path(spec)):
        try:
            path.unlink()
        except FileNotFoundError:
            pass


def _remove_legacy_diagnostics(spec: RunSpec) -> None:
    for name in ("stdout.log", "stderr.log"):
        try:
            (spec.run_dir / name).unlink()
        except FileNotFoundError:
            pass


def _copy_to_gzip(source, destination: gzip.GzipFile) -> None:
    """Drain a child-process pipe while compressing it incrementally."""
    try:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            destination.write(chunk)
    finally:
        source.close()


def run_one(spec: RunSpec, force: bool = False) -> Dict[str, object]:
    if not force and is_complete(spec):
        return {"scenario": spec.scenario.id, "algorithm": spec.algorithm.id, "seed": spec.seed, "status": "skipped"}

    spec.run_dir.mkdir(parents=True, exist_ok=True)
    # Never let a failed replacement run appear to have fresh compact data.
    try:
        _stats_path(spec).unlink()
    except FileNotFoundError:
        pass
    command = simulator_command(spec)
    started = time.time()
    live_config_hash = _spec_config_hash(spec)
    frozen_simulator_hash = simulator_hash(spec.simulator)
    metadata: Dict[str, object] = {
        "scenario": spec.scenario.id,
        "paper_figure": spec.scenario.paper_figure,
        "algorithm": spec.algorithm.id,
        "algorithm_label": spec.algorithm.label,
        "seed": spec.seed,
        "duration": spec.effective_duration,
        "config": str(spec.algorithm.config.relative_to(ROOT)),
        "frozen_config": str(spec.config),
        "config_hash": live_config_hash,
        "simulator_hash": frozen_simulator_hash,
        "campaign_manifest_hash": spec.campaign.manifest_hash if spec.campaign else None,
        "command": command,
        "started_at": started,
        "status": "running",
    }
    atomic_write_json(_metadata_path(spec), metadata)

    stdout_tmp = _stdout_path(spec).with_name("stdout.log.gz.tmp")
    stderr_tmp = _stderr_path(spec).with_name("stderr.log.gz.tmp")
    completed: subprocess.CompletedProcess[bytes] | None = None
    execution_error: str | None = None
    try:
        with gzip.open(stdout_tmp, "wb") as stdout, gzip.open(stderr_tmp, "wb") as stderr:
            process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if process.stdout is None or process.stderr is None:
                raise OSError("Cannot open simulator output pipes")
            with ThreadPoolExecutor(max_workers=2) as compressors:
                stdout_future = compressors.submit(_copy_to_gzip, process.stdout, stdout)
                stderr_future = compressors.submit(_copy_to_gzip, process.stderr, stderr)
                return_code = process.wait()
                stdout_future.result()
                stderr_future.result()
            completed = subprocess.CompletedProcess(command, return_code)
    except OSError as exc:
        execution_error = str(exc)
    finally:
        if stdout_tmp.exists():
            os.replace(stdout_tmp, _stdout_path(spec))
        if stderr_tmp.exists():
            os.replace(stderr_tmp, _stderr_path(spec))

    stats: Dict[str, object] | None = None
    integrity_errors: List[str] = []
    if _stdout_path(spec).is_file():
        try:
            stats = collect_run_stats(
                _stdout_path(spec), scenario=spec.scenario.id, algorithm=spec.algorithm.id,
                seed=spec.seed, duration=spec.effective_duration, config_hash=live_config_hash,
                simulator_hash=frozen_simulator_hash,
            )
            integrity_errors = _stats_errors(spec, stats)
            stats["integrity_errors"] = integrity_errors
            atomic_write_json(_stats_path(spec), stats)
        except (RunStatsError, OSError, EOFError, gzip.BadGzipFile) as exc:
            integrity_errors = [str(exc)]
    else:
        integrity_errors = ["missing compressed stdout diagnostic"]

    return_code = completed.returncode if completed is not None else None
    success = return_code == 0 and not integrity_errors
    completed_at = time.time()
    retained = not success or spec.seed == 0
    metadata.update({
        "completed_at": completed_at,
        "elapsed_seconds": completed_at - started,
        "return_code": return_code,
        "completion_marker": FULL_DURATION_STOP_MARKER,
        "completion_marker_found": bool(stats and stats.get("completion_marker_found")),
        "record_count": stats.get("record_count") if stats else None,
        "integrity_errors": integrity_errors,
        "execution_error": execution_error,
        "diagnostics_retained": retained,
        "status": "success" if success else "failure",
    })
    if success and not retained:
        _remove_success_diagnostics(spec)
    if success:
        _remove_legacy_diagnostics(spec)
    atomic_write_json(_metadata_path(spec), metadata)
    return metadata


def _host_available_memory() -> int:
    with Path("/proc/meminfo").open(encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
    raise RuntimeError("Cannot determine available memory from /proc/meminfo")


def _available_memory() -> int:
    available = _host_available_memory()
    cgroup = Path("/sys/fs/cgroup")
    try:
        maximum_text = (cgroup / "memory.max").read_text(encoding="utf-8").strip()
        if maximum_text != "max":
            current = int((cgroup / "memory.current").read_text(encoding="utf-8").strip())
            available = min(available, max(0, int(maximum_text) - current))
    except (OSError, ValueError):
        pass
    return available


def safe_job_count(requested: int) -> int:
    if not 1 <= requested <= MAX_JOBS:
        raise RuntimeError(f"jobs must be between 1 and {MAX_JOBS}")
    available = _available_memory()
    memory_limited = max(0, (available - FIVE_GIB) // FIVE_GIB)
    effective = min(requested, MAX_JOBS, int(memory_limited))
    if requested == 5 and available < THIRTY_GIB:
        effective = min(effective, 4)
    if effective < 1:
        raise RuntimeError(
            f"Insufficient available memory: {available / 1024**3:.2f} GiB; "
            "at least 10 GiB is required to run one job while reserving 5 GiB"
        )
    return effective


def _check_disk(output_root: Path) -> None:
    output_root.mkdir(parents=True, exist_ok=True)
    free = shutil.disk_usage(output_root).free
    if free < MIN_FREE_DISK:
        raise RuntimeError(f"Free disk is {free / 1024**3:.2f} GiB, below the 3 GiB batch-launch threshold")


def run_many(specs: Iterable[RunSpec], jobs: int = 5, force: bool = False) -> List[Dict[str, object]]:
    spec_list = list(specs)
    if not spec_list:
        return []
    effective_jobs = safe_job_count(jobs)
    if effective_jobs != jobs:
        print(f"[resources] reducing jobs from {jobs} to {effective_jobs} to preserve 5 GiB system memory", flush=True)
    results: List[Dict[str, object]] = []
    failures: List[Dict[str, object]] = []
    for offset in range(0, len(spec_list), effective_jobs):
        _check_disk(spec_list[offset].output_root)
        campaign = spec_list[offset].campaign
        if campaign is not None:
            verified = freeze_campaign(spec_list[offset].output_root)
            if verified.manifest_hash != campaign.manifest_hash:
                raise RuntimeError("Campaign freeze manifest changed during execution; restart all campaigns")
        batch = spec_list[offset : offset + effective_jobs]
        with ThreadPoolExecutor(max_workers=effective_jobs) as executor:
            futures = {executor.submit(run_one, spec, force): spec for spec in batch}
            for future in as_completed(futures):
                result = future.result()
                results.append(result)
                print(f"[{result['status']}] {result['scenario']} {result['algorithm']} seed={result['seed']}", flush=True)
                if result["status"] == "failure":
                    failures.append(result)
        if failures:
            break
    if failures:
        failed = ", ".join(f"{item['scenario']}/{item['algorithm']}/{item['seed']}" for item in failures)
        raise RuntimeError(f"Simulation failures: {failed}")
    return results


def make_specs(
    scenarios: Iterable[Scenario], seeds: Iterable[int], output_root: Path,
    duration: Optional[int] = None, campaign: Optional[CampaignFreeze] = None,
) -> List[RunSpec]:
    return [
        RunSpec(scenario, algorithm, seed, output_root, duration, campaign)
        for scenario in scenarios for algorithm in scenario.algorithms for seed in seeds
    ]
