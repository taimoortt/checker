# RadioNinja Scenarios 1 and 2 reproduction

This repository is a clean, runnable artifact for reproducing Figures 7b/7c and
8b/8c/8d from the RadioNinja evaluation. It contains only the simulator source,
the two experiment manifests, their configuration files, analysis/audit code,
tests, and build metadata.

The configured campaigns are:

| Scenario | Objectives | Algorithms | Seeds | Duration |
|---|---|---:|---:|---:|
| 1 | proportional fairness and maximum throughput | 5 | 0–19 | 3 seconds |
| 2 | proportional fairness and higher fairness (`algo_psi=3`) | 4 | 0–19 | 3 seconds |

The complete campaign contains 180 simulations. At most five simulator processes
run concurrently. The runner preserves at least 5 GiB for the system, requires
30 GiB available before using five jobs, and refuses to launch a batch with less
than 3 GiB free disk.

## Docker

```bash
docker build -t radioninja-artifact .
docker run --rm \
  -v "$PWD/artifacts:/opt/radioninja/artifacts" \
  radioninja-artifact reproduce --scenario all --seeds 0-19 --jobs 5
```

## Native build

The reference environment is Ubuntu 20.04 with GCC/G++ 10 and Python 3.8.

```bash
sudo apt-get update
sudo apt-get install -y build-essential g++-10 libjsoncpp-dev python3 python3-venv
python3 -m venv .venv
. .venv/bin/activate
python -m pip install pip==24.0
python -m pip install -r requirements.txt
python artifact_pipeline.py build
python -m unittest discover -s tests -v
python artifact_pipeline.py reproduce --scenario all --seeds 0-19 --jobs 5
```

For a non-executing command check:

```bash
python artifact_pipeline.py run --scenario all --seeds 0 --duration 1 --jobs 5 --dry-run
```

Generated data is written beneath `artifacts/` and is ignored by Git. Each run
atomically records compact per-UE/per-slice statistics, provenance hashes, and
completion metadata. Compressed raw logs are retained only for seed 0, failures,
and anomalies. Successful runs resume safely from matching compact statistics.

See [ARTIFACT_RUN.md](ARTIFACT_RUN.md) for individual pipeline commands and
output details.

## Citation and license

This artifact accompanies:

> M. Taimoor Tariq, Yuhang Chen, Haitham Hassanieh, and Radhika Mittal.
> “Enabling Interference-Aware RAN Slicing.” NINeS 2026.

The software is distributed under GPLv3. See [LICENSE](LICENSE), [NOTICE.md](NOTICE.md),
and [CITATION.cff](CITATION.cff).
