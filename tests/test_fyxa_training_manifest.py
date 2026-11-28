import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.validate_fyxa_training_manifest import validate

def eligible():
    return {
        "source_id": "public-asset-001",
        "source_path": "games/public-asset-001.gbscene",
        "content_sha256": "a" * 64,
        "source_kind": "public_game",
        "is_current": True,
        "is_public": True,
        "is_moderated": True,
        "owner_matches": True,
        "account_training_enabled": True,
        "content_training_excluded": False,
        "rights_attested": True,
        "staff_rights_approved": True,
        "age_eligibility_verified": True,
        "screening_status": "approved",
        "policy_version": "policy-1",
        "dataset_version": "dataset-1",
        "approval_history": [{"decision": "approved", "reviewer": "staff-reviewer", "reviewed_at": "2026-11-27T00:00:00Z", "review": "rights-and-age"}],
    }

def manifest(candidate):
    return {"schema_version": 1, "purpose": "training_candidate_manifest", "candidates": [candidate]}

class TrainingManifestTests(unittest.TestCase):
    def test_accepts_candidate_with_all_required_gates(self):
        self.assertEqual(validate(manifest(eligible())), [])

    def test_rejects_opt_out(self):
        candidate = eligible()
        candidate["account_training_enabled"] = False
        self.assertTrue(any("account_training_enabled" in e for e in validate(manifest(candidate))))

    def test_rejects_per_asset_exclusion(self):
        candidate = eligible()
        candidate["content_training_excluded"] = True
        self.assertTrue(any("content_training_excluded" in e for e in validate(manifest(candidate))))

    def test_rejects_missing_age_or_rights_review(self):
        candidate = eligible()
        candidate["age_eligibility_verified"] = False
        candidate["staff_rights_approved"] = False
        errors = validate(manifest(candidate))
        self.assertTrue(any("age_eligibility_verified" in e for e in errors))
        self.assertTrue(any("staff_rights_approved" in e for e in errors))

    def test_rejects_raw_text_payload(self):
        candidate = eligible()
        candidate["approval_history"][0]["answer"] = "raw interaction text"
        self.assertTrue(any("raw content field" in e for e in validate(manifest(candidate))))

    def test_rejects_unsafe_source_path(self):
        candidate = eligible()
        candidate["source_path"] = "C:\\private\\data"
        self.assertTrue(any("safe relative" in e for e in validate(manifest(candidate))))

    def test_rejects_interaction_sources(self):
        candidate = eligible()
        candidate["source_kind"] = "fyxa_conversation"
        self.assertTrue(any("source_kind" in e for e in validate(manifest(candidate))))

    def test_rejects_unapproved_automated_screen(self):
        candidate = eligible()
        candidate["screening_status"] = "screened_pending_human_and_age_review"
        self.assertTrue(any("screening_status" in e for e in validate(manifest(candidate))))

if __name__ == "__main__":
    unittest.main()
