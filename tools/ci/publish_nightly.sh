#!/usr/bin/env bash
# Publishes a Nightly test build to gutsandbolts.net (build.yml's "Publish nightly" job).
# Usage: tools/ci/publish_nightly.sh <folder with the zips> <version> <commit> <commit unix time>
#
# The files go to /download/nightly/<commit>/..., never to the normal downloads, and
# nightly.json is only written after every file is up. The site refuses an older
# build than the one already out, and keeps only the newest two (free R2 space).
set -euo pipefail
dir=$1 version=$2 commit=$3 commit_time=$4
site=${GB_SITE:-https://gutsandbolts.net}

token=$(curl -fsS -H "Authorization: bearer $ACTIONS_ID_TOKEN_REQUEST_TOKEN" \
  "$ACTIONS_ID_TOKEN_REQUEST_URL&audience=gutsandbolts.net" | python3 -c 'import json,sys; print(json.load(sys.stdin)["value"])')
up="$site/download/upload"
q="channel=nightly&commit=$commit"

for f in "$dir"/*; do
  name=$(basename "$f")
  echo "Uploading $name"
  if [ "$(stat -c %s "$f")" -le $((80 * 1024 * 1024)) ]; then
    curl -fsS --retry 3 -X PUT -H "Authorization: Bearer $token" --data-binary @"$f" "$up/$name?$q"
  else
    # Cloudflare stops requests at 100 MB, so big files go up in 80 MB pieces.
    rm -rf pieces && mkdir pieces && split -b 80M -d -a 2 "$f" pieces/p
    id=$(curl -fsS --retry 3 -X POST -H "Authorization: Bearer $token" "$up/$name/start?$q" |
      python3 -c 'import json,sys; print(json.load(sys.stdin)["uploadId"])')
    enc=$(python3 -c 'import sys,urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$id")
    n=0; : > parts.jsonl
    for piece in pieces/p*; do
      n=$((n + 1))
      curl -fsS --retry 3 -X PUT -H "Authorization: Bearer $token" --data-binary @"$piece" \
        "$up/$name/part?$q&uploadId=$enc&n=$n" >> parts.jsonl
      echo >> parts.jsonl
    done
    python3 -c 'import json; print(json.dumps({"parts": [json.loads(l)["part"] for l in open("parts.jsonl") if l.strip()]}))' > parts.json
    curl -fsS --retry 3 -X POST -H "Authorization: Bearer $token" -H "Content-Type: application/json" \
      --data-binary @parts.json "$up/$name/finish?$q&uploadId=$enc"
  fi
  echo
done

python3 - "$dir" "$version" "$commit" "$commit_time" > nightly.json <<'PY'
import json, os, sys
folder, version, commit, when = sys.argv[1:5]
files = {}
for f in os.listdir(folder):
    kind = "windows" if "Windows" in f else "mac" if "macOS" in f else "linux" if "Linux" in f else None
    if kind: files[kind] = f
print(json.dumps({"version": version, "commit": commit, "commitTime": int(when), "files": files}))
PY
cat nightly.json
# 409 = a newer nightly is already out (this run was slower): that's fine, not a failure.
code=$(curl -sS -o publish.out -w '%{http_code}' --retry 3 -X POST -H "Authorization: Bearer $token" \
  -H "Content-Type: application/json" --data-binary @nightly.json "$site/download/publish?channel=nightly")
cat publish.out; echo
if [ "$code" = 409 ]; then echo "A newer nightly is already out; leaving it."; exit 0; fi
[ "$code" = 200 ]
