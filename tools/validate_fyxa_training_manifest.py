#!/usr/bin/env python3
"""Validate a metadata-only, rights-gated Fyxa training manifest.

This validator does not extract content, build a training dataset, or launch training.
Candidate metadata must be approved at selection/freeze time by the trusted server.
"""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path

REQUIRED = {
    "source_id", "content_sha256", "source_kind", "is_current", "is_public",
    "is_moderated", "owner_matches", "account_training_enabled",
    "content_training_excluded", "rights_attested", "staff_rights_approved",
    "age_eligibility_verified", "screening_status", "policy_version",
    "dataset_version", "approval_history",
}
ALLOWED_KINDS = {"public_game", "public_creator_upload"}
FORBIDDEN_PAYLOAD = {"text", "content", "prompt", "answer", "tool_arguments", "tool_results", "project_files", "screenshots", "messages"}

def validate(manifest: dict) -> list[str]:
    errors = []
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1 or manifest.get("purpose") != "training_candidate_manifest":
        return ["manifest must use schema_version=1 and purpose=training_candidate_manifest"]
    candidates = manifest.get("candidates")
    if not isinstance(candidates, list): return ["candidates must be an array"]
    seen = set()
    for i, item in enumerate(candidates, 1):
        if not isinstance(item, dict): errors.append(f"candidate {i}: expected object"); continue
        missing = REQUIRED - item.keys()
        if missing: errors.append(f"candidate {i}: missing fields {sorted(missing)}")
        for key in item:
            if key.lower() in FORBIDDEN_PAYLOAD: errors.append(f"candidate {i}: raw content field {key!r} is prohibited")
        sid = item.get("source_id")
        if not isinstance(sid, str) or not sid or sid in seen: errors.append(f"candidate {i}: source_id must be unique and non-empty")
        else: seen.add(sid)
        if item.get("source_kind") not in ALLOWED_KINDS: errors.append(f"candidate {i}: private/interaction/unknown sources are not allowed")
        if not isinstance(item.get("content_sha256"), str) or not re.fullmatch(r"[0-9a-fA-F]{64}", item.get("content_sha256", "")): errors.append(f"candidate {i}: content_sha256 must be 64 hex chars")
        for gate in ("is_current", "is_public", "is_moderated", "owner_matches", "account_training_enabled", "rights_attested", "staff_rights_approved", "age_eligibility_verified"):
            if item.get(gate) is not True: errors.append(f"candidate {i}: gate {gate} is not true")
        if item.get("content_training_excluded") is not False: errors.append(f"candidate {i}: content is excluded or exclusion is unknown")
        if item.get("screening_status") != "approved": errors.append(f"candidate {i}: screening must be approved (automated scan alone is not approval)")
        for field in ("policy_version", "dataset_version"):
            if not isinstance(item.get(field), str) or not item[field].strip(): errors.append(f"candidate {i}: {field} required")
        if not isinstance(item.get("approval_history"), list) or not item["approval_history"]: errors.append(f"candidate {i}: approval_history must be non-empty")
    return errors

def main() -> int:
    p = argparse.ArgumentParser(); p.add_argument("manifest", type=Path); args = p.parse_args()
    try: data = json.loads(args.manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e: print(f"Cannot read manifest: {e}"); return 2
    errors = validate(data)
    if errors:
        print("Training candidate manifest rejected:")
        for err in errors: print(" - " + err)
        return 1
    print(f"Training candidate manifest validated: {len(data['candidates'])} eligible metadata records.")
    print("Reminder: validation is not dataset freezing, content export, or a training run.")
    return 0

if __name__ == "__main__": raise SystemExit(main())
