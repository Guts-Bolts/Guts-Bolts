#!/usr/bin/env python3
"""Validate the source-grounded Fyxa knowledge evaluation fixtures."""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATASET = ROOT / "assets" / "fyxa" / "evals" / "engine-core.jsonl"
REQUIRED = {
    "id", "category", "prompt", "source_paths",
    "expected_facts", "must_not_claim", "tool_intent",
}
VALID_INTENTS = {"answer_only", "studio_action"}


def main() -> int:
    errors: list[str] = []
    seen: set[str] = set()
    count = 0

    try:
        lines = DATASET.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        print(f"Cannot read {DATASET}: {exc}", file=sys.stderr)
        return 2

    for line_no, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            case = json.loads(line)
        except json.JSONDecodeError as exc:
            errors.append(f"line {line_no}: invalid JSON: {exc}")
            continue
        if not isinstance(case, dict):
            errors.append(f"line {line_no}: each case must be a JSON object")
            continue

        missing = REQUIRED - case.keys()
        if missing:
            errors.append(f"line {line_no}: missing fields: {', '.join(sorted(missing))}")
            continue

        case_id = case["id"]
        if not isinstance(case_id, str) or not case_id.strip():
            errors.append(f"line {line_no}: id must be a non-empty string")
        elif case_id in seen:
            errors.append(f"line {line_no}: duplicate id {case_id!r}")
        else:
            seen.add(case_id)

        for field in ("category", "prompt"):
            if not isinstance(case[field], str) or not case[field].strip():
                errors.append(f"line {line_no}: {field} must be a non-empty string")

        for field in ("source_paths", "expected_facts", "must_not_claim"):
            value = case[field]
            if not isinstance(value, list) or not value:
                errors.append(f"line {line_no}: {field} must be a non-empty array")
                continue
            if field == "source_paths":
                for source in value:
                    if not isinstance(source, str) or not source.strip():
                        errors.append(f"line {line_no}: source paths must be non-empty strings")
                    elif not (ROOT / source).is_file():
                        errors.append(f"line {line_no}: source does not exist: {source}")
            elif any(not isinstance(item, str) or not item.strip() for item in value):
                errors.append(f"line {line_no}: {field} entries must be non-empty strings")

        if case["tool_intent"] not in VALID_INTENTS:
            errors.append(
                f"line {line_no}: tool_intent must be one of {sorted(VALID_INTENTS)}"
            )
        count += 1

    if count == 0:
        errors.append("dataset contains no evaluation cases")

    if errors:
        print("Fyxa evaluation dataset validation failed:")
        for error in errors:
            print(f" - {error}")
        return 1

    print(f"Fyxa evaluation dataset valid: {count} cases, {len(seen)} unique IDs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
