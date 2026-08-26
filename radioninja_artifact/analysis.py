from __future__ import annotations

import gzip
import json
import math
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Mapping, Sequence, Set, Tuple

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

from .manifest import ROOT, Scenario
from .run_stats import RunStatsError, atomic_write_json, load_run_stats


FLOW_RE = re.compile(
    r"\bflow\s+(?P<ue>\d+)\s+cell:\s+(?P<cell>\d+)\s+slice:\s+(?P<slice>\d+)\s+"
    r"nb_of_rbs:\s+(?P<rbs>\d+)\s+eff_sinr:\s+(?P<sinr>\S+)\s+tbs_size:\s+(?P<tbs>\d+)"
)
TTI_RE = re.compile(r"\bTTI:\s*(\d+)")
INTERNET_USER_RE = re.compile(r"CREATED InternetFlow APPLICATION, ID\s+(\d+)")
RUNTIME_PRIORITY_RE = re.compile(r"\bpriority:\s*(\S+)\s+flow_id:\s*(\d+)")
WARMUP_TTIS = 99
BOOTSTRAP_SEED = 2026
BOOTSTRAP_ITERATIONS = 10_000


class AnalysisError(RuntimeError):
    pass


@dataclass
class ParsedLog:
    max_tti: int
    ue_tbs_kbits: Dict[int, float]
    ue_slice: Dict[int, int]
    slice_tbs_kbits: Dict[int, float]
    internet_users: Set[int]
    measured_ttis_override: int | None = None
    internet_user_priorities: Dict[int, Set[float]] | None = None
    configured_internet_user_priorities: Dict[int, Set[float]] | None = None
    record_count: int = 0

    @property
    def measured_ttis(self) -> int:
        duration = self.measured_ttis_override
        if duration is None:
            duration = self.max_tti - WARMUP_TTIS
        if duration <= 0:
            raise AnalysisError(f"Log ended before measurement window: max TTI {self.max_tti}")
        return duration

    def per_ue_throughput(self) -> Dict[int, float]:
        # This per-UE rounding is the convention used by the paper notebooks.
        return {
            ue: round(total / 1000.0 / self.measured_ttis, 2)
            for ue, total in self.ue_tbs_kbits.items()
        }

    def per_slice_throughput(self) -> Dict[int, float]:
        return {
            sid: total / 1000.0 / self.measured_ttis
            for sid, total in self.slice_tbs_kbits.items()
        }

    def slice_user_throughputs(
        self, slice_id: int, exclude: Set[int] | None = None, include_zero: bool = False
    ) -> List[float]:
        excluded = exclude or set()
        per_ue = self.per_ue_throughput()
        return [
            throughput
            for ue, throughput in per_ue.items()
            if self.ue_slice.get(ue) == slice_id
            and ue not in excluded
            and (include_zero or throughput > 0)
        ]


def _open_text(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return path.open(encoding="utf-8", errors="replace")


def parse_log(path: Path, expected_max_tti: int | None = None) -> ParsedLog:
    """Parse an uncompressed or gzip diagnostic log (legacy fallback/tests)."""
    if not path.is_file():
        raise AnalysisError(f"Missing log: {path}")
    max_tti = -1
    record_count = 0
    ue_tbs: Dict[int, float] = defaultdict(float)
    ue_slice: Dict[int, int] = {}
    slice_tbs: Dict[int, float] = defaultdict(float)
    internet_users: Set[int] = set()
    priorities: Dict[int, Set[float]] = defaultdict(set)
    with _open_text(path) as handle:
        for line in handle:
            tti_match = TTI_RE.search(line)
            if tti_match:
                max_tti = max(max_tti, int(tti_match.group(1)))
            internet_match = INTERNET_USER_RE.search(line)
            if internet_match:
                internet_users.add(int(internet_match.group(1)))
            priority_match = RUNTIME_PRIORITY_RE.search(line)
            if priority_match:
                try:
                    priorities[int(priority_match.group(2))].add(float(priority_match.group(1)))
                except ValueError:
                    pass
            flow_match = FLOW_RE.search(line)
            if not flow_match:
                continue
            record_count += 1
            ue = int(flow_match.group("ue"))
            slice_id = int(flow_match.group("slice"))
            tbs = float(flow_match.group("tbs"))
            previous = ue_slice.setdefault(ue, slice_id)
            if previous != slice_id:
                raise AnalysisError(f"{path}: UE {ue} changes slice from {previous} to {slice_id}")
            ue_tbs[ue] += tbs
            slice_tbs[slice_id] += tbs
    override = None
    if expected_max_tti is not None:
        max_tti = expected_max_tti
        override = expected_max_tti - WARMUP_TTIS
    if max_tti < 0 or not ue_tbs:
        raise AnalysisError(f"{path}: no measurement records found")
    return ParsedLog(
        max_tti, dict(ue_tbs), ue_slice, dict(slice_tbs), internet_users,
        override, dict(priorities), None, record_count,
    )


def _from_stats(stats: Dict[str, object]) -> ParsedLog:
    try:
        priorities = {
            int(ue): {float(value) for value in values}
            for ue, values in dict(stats.get("internet_user_priorities", {})).items()
        }
        configured_priorities = {
            int(ue): {float(value) for value in values}
            for ue, values in dict(stats.get("internet_user_configured_priorities", {})).items()
        }
        return ParsedLog(
            int(stats.get("max_observed_tti", -1)),
            {int(ue): float(total) for ue, total in dict(stats["per_ue_tbs_kbits"]).items()},
            {int(ue): int(sid) for ue, sid in dict(stats["ue_slice"]).items()},
            {int(sid): float(total) for sid, total in dict(stats["per_slice_tbs_kbits"]).items()},
            {int(ue) for ue in stats.get("internet_user_ids", [])},
            int(stats["measured_ttis"]), priorities, configured_priorities,
            int(stats["record_count"]),
        )
    except (KeyError, TypeError, ValueError) as exc:
        raise AnalysisError(f"Malformed compact run statistics: {exc}") from exc


def pf_metric(values: Sequence[float]) -> float:
    positive = [value for value in values if value > 0]
    if not positive:
        raise AnalysisError("PF metric requires at least one positive throughput")
    return sum(math.log10(value) for value in positive)


def percent_change(candidate: float, baseline: float) -> float:
    if baseline == 0:
        raise AnalysisError("Cannot calculate percentage change from zero baseline")
    return (candidate - baseline) / abs(baseline) * 100.0


def bootstrap_ci(values: Sequence[float], iterations: int = BOOTSTRAP_ITERATIONS) -> Tuple[float, float]:
    array = np.asarray(values, dtype=float)
    if array.size == 0:
        raise AnalysisError("Bootstrap interval requires at least one observation")
    if array.size == 1:
        value = float(array[0])
        return value, value
    rng = np.random.default_rng(BOOTSTRAP_SEED)
    samples = rng.choice(array, size=(iterations, array.size), replace=True).mean(axis=1)
    lower, upper = np.percentile(samples, [2.5, 97.5])
    return float(lower), float(upper)


def _bootstrap_ratio_columns(
    rows: pd.DataFrame,
    candidate_column: str,
    baseline_column: str,
    iterations: int = BOOTSTRAP_ITERATIONS,
) -> Tuple[float, float]:
    grouped = rows.groupby("seed")[[candidate_column, baseline_column]].mean().sort_index()
    if grouped.empty:
        raise AnalysisError("Paired bootstrap requires at least one seed")
    if len(grouped) == 1:
        value = percent_change(float(grouped[candidate_column].iloc[0]), float(grouped[baseline_column].iloc[0]))
        return value, value
    candidate = grouped[candidate_column].to_numpy(dtype=float)
    baseline = grouped[baseline_column].to_numpy(dtype=float)
    rng = np.random.default_rng(BOOTSTRAP_SEED)
    indices = rng.integers(0, len(grouped), size=(iterations, len(grouped)))
    candidate_means = candidate[indices].mean(axis=1)
    baseline_means = baseline[indices].mean(axis=1)
    values = (candidate_means - baseline_means) / np.abs(baseline_means) * 100.0
    lower, upper = np.percentile(values, [2.5, 97.5])
    return float(lower), float(upper)


def _load_runs(
    run_root: Path, scenario: Scenario, seeds: Iterable[int]
) -> Tuple[Dict[Tuple[str, int], ParsedLog], List[Dict[str, object]]]:
    parsed: Dict[Tuple[str, int], ParsedLog] = {}
    metadata_rows: List[Dict[str, object]] = []
    simulator_hashes: Set[str] = set()
    for algorithm in scenario.algorithms:
        for seed in seeds:
            run_dir = run_root / scenario.id / algorithm.id / f"seed_{seed:02d}"
            metadata_path = run_dir / "metadata.json"
            stats_path = run_dir / "run_stats.json"
            if not metadata_path.is_file():
                raise AnalysisError(f"Missing run metadata: {metadata_path}")
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            if metadata.get("status") != "success" or metadata.get("return_code") != 0:
                raise AnalysisError(f"Run is not successful: {metadata_path}")
            if int(metadata.get("duration", -1)) != scenario.duration:
                raise AnalysisError(f"Run is not full-duration: {metadata_path}")
            if not stats_path.is_file():
                # Retained compressed diagnostics remain supported for migration.
                candidates = [run_dir / "stdout.log.gz", run_dir / "stdout.log"]
                log_path = next((path for path in candidates if path.is_file()), None)
                if log_path is None:
                    raise AnalysisError(f"Missing compact statistics and diagnostic log: {run_dir}")
                parsed[(algorithm.id, seed)] = parse_log(log_path, scenario.duration * 1000)
            else:
                try:
                    stats = load_run_stats(stats_path)
                except RunStatsError as exc:
                    raise AnalysisError(str(exc)) from exc
                if stats.get("integrity_errors"):
                    raise AnalysisError(f"Run statistics failed integrity: {stats_path}")
                parsed[(algorithm.id, seed)] = _from_stats(stats)
                simulator_hashes.add(str(stats.get("simulator_hash")))
            metadata_rows.append(metadata)
    if len(simulator_hashes) > 1:
        raise AnalysisError(f"{scenario.id}: runs used multiple simulator binaries")
    return parsed, metadata_rows


def _mean_summary(values: Sequence[float]) -> Dict[str, object]:
    lower, upper = bootstrap_ci(values)
    return {
        "observations": len(values),
        "mean": float(np.mean(values)),
        "bootstrap_95_ci": [lower, upper],
    }


def _ratio_estimator(rows: pd.DataFrame) -> float:
    return percent_change(float(rows["value"].mean()), float(rows["baseline"].mean()))


def _write_summary(records: pd.DataFrame, scenario: Scenario, output_dir: Path) -> Dict[str, object]:
    summary: Dict[str, object] = {}
    for (algorithm, metric), group in records.groupby(["algorithm", "metric"]):
        if scenario.id == "scenario2" and metric in {"tail_throughput", "mean_throughput"}:
            mean = _ratio_estimator(group)
            lower, upper = _bootstrap_ratio_columns(group, "value", "baseline")
            observations = int(group["seed"].nunique())
        else:
            seed_values = group.groupby("seed")["improvement_pct"].mean().astype(float).tolist()
            mean = float(np.mean(seed_values))
            lower, upper = bootstrap_ci(seed_values)
            observations = len(seed_values)
        summary[f"{algorithm}.{metric}"] = {
            "observations": observations,
            "mean": mean,
            "bootstrap_95_ci": [lower, upper],
        }

        if metric in {"pf", "throughput"}:
            harmed = group.groupby("seed")["improvement_pct"].apply(lambda values: float((values < 0).mean() * 100))
            summary[f"{algorithm}.{metric}.harmed_pct"] = _mean_summary(harmed.tolist())
            impacts = group["improvement_pct"].astype(float)
            summary[f"{algorithm}.{metric}.all_negative"] = {
                "value": bool((impacts < 0).all()),
                "maximum_impact": float(impacts.max()),
                "observations": int(len(impacts)),
            }

    pf_seed = (
        records[records["metric"] == "pf"]
        .groupby(["algorithm", "seed"])["improvement_pct"].mean().unstack("algorithm")
    )
    for greater, lesser in [("radioninja", "rsep")]:
        if greater in pf_seed and lesser in pf_seed:
            values = (pf_seed[greater] - pf_seed[lesser]).astype(float).tolist()
            summary[f"{greater}_minus_{lesser}.pf_gap"] = _mean_summary(values)

    atomic_write_json(output_dir / "summary.json", summary)
    return summary


def _cdf_plot(frame: pd.DataFrame, metric: str, labels: Mapping[str, str], output: Path, xlabel: str) -> None:
    fig, ax = plt.subplots(figsize=(5, 3))
    styles = ["-", "--", ":", "-."]
    for index, algorithm in enumerate(labels):
        values = frame[(frame["algorithm"] == algorithm) & (frame["metric"] == metric)]["improvement_pct"]
        sns.ecdfplot(values, ax=ax, label=labels[algorithm], color="black", linestyle=styles[index % len(styles)], linewidth=1.6)
    ax.axvline(0, color="black", linestyle="--", linewidth=1)
    ax.grid(linestyle="--", alpha=0.6)
    ax.set_xlabel(xlabel)
    ax.set_ylabel("CDF")
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(output, dpi=300, bbox_inches="tight")
    plt.close(fig)


def _bar_plot(frame: pd.DataFrame, metric: str, labels: Mapping[str, str], output: Path, ylabel: str) -> None:
    subset = frame[frame["metric"] == metric].copy()
    subset["label"] = subset["algorithm"].map(labels)
    fig, ax = plt.subplots(figsize=(4, 3))
    sns.barplot(data=subset, x="label", y="improvement_pct", errorbar="se", ax=ax, edgecolor="black")
    ax.axhline(0, color="black", linewidth=1)
    ax.grid(linestyle="--", alpha=0.6, axis="y")
    ax.set_xlabel("Algorithm")
    ax.set_ylabel(ylabel)
    ax.tick_params(axis="x", rotation=15)
    fig.tight_layout()
    fig.savefig(output, dpi=300, bbox_inches="tight")
    plt.close(fig)


def analyze_scenario(scenario: Scenario, run_root: Path, output_root: Path, seeds: Sequence[int]) -> Dict[str, object]:
    parsed, metadata_rows = _load_runs(run_root, scenario, seeds)
    output_dir = output_root / scenario.id
    output_dir.mkdir(parents=True, exist_ok=True)
    records: List[Dict[str, object]] = []
    baseline_id = "no_muting"

    for algorithm in scenario.algorithms:
        if algorithm.id == baseline_id:
            continue
        for seed in seeds:
            candidate = parsed[(algorithm.id, seed)]
            baseline = parsed[(baseline_id, seed)]
            for slice_id in scenario.analysis["pf_slices"]:
                candidate_metric = pf_metric(candidate.slice_user_throughputs(slice_id))
                baseline_metric = pf_metric(baseline.slice_user_throughputs(slice_id))
                records.append({"seed": seed, "slice": slice_id, "algorithm": algorithm.id, "metric": "pf", "value": candidate_metric, "baseline": baseline_metric, "improvement_pct": percent_change(candidate_metric, baseline_metric)})

            if scenario.analysis["kind"] == "pf_throughput":
                candidate_tp = candidate.per_slice_throughput()
                baseline_tp = baseline.per_slice_throughput()
                for slice_id in scenario.analysis["throughput_slices"]:
                    records.append({"seed": seed, "slice": slice_id, "algorithm": algorithm.id, "metric": "throughput", "value": candidate_tp[slice_id], "baseline": baseline_tp[slice_id], "improvement_pct": percent_change(candidate_tp[slice_id], baseline_tp[slice_id])})
            elif scenario.analysis["kind"] == "pf_fairness":
                for slice_id in scenario.analysis["fairness_slices"]:
                    candidate_values = candidate.slice_user_throughputs(slice_id)
                    baseline_values = baseline.slice_user_throughputs(slice_id)
                    candidate_tail = float(np.percentile(candidate_values, 10))
                    baseline_tail = float(np.percentile(baseline_values, 10))
                    candidate_mean = float(np.mean(candidate_values))
                    baseline_mean = float(np.mean(baseline_values))
                    records.extend([
                        {"seed": seed, "slice": slice_id, "algorithm": algorithm.id, "metric": "tail_throughput", "value": candidate_tail, "baseline": baseline_tail, "improvement_pct": percent_change(candidate_tail, baseline_tail)},
                        {"seed": seed, "slice": slice_id, "algorithm": algorithm.id, "metric": "mean_throughput", "value": candidate_mean, "baseline": baseline_mean, "improvement_pct": percent_change(candidate_mean, baseline_mean)},
                    ])

    frame = pd.DataFrame.from_records(records)
    if frame.empty or frame["improvement_pct"].isna().any():
        raise AnalysisError(f"{scenario.id}: analysis produced missing values")
    frame.to_csv(output_dir / "metrics.csv", index=False)
    labels = {algorithm.id: algorithm.label for algorithm in scenario.algorithms if algorithm.id != baseline_id}

    if scenario.id == "scenario1":
        _cdf_plot(frame, "pf", labels, output_dir / "figure_7b_pf_cdf.pdf", "% increase in PF metric")
        _cdf_plot(frame, "throughput", labels, output_dir / "figure_7c_throughput_cdf.pdf", "% increase in throughput")
    elif scenario.id == "scenario2":
        _cdf_plot(frame, "pf", labels, output_dir / "figure_8b_pf_cdf.pdf", "% increase in PF metric")
        _bar_plot(frame, "tail_throughput", labels, output_dir / "figure_8c_tail_throughput_gain.pdf", "% gain in 10th-percentile throughput")
        _bar_plot(frame, "mean_throughput", labels, output_dir / "figure_8d_mean_throughput_change.pdf", "% change in mean throughput")

    summary = _write_summary(frame, scenario, output_dir)
    operator_report = {
        "scenario": scenario.id,
        "run_count": len(metadata_rows),
        "expected_run_count": len(scenario.algorithms) * len(seeds),
        "full_duration_seconds": scenario.duration,
        "seeds": list(seeds),
        "simulator_hashes": sorted({str(row["simulator_hash"]) for row in metadata_rows}),
        "config_hashes": sorted({str(row["config_hash"]) for row in metadata_rows}),
        "summary": summary,
    }
    atomic_write_json(output_dir / "operator_report.json", operator_report)
    return summary


def validate_summary(scenario: Scenario, output_root: Path) -> Dict[str, object]:
    output_dir = output_root / scenario.id
    summary_path = output_dir / "summary.json"
    if not summary_path.is_file():
        raise AnalysisError(f"Missing summary: {summary_path}")
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    dossier = json.loads((ROOT / "artifact" / "scenarios" / "reference_intervals.json").read_text(encoding="utf-8"))
    checks: List[Dict[str, object]] = []
    for criterion in dossier[scenario.id]["checks"]:
        key = criterion["summary_key"]
        if key not in summary:
            raise AnalysisError(f"{scenario.id}: summary missing required result {key}")
        rule = criterion["criterion"]
        result = summary[key]
        check = dict(criterion)
        if rule == "mean_in_interval":
            observed = float(result["mean"])
            lower, upper = map(float, criterion["interval"])
            passed = lower <= observed <= upper
            check.update({"observed": observed, "deviation_from_paper": _deviation(observed, criterion["paper_value"]), "passed": passed})
        elif rule == "paper_value_in_ci":
            lower, upper = map(float, result["bootstrap_95_ci"])
            target = float(criterion["paper_value"])
            check.update({"observed": float(result["mean"]), "observed_ci": [lower, upper], "deviation_from_paper": float(result["mean"]) - target, "passed": lower <= target <= upper})
        elif rule == "boolean_true":
            passed = bool(result["value"])
            check.update({"observed": result, "passed": passed})
        else:
            raise AnalysisError(f"Unknown validation criterion: {rule}")
        checks.append(check)

    report = {
        "scenario": scenario.id,
        "passed": all(check["passed"] for check in checks),
        "bootstrap_resamples": BOOTSTRAP_ITERATIONS,
        "analysis_seed": BOOTSTRAP_SEED,
        "checks": checks,
    }
    if scenario.id == "scenario2":
        report["som_pf_investigation"] = {
            "algorithm_id": "som_pf",
            "config": str(next(item.config for item in scenario.algorithms if item.id == "som_pf").relative_to(ROOT)),
            "analysis_convention": "PF is paired by seed and slice against no_muting; per-UE Mbps is rounded to two decimals before sum(log10(throughput)); zero-throughput UEs are excluded.",
            "fresh_direct_result": summary["som_pf.pf"],
        }
    atomic_write_json(output_dir / "validation_report.json", report)
    operator_path = output_dir / "operator_report.json"
    if operator_path.is_file():
        operator = json.loads(operator_path.read_text(encoding="utf-8"))
        operator["validation"] = report
        atomic_write_json(operator_path, operator)
    return report


def _deviation(observed: float, paper_value: object) -> float | None:
    if isinstance(paper_value, (int, float)):
        return observed - float(paper_value)
    return None
