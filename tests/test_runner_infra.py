import gzip
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from radioninja_artifact.manifest import load_scenario
from radioninja_artifact.run_stats import (
    RunStatsError,
    atomic_write_json,
    collect_run_stats,
    validate_run_stats,
)
from radioninja_artifact.runner import RunSpec, freeze_campaign, run_one, safe_job_count


FLOW_LINES = """CREATED InternetFlow APPLICATION, ID 1 Flow Rate: 5 Mbps Priority: 1000
ipflow end app - flow: 0 fct: 0.1 flowsize: 1460 priority: 1000 flow_id: 1
Setting User: 1 to Slice: 0
Setting User: 2 to Slice: 0
\t\tflow 1 cell: 0 slice: 0 nb_of_rbs: 1 eff_sinr: 2.0 tbs_size: 1000
\t\tflow 2 cell: 0 slice: 0 nb_of_rbs: 1 eff_sinr: 3.0 tbs_size: 2000
"""


class CompactStatsTests(unittest.TestCase):
    def collect(self, suffix, ending):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / f"stdout.log{suffix}"
            contents = FLOW_LINES + ending
            if suffix == ".gz":
                with gzip.open(path, "wt", encoding="utf-8") as handle:
                    handle.write(contents)
            else:
                path.write_text(contents, encoding="utf-8")
            return collect_run_stats(
                path,
                scenario="test",
                algorithm="algorithm",
                seed=4,
                duration=1,
                config_hash="config",
                simulator_hash="simulator",
            )

    def test_compressed_stats_capture_required_totals_and_priority(self):
        stats = self.collect(".gz", " SIMULATOR_DEBUG: Stop ()\n")
        self.assertEqual(stats["record_count"], 2)
        self.assertEqual(stats["measured_ttis"], 901)
        self.assertEqual(stats["per_ue_tbs_kbits"], {"1": 1000, "2": 2000})
        self.assertEqual(stats["per_slice_tbs_kbits"], {"0": 3000})
        self.assertEqual(stats["internet_user_ids"], [1])
        self.assertEqual(stats["internet_user_configured_priorities"], {"1": [1000.0]})
        self.assertEqual(stats["internet_user_priorities"], {"1": [1000.0]})
        self.assertEqual(
            validate_run_stats(
                stats,
                expected_users=2,
                expected_slices=1,
                expected_internet_users=1,
                expected_internet_priority=1000,
            ),
            [],
        )

    def test_early_ip_flow_exit_is_not_full_duration_success(self):
        stats = self.collect("", "All IP Flows Complete. Exiting\n")
        errors = validate_run_stats(stats, expected_users=2, expected_slices=1)
        self.assertTrue(any("full-duration marker" in error for error in errors))

    def test_configured_priority_required_for_every_internet_user(self):
        stats = self.collect("", " SIMULATOR_DEBUG: Stop ()\n")
        stats["internet_user_ids"] = [1, 2]
        errors = validate_run_stats(
            stats,
            expected_users=2,
            expected_slices=1,
            expected_internet_users=2,
            expected_internet_priority=1000,
        )
        self.assertIn("not every Internet user has a configured priority record", errors)

    def test_partial_runtime_priority_evidence_must_be_correct(self):
        stats = self.collect("", " SIMULATOR_DEBUG: Stop ()\n")
        stats["internet_user_priorities"] = {"1": [7.0]}
        errors = validate_run_stats(
            stats,
            expected_users=2,
            expected_slices=1,
            expected_internet_users=1,
            expected_internet_priority=1000,
        )
        self.assertIn("observed Internet user runtime priorities are not all 1000", errors)

    def test_initialization_mapping_preserves_zero_throughput_ues(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stdout.log.gz"
            contents = """Setting User: 0 to Slice: 0
Setting User: 1 to Slice: 0
Setting User: 2 to Slice: 1
Counter: 0 Reassigning UE: 0 to Slice: 1
Counter: 1 Reassigning UE: 1 to Slice: 0
Counter: 2 Reassigning UE: 2 to Slice: 1
\t\tflow 1 cell: 0 slice: 0 nb_of_rbs: 1 eff_sinr: 2.0 tbs_size: 1000
 SIMULATOR_DEBUG: Stop ()
"""
            with gzip.open(path, "wt", encoding="utf-8") as handle:
                handle.write(contents)
            stats = collect_run_stats(
                path,
                scenario="test",
                algorithm="algorithm",
                seed=0,
                duration=1,
                config_hash="config",
                simulator_hash="simulator",
            )

        self.assertEqual(stats["per_ue_tbs_kbits"], {"0": 0, "1": 1000, "2": 0})
        self.assertEqual(stats["ue_slice"], {"0": 1, "1": 0, "2": 1})
        self.assertEqual(stats["per_slice_tbs_kbits"], {"0": 1000, "1": 0})
        self.assertEqual(
            validate_run_stats(stats, expected_users=3, expected_slices=2),
            [],
        )

    def test_flow_slice_must_match_final_reassignment(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stdout.log"
            path.write_text(
                "Setting User: 7 to Slice: 0\n"
                "Counter: 0 Reassigning UE: 7 to Slice: 1\n"
                "flow 7 cell: 0 slice: 0 nb_of_rbs: 1 eff_sinr: 2.0 tbs_size: 1000\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                RunStatsError, "assigned to slice 1 but has flow records for slice 0"
            ):
                collect_run_stats(
                    path,
                    scenario="test",
                    algorithm="algorithm",
                    seed=0,
                    duration=1,
                    config_hash="config",
                    simulator_hash="simulator",
                )

    def test_atomic_json_has_no_partial_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run_stats.json"
            atomic_write_json(path, {"schema_version": 1, "record_count": 2})
            self.assertEqual(json.loads(path.read_text()), {"record_count": 2, "schema_version": 1})
            self.assertFalse(path.with_name("run_stats.json.tmp").exists())

    @staticmethod
    def simulator_output():
        lines = []
        for ue in range(160):
            slice_id = ue % 8
            lines.append(
                f"\t\tflow {ue} cell: 0 slice: {slice_id} nb_of_rbs: 1 "
                "eff_sinr: 2.0 tbs_size: 1000\n"
            )
        lines.append(" SIMULATOR_DEBUG: Stop ()\n")
        return "".join(lines).encode()

    @mock.patch("radioninja_artifact.runner.subprocess.Popen")
    def test_successful_seed_retention_policy(self, popen_mock):
        output = self.simulator_output()

        def fake_popen(command, cwd, stdout, stderr, env):
            process = mock.Mock()
            process.stdout = io.BytesIO(output)
            process.stderr = io.BytesIO(b"")
            process.wait.return_value = 0
            return process

        popen_mock.side_effect = fake_popen
        scenario = load_scenario("scenario1")
        algorithm = scenario.algorithms[0]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed_zero = RunSpec(scenario, algorithm, 0, root, 1)
            seed_one = RunSpec(scenario, algorithm, 1, root, 1)
            seed_forty_nine = RunSpec(scenario, algorithm, 49, root, 1)
            self.assertEqual(run_one(seed_zero)["status"], "success")
            self.assertEqual(run_one(seed_one)["status"], "success")
            self.assertEqual(run_one(seed_forty_nine)["status"], "success")
            self.assertTrue((seed_zero.run_dir / "stdout.log.gz").is_file())
            self.assertFalse((seed_one.run_dir / "stdout.log.gz").exists())
            self.assertFalse((seed_forty_nine.run_dir / "stdout.log.gz").exists())
            self.assertTrue((seed_one.run_dir / "run_stats.json").is_file())
            self.assertTrue((seed_forty_nine.run_dir / "run_stats.json").is_file())
            self.assertFalse((seed_one.run_dir / "run_stats.json.tmp").exists())

    @mock.patch(
        "radioninja_artifact.runner.trace_provenance",
        return_value={
            "dataset": "test-traces",
            "version": "v1",
            "dataset_sha256": "trace-hash",
            "trace_length": 1000,
            "rsrp_min_db": -171,
            "rsrp_max_db": -89,
            "directory": "/tmp/test-traces",
        },
    )
    def test_campaign_freeze_is_stable(self, _trace_provenance):
        with tempfile.TemporaryDirectory() as directory:
            first = freeze_campaign(Path(directory))
            second = freeze_campaign(Path(directory))
            self.assertEqual(first.manifest_hash, second.manifest_hash)
            self.assertEqual(first.simulator_hash, second.simulator_hash)
            self.assertEqual(first.trace_dataset_hash, "trace-hash")
            self.assertTrue(first.simulator.is_file())
            scenario = load_scenario("scenario1")
            self.assertTrue(first.config_path(scenario.algorithms[0].config).is_file())


class ResourceGateTests(unittest.TestCase):
    def test_rejects_more_than_five_jobs(self):
        with self.assertRaisesRegex(RuntimeError, "between 1 and 5"):
            safe_job_count(6)

    @mock.patch("radioninja_artifact.runner._available_memory", return_value=29 * 1024**3)
    def test_five_jobs_require_thirty_gib(self, _available):
        self.assertEqual(safe_job_count(5), 4)

    @mock.patch("radioninja_artifact.runner._available_memory", return_value=30 * 1024**3)
    def test_five_jobs_allowed_at_thirty_gib(self, _available):
        self.assertEqual(safe_job_count(5), 5)


if __name__ == "__main__":
    unittest.main()
