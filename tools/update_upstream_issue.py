#!/usr/bin/env python3
"""Publish a complete weekly report on one tracking issue (#407)."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

TITLE = "Third-party updates available"
MARKER = re.compile(r"<!-- upstream-check: ([0-9a-f]+|none) -->")


def gh(*args):
    return subprocess.run(["gh", *args], check=True, stdout=subprocess.PIPE,
                          text=True).stdout


def publish(repo, body_file, key):
    if key == "incomplete":
        print("Incomplete upstream check: keeping the last complete report.")
        return
    # Paginate both issues and comments; use an exact title and reuse a closed
    # tracking issue so closing it does not create a new issue every week.
    issues = [json.loads(line) for line in gh(
        "api", "--paginate", "repos/%s/issues?state=all" % repo,
        "--jq", '.[] | select(.pull_request == null) | {number,title,state,body} | @json'
    ).splitlines() if line.strip()]
    matching = sorted((i for i in issues if i["title"] == TITLE),
                      key=lambda i: i["number"])
    if not matching:
        if key != "none":
            gh("issue", "create", "-R", repo, "--title", TITLE,
               "--body-file", str(body_file))
        else:
            print("Nothing newer upstream and no tracking issue.")
        return
    issue = matching[0]
    number = str(issue["number"])
    comments = [json.loads(line) for line in gh(
        "api", "--paginate", "repos/%s/issues/%s/comments" % (repo, number),
        "--jq", ".[] | {body} | @json"
    ).splitlines() if line.strip()]
    keys = MARKER.findall("\n".join([issue["body"] or ""] +
                                   [c["body"] or "" for c in comments]))
    if issue["state"] == "closed" and key != "none":
        gh("issue", "reopen", number, "-R", repo)
    if keys and keys[-1] == key:
        print("Same set as the last report on #%s: no comment." % number)
    elif issue["state"] != "closed" or key != "none":
        gh("issue", "comment", number, "-R", repo,
           "--body-file", str(body_file))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--body", required=True)
    ap.add_argument("--fingerprint", required=True)
    a = ap.parse_args()
    body_file = Path(a.body)
    key = Path(a.fingerprint).read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9a-f]{16}|none|incomplete", key):
        raise ValueError("invalid report fingerprint")
    body = body_file.read_text(encoding="utf-8")
    if "<!-- upstream-check: %s -->" % key not in body:
        raise ValueError("report body and fingerprint disagree")
    run_url = os.environ.get("RUN_URL")
    if run_url:
        body += "\nRun: %s\n" % run_url
        body_file.write_text(body, encoding="utf-8")
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
        f.write(body)
    publish(os.environ["GITHUB_REPOSITORY"], body_file, key)


if __name__ == "__main__":
    main()
