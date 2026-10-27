"""Write a CycloneDX SBOM from a Meson build directory.

Reads ``meson-info/intro-projectinfo.json`` and ``intro-dependencies.json``.
Those files exist after ``meson setup`` and list the dependencies that build
resolved, including the versions pkg-config and Qt's qmake reported.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote

_PURL_TOKEN = re.compile(r"^[A-Za-z0-9._+-]+$")
_QT_MODULE = re.compile(r"Qt6([A-Za-z][A-Za-z0-9]*)")
_SPDX_ID = re.compile(r"^[A-Za-z0-9.+-]+$")


def purl_for(name: str, version: str) -> str:
    token = name if _PURL_TOKEN.match(name) else quote(name, safe="")
    if not version:
        return f"pkg:generic/{token}"
    ver = version if _PURL_TOKEN.match(version) else quote(version, safe="")
    return f"pkg:generic/{token}@{ver}"


def qt_modules_from_link_args(link_args: object) -> list[str]:
    if not isinstance(link_args, list):
        return []
    found: set[str] = set()
    for arg in link_args:
        if isinstance(arg, str):
            found.update(_QT_MODULE.findall(arg))
    return sorted(found)


def _license_entries(license_field: object) -> list[dict]:
    if isinstance(license_field, str):
        raw = [license_field]
    elif isinstance(license_field, list):
        raw = [item for item in license_field if isinstance(item, str)]
    else:
        return []
    entries = []
    for item in raw:
        text = item.strip()
        if not text:
            continue
        key = "id" if _SPDX_ID.match(text) else "name"
        entries.append({"license": {key: text}})
    return entries


def _text(value: object) -> str:
    if value is None:
        return ""
    return str(value).strip()


def build_bom(
    projectinfo: dict,
    dependencies: list,
    *,
    timestamp: str,
    serial_number: str,
) -> dict:
    name = _text(projectinfo.get("descriptive_name") or projectinfo.get("name"))
    version = _text(projectinfo.get("version"))
    if not name or not version:
        raise ValueError("Meson project info is missing a name or version")

    root_ref = purl_for(name, version)
    root: dict = {
        "type": "application",
        "bom-ref": root_ref,
        "name": name,
        "version": version,
        "purl": root_ref,
    }
    licenses = _license_entries(projectinfo.get("license"))
    if licenses:
        root["licenses"] = licenses

    components_by_key: dict[tuple[str, str], dict] = {}
    for entry in dependencies:
        if not isinstance(entry, dict):
            continue
        dep_name = _text(entry.get("name"))
        if not dep_name:
            continue
        dep_version = _text(entry.get("version"))
        ref = purl_for(dep_name, dep_version)
        if ref == root_ref:
            continue
        component: dict = {
            "type": "library",
            "bom-ref": ref,
            "name": dep_name,
            "purl": ref,
            "scope": "required",
        }
        if dep_version:
            component["version"] = dep_version
        modules = qt_modules_from_link_args(entry.get("link_args"))
        if modules:
            component["properties"] = [{"name": "qt:modules", "value": ",".join(modules)}]
        components_by_key[(dep_name, dep_version)] = component

    components = [components_by_key[key] for key in sorted(components_by_key)]
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": serial_number,
        "version": 1,
        "metadata": {
            "timestamp": timestamp,
            "tools": {
                "components": [
                    {
                        "type": "application",
                        "name": "meson_to_cyclonedx",
                        "version": "1",
                    }
                ]
            },
            "component": root,
        },
        "components": components,
        "dependencies": [
            {
                "ref": root_ref,
                "dependsOn": [component["bom-ref"] for component in components],
            }
        ],
    }


def format_summary(bom: dict) -> str:
    root = bom["metadata"]["component"]
    rows = [
        "## SBOM",
        "",
        f"{_markdown_cell(root['name'])} {_markdown_cell(root.get('version', ''))}",
        "",
        "| Component | Version |",
        "| --- | --- |",
    ]
    for component in bom.get("components", []):
        rows.append(
            f"| `{_markdown_cell(component['name'])}` | {_markdown_cell(component.get('version', ''))} |"
        )
    return "\n".join(rows) + "\n"


def _markdown_cell(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ")


def _read_json(path: Path) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise ValueError(f"{path} not found; run meson setup first") from None
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path} is not valid JSON: {exc}") from None


def write_sbom(builddir: Path, output: Path) -> dict:
    info_dir = builddir / "meson-info"
    projectinfo = _read_json(info_dir / "intro-projectinfo.json")
    dependencies = _read_json(info_dir / "intro-dependencies.json")
    if not isinstance(projectinfo, dict) or not isinstance(dependencies, list):
        raise ValueError("unexpected Meson introspect JSON")
    bom = build_bom(
        projectinfo,
        dependencies,
        timestamp=datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        serial_number=f"urn:uuid:{uuid.uuid4()}",
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(json.dumps(bom, indent=2) + "\n", encoding="utf-8")
    temporary.replace(output)
    return bom


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Create a CycloneDX SBOM from a Meson build directory.")
    parser.add_argument("builddir", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        bom = write_sbom(args.builddir, args.output)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(format_summary(bom), end="")
    print(
        f"Wrote {args.output} ({len(bom['components'])} components)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
