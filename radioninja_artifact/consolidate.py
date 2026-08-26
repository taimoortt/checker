from __future__ import annotations

import json
from pathlib import Path
from typing import Dict, List, Sequence

from .manifest import Scenario
from .run_stats import atomic_write_json, load_run_stats


class ConsolidationError(RuntimeError):
    pass


EXPECTED_FIGURES = {
    "scenario1": ["figure_7b_pf_cdf.pdf", "figure_7c_throughput_cdf.pdf"],
    "scenario2": [
        "figure_8b_pf_cdf.pdf",
        "figure_8c_tail_throughput_gain.pdf",
        "figure_8d_mean_throughput_change.pdf",
    ],
}


def consolidate(
    scenarios: Sequence[Scenario], run_root: Path, output_root: Path, seeds: Sequence[int]
) -> Dict[str, object]:
    run_checks: List[Dict[str, object]] = []
    scenario_reports: List[Dict[str, object]] = []
    simulator_hashes = set()
    successful_runs = 0

    for scenario in scenarios:
        expected = len(scenario.algorithms) * len(seeds)
        scenario_successes = 0
        for algorithm in scenario.algorithms:
            for seed in seeds:
                run_dir = run_root / scenario.id / algorithm.id / f"seed_{seed:02d}"
                metadata_path = run_dir / "metadata.json"
                stats_path = run_dir / "run_stats.json"
                passed = False
                errors: List[str] = []
                if not metadata_path.is_file() or not stats_path.is_file():
                    errors.append("missing metadata or compact statistics")
                else:
                    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
                    stats = load_run_stats(stats_path)
                    if metadata.get("status") != "success" or metadata.get("return_code") != 0:
                        errors.append("run status is not successful")
                    if int(metadata.get("duration", -1)) != scenario.duration:
                        errors.append("run is not full-duration")
                    errors.extend(str(value) for value in stats.get("integrity_errors", []))
                    simulator_hashes.add(str(stats.get("simulator_hash")))
                    passed = not errors
                if passed:
                    successful_runs += 1
                    scenario_successes += 1
                run_checks.append({
                    "scenario": scenario.id,
                    "algorithm": algorithm.id,
                    "seed": seed,
                    "passed": passed,
                    "errors": errors,
                })

        scenario_dir = output_root / scenario.id
        validation_path = scenario_dir / "validation_report.json"
        audit_path = scenario_dir / "audit_report.json"
        if not validation_path.is_file() or not audit_path.is_file():
            raise ConsolidationError(f"missing operator/auditor report for {scenario.id}")
        validation = json.loads(validation_path.read_text(encoding="utf-8"))
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        figures = {
            name: (scenario_dir / name).is_file() and (scenario_dir / name).stat().st_size > 0
            for name in EXPECTED_FIGURES[scenario.id]
        }
        scenario_passed = (
            scenario_successes == expected
            and bool(validation.get("passed"))
            and bool(audit.get("passed"))
            and all(figures.values())
        )
        scenario_reports.append({
            "scenario": scenario.id,
            "successful_runs": scenario_successes,
            "expected_runs": expected,
            "operator_validation_passed": bool(validation.get("passed")),
            "independent_audit_passed": bool(audit.get("passed")),
            "operator_auditor_agreement": bool(audit.get("operator_agreement")),
            "figures": figures,
            "passed": scenario_passed,
        })

    expected_total = sum(len(scenario.algorithms) * len(seeds) for scenario in scenarios)
    requested_scope = {scenario.id for scenario in scenarios}
    supported_scope = requested_scope == {"scenario1", "scenario2"}
    required_seed_set = list(seeds) == list(range(50))
    required_total = 450
    passed = (
            supported_scope
            and required_seed_set
            and expected_total == required_total
            and successful_runs == required_total
            and len(simulator_hashes) == 1
            and all(item["passed"] for item in scenario_reports)
        )
    report = {
        "passed": passed,
        "publication_gate_passed": passed,
        "publication_performed": False,
        "publication_note": "This pipeline never commits, uploads, or creates a public repository.",
        "required_seed_set": list(range(50)),
        "observed_seed_set": list(seeds),
        "successful_full_duration_runs": successful_runs,
        "expected_full_duration_runs": required_total if supported_scope else expected_total,
        "single_frozen_simulator": len(simulator_hashes) == 1,
        "simulator_hashes": sorted(simulator_hashes),
        "scenarios": scenario_reports,
        "runs": run_checks,
    }
    output_root.mkdir(parents=True, exist_ok=True)
    atomic_write_json(output_root / "consolidated_validation_report.json", report)
    return report
