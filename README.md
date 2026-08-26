# RadioNinja

RadioNinja is a research prototype for interference-aware resource allocation in
radio access network slicing. This repository contains the simulation,
experiment orchestration, and analysis artifact for the paper:

> M. Taimoor Tariq, Yuhang Chen, Haitham Hassanieh, and Radhika Mittal.
> “Enabling Interference-Aware RAN Slicing.” NINeS 2026.

The artifact provides a reproducible workflow for building the simulator,
running configured experiments, generating figures and metrics, and validating
results with an independent audit.

## Organization

`src/` contains the LTE simulator and RadioNinja implementation.

`scripts/` contains slice, workload, and baseline configurations.

`artifact/` contains machine-readable experiment and validation definitions.

`radioninja_artifact/` contains the experiment runner, compact-statistics
pipeline, analysis code, and independent audit.

`tests/` contains infrastructure and analysis checks.

## Setup

### Docker

Build the artifact image:

```bash
git clone https://github.com/taimoortt/checker.git
cd checker
docker build -t radioninja-artifact .
```

Run the configured experiments and persist the generated output locally:

```bash
docker run --rm \
  -v "$PWD/artifacts:/opt/radioninja/artifacts" \
  radioninja-artifact reproduce --scenario all --jobs 5
```

### Native installation

The reference environment is Ubuntu 20.04 with GCC/G++ 10 and Python 3.8.

```bash
sudo apt-get update
sudo apt-get install -y build-essential g++-10 libjsoncpp-dev python3 python3-venv

python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt

python artifact_pipeline.py build
python -m unittest discover -s tests -v
```

Run the complete configured workflow with:

```bash
python artifact_pipeline.py reproduce --scenario all --jobs 5
```

The runner freezes the simulator and experiment inputs, records compact
per-run statistics and provenance hashes, and resumes completed work safely.
Generated data is written under `artifacts/` and is excluded from version
control.

See [ARTIFACT_RUN.md](ARTIFACT_RUN.md) for command-level usage, output formats,
resource controls, and troubleshooting.

## Status

This repository is an actively maintained research artifact. Experiment
definitions and documentation may continue to evolve as the artifact is
prepared for broader use.

## License and citation

RadioNinja is distributed under GPLv3. See [LICENSE](LICENSE),
[NOTICE.md](NOTICE.md), and [CITATION.cff](CITATION.cff) for licensing and
attribution details.
