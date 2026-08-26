from __future__ import annotations

import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

import numpy as np

from .manifest import ROOT, Scenario
from .run_stats import atomic_write_json, load_run_stats


AUDIT_BOOTSTRAP_SEED = 2026
AUDIT_BOOTSTRAP_ITERATIONS = 10_000


class AuditError(RuntimeError):
    pass


def _pct(candidate: float, baseline: float) -> float:
    if baseline == 0:
        raise AuditError("zero audit baseline")
    return (candidate - baseline) / abs(baseline) * 100.0


def _pf(values: Iterable[float]) -> float:
    positive = [value for value in values if value > 0]
    if not positive:
        raise AuditError("empty PF population")
    return float(sum(math.log10(value) for value in positive))


def _ci(values: Sequence[float]) -> List[float]:
    data = np.asarray(values, dtype=float)
    if not len(data):
        raise AuditError("empty audit bootstrap")
    if len(data) == 1:
        return [float(data[0]), float(data[0])]
    rng = np.random.default_rng(AUDIT_BOOTSTRAP_SEED)
    means = rng.choice(data, size=(AUDIT_BOOTSTRAP_ITERATIONS, len(data)), replace=True).mean(axis=1)
    return [float(value) for value in np.percentile(means, [2.5, 97.5])]


def _ratio_ci(candidate: Dict[int, List[float]], baseline: Dict[int, List[float]]) -> List[float]:
    seeds = sorted(candidate)
    candidate_means = np.asarray([np.mean(candidate[seed]) for seed in seeds], dtype=float)
    baseline_means = np.asarray([np.mean(baseline[seed]) for seed in seeds], dtype=float)
    if len(seeds) == 1:
        value = _pct(float(candidate_means[0]), float(baseline_means[0]))
        return [value, value]
    rng = np.random.default_rng(AUDIT_BOOTSTRAP_SEED)
    indices = rng.integers(0, len(seeds), size=(AUDIT_BOOTSTRAP_ITERATIONS, len(seeds)))
    c_values = candidate_means[indices].mean(axis=1)
    b_values = baseline_means[indices].mean(axis=1)
    estimates = (c_values - b_values) / np.abs(b_values) * 100.0
    return [float(value) for value in np.percentile(estimates, [2.5, 97.5])]


def _load(scenario: Scenario, run_root: Path, seeds: Sequence[int]) -> Dict[Tuple[str, int], Dict[str, object]]:
    runs: Dict[Tuple[str, int], Dict[str, object]] = {}
    simulator_hashes = set()
    for algorithm in scenario.algorithms:
        for seed in seeds:
            run_dir = run_root / scenario.id / algorithm.id / f"seed_{seed:02d}"
            metadata_path = run_dir / "metadata.json"
            stats_path = run_dir / "run_stats.json"
            if not metadata_path.is_file() or not stats_path.is_file():
                raise AuditError(f"missing audit input: {run_dir}")
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            stats = load_run_stats(stats_path)
            if metadata.get("status") != "success" or metadata.get("return_code") != 0:
                raise AuditError(f"unsuccessful audit input: {run_dir}")
            if int(metadata.get("duration", -1)) != scenario.duration:
                raise AuditError(f"non-full-duration audit input: {run_dir}")
            if stats.get("integrity_errors"):
                raise AuditError(f"integrity errors in audit input: {stats_path}")
            simulator_hashes.add(stats.get("simulator_hash"))
            runs[(algorithm.id, seed)] = stats
    if len(simulator_hashes) != 1:
        raise AuditError(f"{scenario.id}: audit found multiple simulator hashes")
    return runs


def _per_ue(stats: Dict[str, object]) -> Dict[int, float]:
    denominator = float(stats["measured_ttis"])
    return {
        int(ue): round(float(total) / 1000.0 / denominator, 2)
        for ue, total in dict(stats["per_ue_tbs_kbits"]).items()
    }


def _slice_users(stats: Dict[str, object], slice_id: int, excluded: set[int] | None = None) -> List[float]:
    excluded = excluded or set()
    mapping = {int(ue): int(sid) for ue, sid in dict(stats["ue_slice"]).items()}
    # Match the paper notebooks: users with no scheduled TBS record do not
    # enter PF, percentile, or mean-throughput populations.
    return [
        value for ue, value in _per_ue(stats).items()
        if mapping[ue] == slice_id and ue not in excluded and value > 0
    ]


def _slice_tp(stats: Dict[str, object], slice_id: int) -> float:
    return float(dict(stats["per_slice_tbs_kbits"])[str(slice_id)]) / 1000.0 / float(stats["measured_ttis"])


def _mean_result(values: Sequence[float]) -> Dict[str, object]:
    return {"observations": len(values), "mean": float(np.mean(values)), "bootstrap_95_ci": _ci(values)}


def calculate_audit_summary(scenario: Scenario, run_root: Path, seeds: Sequence[int]) -> Dict[str, object]:
    runs = _load(scenario, run_root, seeds)
    baseline_id = "no_muting"
    impacts: Dict[Tuple[str, str, int], List[float]] = defaultdict(list)
    raw: Dict[Tuple[str, str, int], List[float]] = defaultdict(list)

    for algorithm in scenario.algorithms:
        if algorithm.id == baseline_id:
            continue
        for seed in seeds:
            candidate = runs[(algorithm.id, seed)]
            baseline = runs[(baseline_id, seed)]
            for sid in scenario.analysis["pf_slices"]:
                c_value = _pf(_slice_users(candidate, sid))
                b_value = _pf(_slice_users(baseline, sid))
                impacts[(algorithm.id, "pf", seed)].append(_pct(c_value, b_value))
                raw[(algorithm.id, "pf_candidate", seed)].append(c_value)
                raw[(algorithm.id, "pf_baseline", seed)].append(b_value)

            kind = scenario.analysis["kind"]
            if kind == "pf_throughput":
                for sid in scenario.analysis["throughput_slices"]:
                    c_value, b_value = _slice_tp(candidate, sid), _slice_tp(baseline, sid)
                    impacts[(algorithm.id, "throughput", seed)].append(_pct(c_value, b_value))
                    raw[(algorithm.id, "throughput_candidate", seed)].append(c_value)
                    raw[(algorithm.id, "throughput_baseline", seed)].append(b_value)
            elif kind == "pf_fairness":
                for sid in scenario.analysis["fairness_slices"]:
                    c_users, b_users = _slice_users(candidate, sid), _slice_users(baseline, sid)
                    pairs = {
                        "tail_throughput": (float(np.percentile(c_users, 10)), float(np.percentile(b_users, 10))),
                        "mean_throughput": (float(np.mean(c_users)), float(np.mean(b_users))),
                    }
                    for metric, (c_value, b_value) in pairs.items():
                        impacts[(algorithm.id, metric, seed)].append(_pct(c_value, b_value))
                        raw[(algorithm.id, metric + "_candidate", seed)].append(c_value)
                        raw[(algorithm.id, metric + "_baseline", seed)].append(b_value)

    summary: Dict[str, object] = {}
    algorithms = [item.id for item in scenario.algorithms if item.id != baseline_id]
    metrics = sorted({metric for _, metric, _ in impacts})
    for algorithm in algorithms:
        for metric in metrics:
            if (algorithm, metric, seeds[0]) not in impacts:
                continue
            if scenario.id == "scenario2" and metric in {"tail_throughput", "mean_throughput"}:
                candidates = {seed: raw[(algorithm, metric + "_candidate", seed)] for seed in seeds}
                baselines = {seed: raw[(algorithm, metric + "_baseline", seed)] for seed in seeds}
                all_candidate = [value for seed in seeds for value in candidates[seed]]
                all_baseline = [value for seed in seeds for value in baselines[seed]]
                summary[f"{algorithm}.{metric}"] = {
                    "observations": len(seeds),
                    "mean": _pct(float(np.mean(all_candidate)), float(np.mean(all_baseline))),
                    "bootstrap_95_ci": _ratio_ci(candidates, baselines),
                }
            else:
                seed_means = [float(np.mean(impacts[(algorithm, metric, seed)])) for seed in seeds]
                summary[f"{algorithm}.{metric}"] = _mean_result(seed_means)
            if metric in {"pf", "throughput"}:
                harmed = [float(np.mean(np.asarray(impacts[(algorithm, metric, seed)]) < 0) * 100) for seed in seeds]
                summary[f"{algorithm}.{metric}.harmed_pct"] = _mean_result(harmed)
                all_impacts = [value for seed in seeds for value in impacts[(algorithm, metric, seed)]]
                summary[f"{algorithm}.{metric}.all_negative"] = {
                    "value": all(value < 0 for value in all_impacts),
                    "maximum_impact": max(all_impacts),
                    "observations": len(all_impacts),
                }

    for first, second in [("radioninja", "rsep")]:
        gaps = [
            float(np.mean(impacts[(first, "pf", seed)])) - float(np.mean(impacts[(second, "pf", seed)]))
            for seed in seeds
        ]
        summary[f"{first}_minus_{second}.pf_gap"] = _mean_result(gaps)

    return summary


def audit_scenario(scenario: Scenario, run_root: Path, output_root: Path, seeds: Sequence[int]) -> Dict[str, object]:
    output_dir = output_root / scenario.id
    output_dir.mkdir(parents=True, exist_ok=True)
    # Calculate and persist before reading the operator's summary.
    audit_summary = calculate_audit_summary(scenario, run_root, seeds)
    atomic_write_json(output_dir / "audit_summary.json", audit_summary)

    operator_path = output_dir / "summary.json"
    if not operator_path.is_file():
        raise AuditError(f"missing operator summary: {operator_path}")
    operator = json.loads(operator_path.read_text(encoding="utf-8"))
    comparisons: List[Dict[str, object]] = []
    for key in sorted(audit_summary):
        if key not in operator:
            comparisons.append({"key": key, "passed": False, "error": "missing operator result"})
            continue
        if "mean" in audit_summary[key]:
            audit_value = float(audit_summary[key]["mean"])
            operator_value = float(operator[key]["mean"])
            difference = audit_value - operator_value
            audit_ci = [float(value) for value in audit_summary[key]["bootstrap_95_ci"]]
            operator_ci = [float(value) for value in operator[key]["bootstrap_95_ci"]]
            ci_difference = [audit_ci[index] - operator_ci[index] for index in range(2)]
            passed = abs(difference) <= 1e-9 and max(abs(value) for value in ci_difference) <= 1e-9
        else:
            audit_value = audit_summary[key]["value"]
            operator_value = operator[key]["value"]
            difference = None
            audit_ci = None
            operator_ci = None
            ci_difference = None
            passed = audit_value == operator_value
        comparisons.append({
            "key": key,
            "operator": operator_value,
            "auditor": audit_value,
            "difference": difference,
            "operator_ci": operator_ci,
            "auditor_ci": audit_ci,
            "ci_difference": ci_difference,
            "passed": passed,
        })

    dossier = json.loads((ROOT / "artifact" / "scenarios" / "reference_intervals.json").read_text(encoding="utf-8"))
    target_checks: List[Dict[str, object]] = []
    for criterion in dossier[scenario.id]["checks"]:
        result = audit_summary[criterion["summary_key"]]
        if criterion["criterion"] == "mean_in_interval":
            lower, upper = criterion["interval"]
            observed = float(result["mean"])
            passed = float(lower) <= observed <= float(upper)
        elif criterion["criterion"] == "paper_value_in_ci":
            lower, upper = result["bootstrap_95_ci"]
            observed = float(result["mean"])
            passed = float(lower) <= float(criterion["paper_value"]) <= float(upper)
        else:
            observed = result
            passed = bool(result["value"])
        paper = criterion.get("paper_value")
        deviation = observed - float(paper) if isinstance(observed, float) and isinstance(paper, (int, float)) else None
        target_checks.append({
            **criterion,
            "auditor_observed": observed,
            "deviation_from_paper": deviation,
            "passed": passed,
        })

    report: Dict[str, object] = {
        "scenario": scenario.id,
        "analysis_seed": AUDIT_BOOTSTRAP_SEED,
        "bootstrap_resamples": AUDIT_BOOTSTRAP_ITERATIONS,
        "calculated_before_operator_summary_read": True,
        "operator_agreement": all(item["passed"] for item in comparisons),
        "target_acceptance": all(item["passed"] for item in target_checks),
        "comparisons": comparisons,
        "target_checks": target_checks,
    }
    if scenario.id == "scenario2":
        algorithm = next(item for item in scenario.algorithms if item.id == "som_pf")
        report["som_pf_discrepancy_investigation"] = {
            "manifest_algorithm": algorithm.id,
            "manifest_config": str(algorithm.config.relative_to(ROOT)),
            "config_sha256": __import__("hashlib").sha256(algorithm.config.read_bytes()).hexdigest(),
            "fresh_direct_paired_pf_result": audit_summary["som_pf.pf"],
            "conclusion": "Mapping is SOM-PF -> ea_pf.json. The audit pairs each seed/slice with no_muting and applies the paper per-UE rounding convention.",
        }
    report["passed"] = bool(report["operator_agreement"] and report["target_acceptance"])
    atomic_write_json(output_dir / "audit_report.json", report)
    return report
