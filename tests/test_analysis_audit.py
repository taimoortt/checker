import json
import tempfile
import unittest
from pathlib import Path

from radioninja_artifact.analysis import analyze_scenario
from radioninja_artifact.audit import audit_scenario
from radioninja_artifact.manifest import load_scenario


class IndependentAuditTests(unittest.TestCase):
    def _write_run(self, run_root, scenario, algorithm, factor):
        seed = 0
        run_dir = run_root / scenario.id / algorithm.id / "seed_00"
        run_dir.mkdir(parents=True)
        measured = scenario.duration * 1000 - 99
        ue_tbs = {}
        ue_slice = {}
        slice_tbs = {str(sid): 0 for sid in range(8)}
        internet_ids = []
        priorities = {}
        for ue in range(160):
            sid = ue // 20
            total = int(((ue % 5) + 1) * measured * 1000 * factor)
            ue_tbs[str(ue)] = total
            ue_slice[str(ue)] = sid
            slice_tbs[str(sid)] += total
        stats = {
            "schema_version": 1,
            "scenario": scenario.id,
            "algorithm": algorithm.id,
            "seed": seed,
            "duration": scenario.duration,
            "warmup_ttis": 99,
            "measured_ttis": measured,
            "config_hash": algorithm.id + "-config",
            "simulator_hash": "one-frozen-binary",
            "completion_marker_found": True,
            "max_observed_tti": scenario.duration * 1000,
            "record_count": 160,
            "per_ue_tbs_kbits": ue_tbs,
            "ue_slice": ue_slice,
            "per_slice_tbs_kbits": slice_tbs,
            "internet_user_ids": internet_ids,
            "internet_user_configured_priorities": priorities,
            "internet_user_priorities": priorities,
            "integrity_errors": [],
        }
        metadata = {
            "status": "success",
            "return_code": 0,
            "duration": scenario.duration,
            "simulator_hash": "one-frozen-binary",
            "config_hash": algorithm.id + "-config",
        }
        (run_dir / "run_stats.json").write_text(json.dumps(stats), encoding="utf-8")
        (run_dir / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")

    def test_operator_and_independent_auditor_agree_for_all_scenarios(self):
        factors = {
            "radioninja": 1.20,
            "rsep": 0.90,
            "som_pf": 1.10,
            "som_tp": 0.80,
            "no_muting": 1.00,
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            run_root = root / "runs"
            output_root = root / "analysis"
            for scenario_id in ("scenario1", "scenario2"):
                scenario = load_scenario(scenario_id)
                for algorithm in scenario.algorithms:
                    self._write_run(run_root, scenario, algorithm, factors[algorithm.id])
                analyze_scenario(scenario, run_root, output_root, [0])
                report = audit_scenario(scenario, run_root, output_root, [0])
                self.assertTrue(report["operator_agreement"], scenario_id)
                self.assertTrue(report["calculated_before_operator_summary_read"])


if __name__ == "__main__":
    unittest.main()
