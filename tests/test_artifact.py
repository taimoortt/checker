import json
import tempfile
import unittest
from pathlib import Path

from radioninja_artifact.analysis import parse_log, percent_change, pf_metric
from radioninja_artifact.cli import parse_seeds
from radioninja_artifact.manifest import ROOT, load_scenario, scenario_ids
from radioninja_artifact.manifest import load_selected
from radioninja_artifact.runner import RunSpec, simulator_command


class ManifestTests(unittest.TestCase):
    def test_scenarios_one_and_two_can_be_selected_together(self):
        self.assertEqual([item.id for item in load_selected("1,2")], ["scenario1", "scenario2"])

    def test_all_scenarios_validate(self):
        for scenario_id in scenario_ids():
            scenario = load_scenario(scenario_id)
            self.assertEqual(scenario.topology["users"], 160)
            self.assertEqual(scenario.topology["slices"], 8)
            self.assertLessEqual(scenario.duration, 3)

    def test_scenario_two_uses_configured_higher_fairness(self):
        scenario = load_scenario("scenario2")
        for algorithm in scenario.algorithms:
            config = json.loads(algorithm.config.read_text(encoding="utf-8"))
            self.assertEqual(
                [group["algo_psi"] for group in config["slices"]],
                [1, scenario.analysis["fairness_psi"]],
            )

class CommandTests(unittest.TestCase):
    def test_seed_parser(self):
        self.assertEqual(parse_seeds("0,2,4-6,2"), [0, 2, 4, 5, 6])

    def test_simulator_command_is_portable_and_unprivileged(self):
        scenario = load_scenario("scenario1")
        spec = RunSpec(scenario, scenario.algorithms[0], 7, Path("/tmp/runs"), 1)
        command = simulator_command(spec)
        self.assertEqual(command[0], str(ROOT / "LTE-Sim"))
        self.assertNotIn("sudo", command)
        self.assertEqual(command[-1], "7")
        self.assertEqual(command[-3], "1")


class AnalysisTests(unittest.TestCase):
    def test_log_parser_and_metrics(self):
        contents = """CREATED InternetFlow APPLICATION, ID 1
TTI: 200
\t\tflow 1 cell: 0 slice: 4 nb_of_rbs: 1 eff_sinr: 2.0 tbs_size: 1000
\t\tflow 2 cell: 0 slice: 4 nb_of_rbs: 1 eff_sinr: 3.0 tbs_size: 2000
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stdout.log"
            path.write_text(contents, encoding="utf-8")
            parsed = parse_log(path)
        self.assertEqual(parsed.max_tti, 200)
        self.assertEqual(parsed.internet_users, {1})
        self.assertEqual(parsed.slice_user_throughputs(4, parsed.internet_users), [0.02])
        parsed.ue_tbs_kbits[3] = 0
        parsed.ue_slice[3] = 4
        self.assertNotIn(0.0, parsed.slice_user_throughputs(4))
        self.assertAlmostEqual(pf_metric([1.0, 10.0]), 1.0)
        self.assertAlmostEqual(percent_change(12.0, 10.0), 20.0)


if __name__ == "__main__":
    unittest.main()
