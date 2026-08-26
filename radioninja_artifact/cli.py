from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Iterable, List

from .analysis import AnalysisError, analyze_scenario, validate_summary
from .audit import AuditError, audit_scenario
from .consolidate import ConsolidationError, consolidate
from .manifest import ManifestError, load_selected
from .runner import MAX_JOBS, build_simulator, freeze_campaign, make_specs, run_many, simulator_command


DEFAULT_SEEDS = "0-49"


def parse_seeds(value: str) -> List[int]:
    seeds = set()
    for part in value.split(","):
        token = part.strip()
        if not token:
            continue
        if "-" in token:
            start_text, end_text = token.split("-", 1)
            start, end = int(start_text), int(end_text)
            if start < 0 or end < start:
                raise argparse.ArgumentTypeError(f"Invalid seed range: {token}")
            seeds.update(range(start, end + 1))
        else:
            seed = int(token)
            if seed < 0:
                raise argparse.ArgumentTypeError("Seeds must be non-negative")
            seeds.add(seed)
    if not seeds:
        raise argparse.ArgumentTypeError("At least one seed is required")
    return sorted(seeds)


def _common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--scenario", choices=["1", "2", "1,2", "scenario1", "scenario2", "all"], default="all")
    parser.add_argument(
        "--seeds",
        type=parse_seeds,
        default=parse_seeds(DEFAULT_SEEDS),
        help="Seed list/range, default: 0-49 (use 0-19 for a faster approximate reproduction)",
    )
    parser.add_argument("--run-dir", type=Path, default=Path("artifacts/runs"))
    parser.add_argument("--output-dir", type=Path, default=Path("artifacts/analysis"))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="RadioNinja reproducibility pipeline")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("build", help="Build LTE-Sim")

    run_parser = subparsers.add_parser("run", help="Run simulations")
    _common_arguments(run_parser)
    run_parser.add_argument("--jobs", type=int, default=5)
    run_parser.add_argument("--duration", type=int, default=None, help="Override duration for smoke testing")
    run_parser.add_argument("--force", action="store_true")
    run_parser.add_argument("--dry-run", action="store_true")

    analyze_parser = subparsers.add_parser("analyze", help="Generate metrics and figures")
    _common_arguments(analyze_parser)

    validate_parser = subparsers.add_parser("validate", help="Validate generated metrics")
    _common_arguments(validate_parser)

    audit_parser = subparsers.add_parser("audit", help="Independently recalculate and compare results")
    _common_arguments(audit_parser)

    consolidate_parser = subparsers.add_parser("consolidate", help="Create the final 450-run pass/fail report")
    _common_arguments(consolidate_parser)

    reproduce_parser = subparsers.add_parser("reproduce", help="Build, run, analyze, and validate")
    _common_arguments(reproduce_parser)
    reproduce_parser.add_argument("--jobs", type=int, default=5)
    reproduce_parser.add_argument("--force", action="store_true")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)
    try:
        if args.command == "build":
            build_simulator()
            return 0

        scenarios = load_selected(args.scenario)
        run_dir = args.run_dir.resolve()
        output_dir = args.output_dir.resolve()

        if args.command in {"run", "reproduce"}:
            if not 1 <= args.jobs <= MAX_JOBS:
                parser.error(f"--jobs must be between 1 and {MAX_JOBS}")
            duration = getattr(args, "duration", None)
            if getattr(args, "dry_run", False):
                specs = make_specs(scenarios, args.seeds, run_dir, duration)
                for spec in specs:
                    print(" ".join(simulator_command(spec)))
                return 0
            if args.command == "reproduce":
                build_simulator()
            campaign = freeze_campaign(run_dir)
            specs = make_specs(scenarios, args.seeds, run_dir, duration, campaign)
            run_many(specs, jobs=args.jobs, force=args.force)

        if args.command in {"analyze", "reproduce"}:
            for scenario in scenarios:
                analyze_scenario(scenario, run_dir, output_dir, args.seeds)

        reports = []
        if args.command in {"validate", "reproduce"}:
            reports = [validate_summary(scenario, output_dir) for scenario in scenarios]
            print(json.dumps(reports, indent=2))

        audits = []
        if args.command in {"audit", "reproduce"}:
            audits = [audit_scenario(scenario, run_dir, output_dir, args.seeds) for scenario in scenarios]
            print(json.dumps(audits, indent=2))

        consolidated = None
        if args.command == "consolidate" or (
            args.command == "reproduce" and len(scenarios) == 2 and args.seeds == list(range(50))
        ):
            consolidated = consolidate(scenarios, run_dir, output_dir, args.seeds)
            print(json.dumps(consolidated, indent=2))

        passed = all(report.get("passed", False) for report in reports + audits)
        if consolidated is not None:
            passed = passed and bool(consolidated.get("passed"))
        return 0 if passed else 1
    except (ManifestError, AnalysisError, AuditError, ConsolidationError, OSError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
