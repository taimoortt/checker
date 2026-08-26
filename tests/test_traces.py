import hashlib
import tarfile
import tempfile
import unittest
from pathlib import Path

from radioninja_artifact.traces import (
    TraceDataError,
    install_trace_data,
    logical_dataset_sha256,
    trace_provenance,
)


class TraceDataTests(unittest.TestCase):
    @staticmethod
    def manifest(dataset_hash=""):
        return {
            "schema_version": 1,
            "dataset": "test-traces",
            "version": "v1",
            "directory": "unused",
            "archive_root": "csl_2120",
            "bundled_archive": "does-not-exist.tar.gz",
            "download_url": "https://example.invalid/traces.tar.gz",
            "archive_sha256": "",
            "file_count": 1,
            "rsrp_min_db": -89,
            "rsrp_max_db": -89,
            "trace_length": 2,
            "dataset_sha256": dataset_hash,
        }

    def test_logical_hash_ignores_unreachable_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trace = root / "-89db.log"
            trace.write_bytes(b"first\nsecond\n")
            manifest = self.manifest()
            expected = logical_dataset_sha256(root, manifest)
            trace.write_bytes(b"first\nsecond\nunreachable\n")
            self.assertEqual(logical_dataset_sha256(root, manifest), expected)

    def test_missing_trace_has_install_instruction(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "missing"
            with self.assertRaisesRegex(TraceDataError, "artifact_pipeline.py traces"):
                trace_provenance(missing, self.manifest("not-present"))

    def test_installs_and_verifies_local_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = b"first\nsecond\n"
            trace_dir = root / "source" / "csl_2120"
            trace_dir.mkdir(parents=True)
            (trace_dir / "-89db.log").write_bytes(payload)
            manifest = self.manifest()
            manifest["dataset_sha256"] = logical_dataset_sha256(trace_dir, manifest)
            archive = root / "traces.tar.gz"
            with tarfile.open(archive, "w:gz") as handle:
                handle.add(trace_dir, arcname="csl_2120")
            manifest["archive_sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
            destination = root / "installed"
            result = install_trace_data(
                source=archive, destination=destination, manifest=manifest
            )
            self.assertEqual(result["dataset_sha256"], manifest["dataset_sha256"])
            self.assertEqual((destination / "-89db.log").read_bytes(), payload)


if __name__ == "__main__":
    unittest.main()
