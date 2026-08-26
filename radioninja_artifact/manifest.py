from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_DIR = ROOT / "artifact" / "scenarios"


class ManifestError(ValueError):
    pass


@dataclass(frozen=True)
class Algorithm:
    id: str
    label: str
    config: Path


@dataclass(frozen=True)
class Scenario:
    id: str
    paper_figure: int
    description: str
    duration: int
    topology: Dict[str, int]
    algorithms: List[Algorithm]
    analysis: Dict[str, Any]
    manifest_path: Path

    @property
    def total_cells(self) -> int:
        macro = self.topology["macro_cells"]
        return macro * self.topology["micro_cells_per_macro"] + macro


def scenario_ids() -> List[str]:
    return ["scenario1", "scenario2"]


def _load_json(path: Path) -> Dict[str, Any]:
    try:
        with path.open(encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"Cannot read {path}: {exc}") from exc


def load_scenario(name: str) -> Scenario:
    normalized = name if name.startswith("scenario") else f"scenario{name}"
    path = MANIFEST_DIR / f"{normalized}.json"
    data = _load_json(path)
    algorithms = [
        Algorithm(item["id"], item["label"], ROOT / item["config"])
        for item in data["algorithms"]
    ]
    scenario = Scenario(
        id=data["id"],
        paper_figure=int(data["paper_figure"]),
        description=data["description"],
        duration=int(data["duration"]),
        topology=data["topology"],
        algorithms=algorithms,
        analysis=data["analysis"],
        manifest_path=path,
    )
    validate_scenario(scenario)
    return scenario


def load_selected(name: str) -> List[Scenario]:
    if name == "all":
        return [load_scenario(item) for item in scenario_ids()]
    if "," in name:
        selected = [load_scenario(item.strip()) for item in name.split(",") if item.strip()]
        if not selected:
            raise ManifestError("At least one scenario must be selected")
        return selected
    return [load_scenario(name)]


def config_hash(scenario: Scenario, algorithm: Algorithm) -> str:
    digest = hashlib.sha256()
    digest.update(scenario.manifest_path.read_bytes())
    digest.update(algorithm.config.read_bytes())
    return digest.hexdigest()


def validate_scenario(scenario: Scenario) -> None:
    topology = scenario.topology
    required = {
        "macro_cells",
        "micro_cells_per_macro",
        "inter_micro_distance",
        "users",
        "slices",
        "bandwidth_mhz",
    }
    missing = required.difference(topology)
    if missing:
        raise ManifestError(f"{scenario.id}: missing topology fields {sorted(missing)}")
    if topology["users"] != 160 or topology["slices"] != 8:
        raise ManifestError(f"{scenario.id}: artifact expects 160 users and 8 slices")
    if not scenario.algorithms or scenario.algorithms[-1].id != "no_muting":
        raise ManifestError(f"{scenario.id}: no_muting must be the final baseline")

    seen = set()
    for algorithm in scenario.algorithms:
        if algorithm.id in seen:
            raise ManifestError(f"{scenario.id}: duplicate algorithm {algorithm.id}")
        seen.add(algorithm.id)
        if not algorithm.config.is_file():
            raise ManifestError(f"{scenario.id}: missing config {algorithm.config}")
        config = _load_json(algorithm.config)
        if sum(config.get("ues_per_slice", [])) != topology["users"]:
            raise ManifestError(f"{algorithm.config}: ues_per_slice does not total 160")
        expanded_slices = sum(int(group["n_slices"]) for group in config["slices"])
        if expanded_slices != topology["slices"]:
            raise ManifestError(f"{algorithm.config}: expected 8 expanded slices")
        for group in config["slices"]:
            flow_count = (
                int(group["video_app"])
                + int(group["internet_flow"])
                + int(group["backlog_flow"])
            )
            if flow_count != int(group["num_ues"]):
                raise ManifestError(f"{algorithm.config}: flows do not equal num_ues")

    if scenario.id == "scenario2":
        expected_fairness_psi = int(scenario.analysis["fairness_psi"])
        for algorithm in scenario.algorithms:
            config = _load_json(algorithm.config)
            psi_values = [int(group["algo_psi"]) for group in config["slices"]]
            expected_psi_values = [1, expected_fairness_psi]
            if psi_values != expected_psi_values:
                raise ManifestError(
                    f"{algorithm.config}: expected psi groups {expected_psi_values}"
                )
