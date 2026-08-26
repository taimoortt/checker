# Running the artifact

All operations use:

```bash
python3 artifact_pipeline.py COMMAND [OPTIONS]
```

## Radio trace data

Install the versioned trace dataset before a native run:

```bash
python3 artifact_pipeline.py traces
```

The command verifies the bundled archive SHA-256, extracts the 83 required RSRP
traces, and verifies the logical dataset hash. If the archive is absent from a
source distribution, the installer retrieves the same version-pinned archive.
The packaged traces contain the 1,000 TTI rows addressable by the simulator;
additional rows in the original source logs are never read. Docker installs the
same data automatically while building the image.

An existing full trace set may be used without copying it into the repository:

```bash
export RADIONINJA_TRACE_DIR=/path/to/csl_2120
python3 artifact_pipeline.py run --scenario all --jobs 5
```

The runner validates the first 1,000 rows of files `-171db.log` through
`-89db.log`. The dataset hash is included in the immutable campaign manifest,
so changing traces requires a new run directory.

## Complete reproduction

```bash
python3 artifact_pipeline.py reproduce \
  --scenario all \
  --seeds 0-49 \
  --jobs 5
```

This builds and freezes one simulator/config set, executes all 450 full-duration
runs, generates figures and compact metrics, validates results, performs an
independent recalculation, and writes a consolidated report. The paper's
evaluation uses seeds 0–49, so this is also the pipeline default and the seed
argument may be omitted.

The complete evaluation can take a long time. For a faster reproduction that
provides approximate results, use seeds 0–19:

```bash
python3 artifact_pipeline.py reproduce \
  --scenario all \
  --seeds 0-19 \
  --jobs 5
```

Results from this shortened campaign are useful for evaluation and debugging,
but will not be identical to the paper's 50-seed results or satisfy the final
full-reproduction gate.

To keep campaign data elsewhere:

```bash
python3 artifact_pipeline.py reproduce \
  --scenario all --seeds 0-49 --jobs 5 \
  --run-dir /path/to/runs \
  --output-dir /path/to/analysis
```

## Controlled workflow

```bash
python3 artifact_pipeline.py traces
python3 artifact_pipeline.py build
python3 -m unittest discover -s tests -v
python3 artifact_pipeline.py run --scenario 1 --seeds 0-49 --jobs 5
python3 artifact_pipeline.py run --scenario 2 --seeds 0-49 --jobs 5
python3 artifact_pipeline.py analyze --scenario all --seeds 0-49
python3 artifact_pipeline.py validate --scenario all --seeds 0-49
python3 artifact_pipeline.py audit --scenario all --seeds 0-49
python3 artifact_pipeline.py consolidate --scenario all --seeds 0-49
```

The manifests enforce `DURATION=3`. The `--duration` override exists only for
short smoke tests; shortened runs cannot be analyzed as full reproductions.

## Outputs

```text
artifacts/runs/<scenario>/<algorithm>/seed_XX/
  metadata.json
  run_stats.json
  stdout.log.gz   # seed 0, failures, and anomalies only
  stderr.log.gz   # seed 0, failures, and anomalies only

artifacts/analysis/<scenario>/
  metrics.csv
  summary.json
  validation_report.json
  operator_report.json
  audit_summary.json
  audit_report.json
  figure_*.pdf

artifacts/analysis/consolidated_validation_report.json
```

The campaign freeze is stored under `artifacts/runs/_campaign/`. If simulator
source or experiment inputs change, use a new run directory and rerun both
scenarios. Analysis-only changes require reanalysis, not new simulations.

## Resource and retention rules

- `--jobs` is capped at five.
- Five jobs require at least 30 GiB available memory.
- At least 5 GiB is reserved for the system.
- No batch launches below 3 GiB free disk.
- Raw logs for successful nonzero seeds are removed after compact-stat integrity checks.
- Seed 0 and all failed/anomalous logs are retained.

The pipeline never commits, uploads, or creates a public repository.
