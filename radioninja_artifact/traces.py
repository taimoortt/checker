from __future__ import annotations

import hashlib
import json
import os
import shutil
import tarfile
import tempfile
import urllib.request
from pathlib import Path
from typing import Dict, Iterable, Optional

from .manifest import ROOT


TRACE_ENVIRONMENT_VARIABLE = "RADIONINJA_TRACE_DIR"
TRACE_MANIFEST_PATH = ROOT / "artifact" / "trace_data.json"


class TraceDataError(RuntimeError):
    pass


def load_trace_manifest(path: Path = TRACE_MANIFEST_PATH) -> Dict[str, object]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise TraceDataError(f"Cannot read trace manifest {path}: {exc}") from exc
    if int(manifest.get("schema_version", -1)) != 1:
        raise TraceDataError(f"Unsupported trace manifest schema in {path}")
    return manifest


def trace_directory(
    destination: Optional[Path] = None,
    manifest: Optional[Dict[str, object]] = None,
) -> Path:
    if destination is not None:
        return destination.expanduser().resolve()
    configured = os.environ.get(TRACE_ENVIRONMENT_VARIABLE)
    if configured:
        return Path(configured).expanduser().resolve()
    data = manifest or load_trace_manifest()
    return (ROOT / str(data["directory"])).resolve()


def required_trace_names(manifest: Dict[str, object]) -> Iterable[str]:
    minimum = int(manifest["rsrp_min_db"])
    maximum = int(manifest["rsrp_max_db"])
    return (f"{rsrp}db.log" for rsrp in range(minimum, maximum + 1))


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def logical_dataset_sha256(directory: Path, manifest: Dict[str, object]) -> str:
    """Hash exactly the trace prefix the simulator can address."""
    trace_length = int(manifest["trace_length"])
    digest = hashlib.sha256()
    names = list(required_trace_names(manifest))
    if len(names) != int(manifest["file_count"]):
        raise TraceDataError("Trace manifest file count does not match its RSRP range")
    for name in names:
        path = directory / name
        if not path.is_file():
            raise TraceDataError(f"Missing required trace file: {path}")
        file_digest = hashlib.sha256()
        with path.open("rb") as handle:
            for row in range(trace_length):
                line = handle.readline()
                if not line:
                    raise TraceDataError(
                        f"{path} has fewer than {trace_length} trace rows (stopped at {row})"
                    )
                file_digest.update(line)
        digest.update(name.encode("utf-8") + b"\0" + file_digest.hexdigest().encode("ascii") + b"\n")
    return digest.hexdigest()


def trace_provenance(
    directory: Optional[Path] = None,
    manifest: Optional[Dict[str, object]] = None,
) -> Dict[str, object]:
    data = manifest or load_trace_manifest()
    location = trace_directory(directory, data)
    if not location.is_dir():
        raise TraceDataError(
            f"Radio trace data is missing at {location}. Install it with "
            "'python3 artifact_pipeline.py traces' or set "
            f"{TRACE_ENVIRONMENT_VARIABLE}."
        )
    actual = logical_dataset_sha256(location, data)
    expected = str(data["dataset_sha256"])
    if actual != expected:
        raise TraceDataError(
            f"Radio trace checksum mismatch at {location}: expected {expected}, found {actual}"
        )
    return {
        "dataset": str(data["dataset"]),
        "version": str(data["version"]),
        "dataset_sha256": actual,
        "trace_length": int(data["trace_length"]),
        "rsrp_min_db": int(data["rsrp_min_db"]),
        "rsrp_max_db": int(data["rsrp_max_db"]),
        "directory": str(location),
    }


def _safe_extract(archive: Path, target: Path, manifest: Dict[str, object]) -> Path:
    archive_root = str(manifest["archive_root"])
    allowed = {archive_root}
    allowed.update(f"{archive_root}/{name}" for name in required_trace_names(manifest))
    target.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, mode="r:gz") as handle:
        members = handle.getmembers()
        for member in members:
            normalized = member.name.rstrip("/")
            if normalized not in allowed:
                raise TraceDataError(f"Unexpected path in trace archive: {member.name}")
            if member.issym() or member.islnk() or not (member.isdir() or member.isfile()):
                raise TraceDataError(f"Unsupported entry in trace archive: {member.name}")
            resolved = (target / member.name).resolve()
            if target.resolve() not in resolved.parents and resolved != target.resolve():
                raise TraceDataError(f"Unsafe path in trace archive: {member.name}")
        handle.extractall(target)
    return target / archive_root


def _download(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "RadioNinja-artifact"})
    try:
        with urllib.request.urlopen(request) as response, destination.open("wb") as output:
            shutil.copyfileobj(response, output, length=1024 * 1024)
    except OSError as exc:
        raise TraceDataError(f"Cannot download trace data from {url}: {exc}") from exc


def _remove_replaceable_trace_directory(location: Path, manifest: Dict[str, object]) -> None:
    resolved = location.resolve()
    protected = {Path("/").resolve(), Path.home().resolve(), ROOT.resolve()}
    if resolved in protected:
        raise TraceDataError(f"Refusing to replace protected directory: {resolved}")
    if location.is_symlink() or not location.is_dir():
        location.unlink()
        return
    entries = list(location.iterdir())
    recognizable = (
        not entries
        or (location / ".installed.json").is_file()
        or any((location / name).is_file() for name in required_trace_names(manifest))
    )
    if not recognizable:
        raise TraceDataError(
            f"Refusing to replace {location}: it does not look like a RadioNinja trace directory"
        )
    shutil.rmtree(location)


def install_trace_data(
    *,
    source: Optional[Path] = None,
    destination: Optional[Path] = None,
    force: bool = False,
    manifest: Optional[Dict[str, object]] = None,
) -> Dict[str, object]:
    data = manifest or load_trace_manifest()
    location = trace_directory(destination, data)
    if location.exists() and not force:
        try:
            return trace_provenance(location, data)
        except TraceDataError as exc:
            raise TraceDataError(f"{exc} Re-run the trace installer with --force to replace it.") from exc

    location.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".trace-install-", dir=str(location.parent)) as temporary:
        temporary_root = Path(temporary)
        bundled = ROOT / str(data.get("bundled_archive", ""))
        if source is not None:
            archive = source.expanduser().resolve()
        elif bundled.is_file():
            archive = bundled
        else:
            archive = temporary_root / "traces.tar.gz"
            _download(str(data["download_url"]), archive)
        if not archive.is_file():
            raise TraceDataError(f"Trace archive does not exist: {archive}")
        actual_archive_hash = _sha256(archive)
        expected_archive_hash = str(data["archive_sha256"])
        if actual_archive_hash != expected_archive_hash:
            raise TraceDataError(
                f"Trace archive checksum mismatch: expected {expected_archive_hash}, "
                f"found {actual_archive_hash}"
            )
        extracted = _safe_extract(archive, temporary_root / "extracted", data)
        provenance = trace_provenance(extracted, data)
        if location.exists():
            _remove_replaceable_trace_directory(location, data)
        os.replace(extracted, location)

    marker = dict(provenance)
    marker["directory"] = str(location)
    marker["archive_sha256"] = str(data["archive_sha256"])
    (location / ".installed.json").write_text(
        json.dumps(marker, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return marker
