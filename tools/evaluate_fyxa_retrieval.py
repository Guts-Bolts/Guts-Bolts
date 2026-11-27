#!/usr/bin/env python3
"""Offline retrieval-quality smoke benchmark for Fyxa's first-party knowledge corpus.

This measures source-path retrieval only. It does not call a model or imply an answer-quality score.
"""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path

STOP = {"a","an","and","are","as","at","be","but","by","for","from","how","i","in","is","it","me","my","of","on","or","our","please","the","this","to","us","we","what","when","where","which","with","you","your"}

def tokens(text: str) -> list[str]:
    return [t.lower() for t in re.findall(r"[A-Za-z0-9]+", text) if len(t) > 1 and t.lower() not in STOP]

def chunks(text: str, size: int = 1200) -> list[str]:
    out, current = [], ""
    for line in text.splitlines():
        if len(line) > size:
            if current: out.append(current); current = ""
            out.extend(line[offset:offset + size] for offset in range(0, len(line), size))
            continue
        if current and len(current) + len(line) + 1 > size:
            out.append(current); current = ""
        current += ("\n" if current else "") + line
    if current: out.append(current)
    return out

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--top-k", type=int, default=5)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((root / "assets/fyxa/knowledge/sources.json").read_text(encoding="utf-8"))
    cases = [json.loads(line) for line in (root / "assets/fyxa/evals/engine-core.jsonl").read_text(encoding="utf-8").splitlines() if line.strip()]
    if manifest.get("purpose") != "reference_only" or manifest.get("schema_version") != 1:
        raise SystemExit("Knowledge corpus manifest is not reference_only/schema v1.")
    source_texts = {}
    for src in manifest.get("sources", []):
        if src.get("authority") != "first_party_repository" or src.get("index_policy") != "reference_only":
            continue
        path = Path(src["path"])
        if path.is_absolute() or ".." in path.parts:
            raise SystemExit(f"Unsafe corpus path: {path}")
        try: source_texts[path.as_posix()] = chunks((root / path).read_text(encoding="utf-8", errors="strict"))
        except (OSError, UnicodeError) as exc: print(f"skip source {path}: {exc}")
    scores = []
    misses = []
    for case in cases:
        query = tokens(case["prompt"])
        ranked = []
        for path, pieces in source_texts.items():
            for chunk_index, piece in enumerate(pieces):
                terms = tokens(piece)
                freq = {term: terms.count(term) for term in set(terms)}
                piece_score = sum(2.0 + min(freq.get(term, 0), 3) * 0.35 for term in query if freq.get(term, 0))
                if piece_score: ranked.append((piece_score, path, chunk_index))
        ranked.sort(key=lambda row: row[0], reverse=True)
        top = [path for _, path, _ in ranked[:args.top_k]]
        expected = set(case.get("source_paths", []))
        hits = len(expected.intersection(top))
        recall = hits / max(1, len(expected))
        scores.append(recall)
        if recall == 0: misses.append(case["id"])
        print(f"{case['id']}: source recall@{args.top_k}={recall:.2f}; expected={len(expected)}; found={hits}; top={top[:3]}")
    avg = sum(scores) / len(scores) if scores else 0.0
    zero = len(misses)
    print(f"\nCases: {len(scores)}  Mean source recall@{args.top_k}: {avg:.3f}  zero-hit cases: {zero}")
    if misses: print("Zero-hit cases: " + ", ".join(misses))
    return 1 if not scores or zero > len(scores) * 0.25 else 0

if __name__ == "__main__":
    raise SystemExit(main())
