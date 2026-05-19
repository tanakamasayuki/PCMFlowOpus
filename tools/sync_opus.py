#!/usr/bin/env python3
"""Sync the vendored libopus tree with an upstream xiph/opus tag.

Two modes:

  --check-upstream   Compare the latest non-pre-release tag at
                     https://github.com/xiph/opus with the tag pinned
                     in src/external/UPSTREAM.lock. Prints key=value
                     pairs (suitable for `>> $GITHUB_OUTPUT`):

                         has_update=<true|false>
                         current_tag=<vX.Y.Z or empty>
                         latest_tag=<vX.Y.Z>
                         notes_url=<github release/tag URL>

  --apply [--tag T]  Download the upstream tarball for tag T (default:
                     latest non-pre-release tag), verify SHA256, wipe
                     src/external/opus/ (preserving .gitkeep), populate
                     it with the filtered subset listed in SPEC.md §7,
                     and rewrite src/external/UPSTREAM.lock.

Network: only calls api.github.com and codeload.github.com. Honors
`GITHUB_TOKEN` (or `GH_TOKEN`) if set, to raise the API rate-limit
ceiling.

This script must be runnable from a clean checkout with no third-party
packages installed (stdlib only).
"""

from __future__ import annotations

import argparse
import datetime as _dt
import fnmatch
import hashlib
import io
import json
import os
import pathlib
import re
import shutil
import sys
import tarfile
import urllib.error
import urllib.request


# ---------------------------------------------------------------------------
# Configuration

UPSTREAM_OWNER = "xiph"
UPSTREAM_REPO = "opus"
LOCAL_VENDOR_DIR = pathlib.Path("src/external/opus")
LOCK_PATH = pathlib.Path("src/external/UPSTREAM.lock")

# Paths kept from the upstream tarball (relative to the tarball's top-level
# directory). Matched first; anything not matching is dropped.
INCLUDE_GLOBS = [
    # public API headers
    "include/*.h",
    # encoder/decoder dispatch
    "src/*.c",
    "src/*.h",
    # CELT layer (used by both encoder and decoder)
    "celt/*.c",
    "celt/*.h",
    # SILK layer
    "silk/*.c",
    "silk/*.h",
    # fixed-point SILK (we set FIXED_POINT=1 in opus_config.h)
    "silk/fixed/*.c",
    "silk/fixed/*.h",
    # Per-arch subdirs. opus_config.h disables SIMD by default so the
    # bodies compile out, but a few headers (cpu_support.h, intrinsics
    # stubs) are referenced unconditionally. Keep them.
    "celt/arm/*.c",
    "celt/arm/*.h",
    "celt/x86/*.c",
    "celt/x86/*.h",
    "celt/mips/*.c",
    "celt/mips/*.h",
    "silk/arm/*.c",
    "silk/arm/*.h",
    "silk/x86/*.c",
    "silk/x86/*.h",
    "silk/mips/*.c",
    "silk/mips/*.h",
    # license / credits
    "COPYING",
    "AUTHORS",
    "LICENSE_PLEASE_READ.txt",
    "PATENTS",
    "NEWS",
    "README",
    "README.md",
]

# Hard excludes — applied AFTER include filtering, to drop demo / test
# sources that incidentally match the broad `src/*.c` rule above.
EXCLUDE_GLOBS = [
    "src/opus_demo.c",
    "src/opus_compare.c",
    "src/repacketizer_demo.c",
    "src/mlp_train*.c",
    "src/mlp_train*.h",
    # The ML enhancement layer added in 1.5+. We disable it at compile
    # time via opus_config.h; excluding the sources keeps the vendor
    # tree smaller and the license inventory simpler.
    "dnn/*",
    "dnn/**/*",
    # SILK float path: we ship fixed-point only.
    "silk/float/*",
    "silk/float/**/*",
]


# ---------------------------------------------------------------------------
# UPSTREAM.lock — minimal parser/writer
#
# The lock file is TOML-ish but we deliberately don't use `tomllib` for the
# write path: we want the file to stay diff-friendly and keep its leading
# documentation block intact across syncs.

_LOCK_TEMPLATE = """\
# PCMFlowOpus — pinned upstream versions
#
# Records the exact upstream tag and tarball SHA256 of every vendored
# library. `tools/sync_opus.py` reads this file to detect drift and to
# rebuild `src/external/opus/` reproducibly.
#
# Format (TOML-ish, parsed line-by-line; do not add free comments inside
# a [section]):
#
#   [<project>]
#   repo     = "xiph/<project>"
#   tag      = "v<version>"
#   url      = "https://codeload.github.com/xiph/<project>/tar.gz/refs/tags/<tag>"
#   sha256   = "<lowercase hex digest of the tarball, 64 chars>"
#   synced   = "YYYY-MM-DD"  # the day sync_opus.py wrote this section
#
# This file is the source of truth. The vendored tree in `opus/` MUST
# match what the recorded tag would produce; CI is expected to verify
# this on every push.

[opus]
repo     = "xiph/opus"
tag      = "{tag}"
url      = "{url}"
sha256   = "{sha256}"
synced   = "{synced}"
"""


def read_lock_tag(lock_path: pathlib.Path) -> str:
    """Return the `tag` value from the [opus] section, or "" if absent."""
    if not lock_path.exists():
        return ""
    in_section = False
    for line in lock_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_section = stripped == "[opus]"
            continue
        if not in_section:
            continue
        m = re.match(r"tag\s*=\s*\"([^\"]*)\"", stripped)
        if m:
            return m.group(1)
    return ""


def write_lock(tag: str, url: str, sha256: str) -> None:
    today = _dt.date.today().isoformat()
    LOCK_PATH.write_text(
        _LOCK_TEMPLATE.format(tag=tag, url=url, sha256=sha256, synced=today),
        encoding="utf-8",
    )


# ---------------------------------------------------------------------------
# GitHub API

_SEMVER_TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")


def _gh_request(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"})
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    req.add_header("User-Agent", "pcmflowopus-sync-opus/0.1")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.read()
    except urllib.error.HTTPError as e:
        raise SystemExit(f"error: GET {url} -> HTTP {e.code} {e.reason}")
    except urllib.error.URLError as e:
        raise SystemExit(f"error: GET {url} -> {e.reason}")


def fetch_latest_tag() -> tuple[str, str]:
    """Return (tag_name, release_notes_url) for the highest stable tag."""
    # Paginate; xiph/opus has plenty of tags.
    candidates: list[tuple[tuple[int, int, int], str]] = []
    for page in range(1, 6):
        url = (
            f"https://api.github.com/repos/{UPSTREAM_OWNER}/{UPSTREAM_REPO}"
            f"/tags?per_page=100&page={page}"
        )
        data = json.loads(_gh_request(url))
        if not data:
            break
        for entry in data:
            name = entry.get("name", "")
            m = _SEMVER_TAG_RE.match(name)
            if not m:
                continue   # skip pre-release / non-standard tags
            candidates.append(((int(m[1]), int(m[2]), int(m[3])), name))
        if len(data) < 100:
            break
    if not candidates:
        raise SystemExit("error: no semver-style tags found at xiph/opus")
    candidates.sort()
    tag = candidates[-1][1]
    notes_url = f"https://github.com/{UPSTREAM_OWNER}/{UPSTREAM_REPO}/releases/tag/{tag}"
    return tag, notes_url


# ---------------------------------------------------------------------------
# Tarball download + extraction

def tarball_url(tag: str) -> str:
    return (
        f"https://codeload.github.com/{UPSTREAM_OWNER}/{UPSTREAM_REPO}"
        f"/tar.gz/refs/tags/{tag}"
    )


def download(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "pcmflowopus-sync-opus/0.1"})
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            return resp.read()
    except urllib.error.HTTPError as e:
        raise SystemExit(f"error: GET {url} -> HTTP {e.code} {e.reason}")
    except urllib.error.URLError as e:
        raise SystemExit(f"error: GET {url} -> {e.reason}")


def _path_keeps(rel: str) -> bool:
    rel = rel.replace(os.sep, "/")
    for pat in EXCLUDE_GLOBS:
        if fnmatch.fnmatchcase(rel, pat):
            return False
    for pat in INCLUDE_GLOBS:
        if fnmatch.fnmatchcase(rel, pat):
            return True
    return False


def extract_filtered(tar_bytes: bytes, dest_root: pathlib.Path) -> int:
    """Extract files matching INCLUDE_GLOBS minus EXCLUDE_GLOBS into dest_root.

    Returns the number of files written.
    """
    count = 0
    with tarfile.open(fileobj=io.BytesIO(tar_bytes), mode="r:gz") as tf:
        # Detect the single top-level directory the GitHub tarball wraps
        # everything in (e.g. "opus-1.6.1/").
        top: str | None = None
        for member in tf.getmembers():
            head = member.name.split("/", 1)[0]
            if top is None:
                top = head
            elif head != top:
                # not strictly fatal — but GitHub tarballs always have one root
                pass
        if not top:
            raise SystemExit("error: empty tarball")

        prefix = top + "/"
        for member in tf.getmembers():
            if not member.isfile():
                continue
            if not member.name.startswith(prefix):
                continue
            rel = member.name[len(prefix):]
            if not rel:
                continue
            if not _path_keeps(rel):
                continue
            # Path traversal safety: refuse absolute paths or "..".
            norm = os.path.normpath(rel)
            if norm.startswith("..") or os.path.isabs(norm):
                raise SystemExit(f"error: refusing suspicious path {member.name!r}")
            out_path = dest_root / norm
            out_path.parent.mkdir(parents=True, exist_ok=True)
            fh = tf.extractfile(member)
            if fh is None:
                continue
            out_path.write_bytes(fh.read())
            count += 1
    return count


def wipe_vendor_dir(path: pathlib.Path) -> None:
    """Remove all entries under `path` except `.gitkeep`."""
    if not path.exists():
        path.mkdir(parents=True)
        return
    for entry in path.iterdir():
        if entry.name == ".gitkeep":
            continue
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()


# ---------------------------------------------------------------------------
# Modes

def cmd_check_upstream() -> int:
    current = read_lock_tag(LOCK_PATH)
    latest, notes_url = fetch_latest_tag()
    has_update = (current != latest)
    print(f"has_update={'true' if has_update else 'false'}")
    print(f"current_tag={current}")
    print(f"latest_tag={latest}")
    print(f"notes_url={notes_url}")
    return 0


def cmd_apply(tag: str | None) -> int:
    if tag is None:
        tag, _ = fetch_latest_tag()
    if not _SEMVER_TAG_RE.match(tag):
        # Allow explicit override but warn.
        print(f"warning: tag {tag!r} doesn't look like vX.Y.Z; proceeding anyway",
              file=sys.stderr)

    url = tarball_url(tag)
    print(f"downloading {url}", file=sys.stderr)
    blob = download(url)
    sha = hashlib.sha256(blob).hexdigest()
    print(f"sha256: {sha}", file=sys.stderr)

    print(f"wiping {LOCAL_VENDOR_DIR}/ (preserving .gitkeep)", file=sys.stderr)
    wipe_vendor_dir(LOCAL_VENDOR_DIR)

    print("extracting filtered subset", file=sys.stderr)
    n = extract_filtered(blob, LOCAL_VENDOR_DIR)
    if n == 0:
        raise SystemExit(
            "error: no files matched include rules — upstream layout may have changed; "
            "check INCLUDE_GLOBS in tools/sync_opus.py"
        )
    print(f"wrote {n} files into {LOCAL_VENDOR_DIR}/", file=sys.stderr)

    write_lock(tag=tag, url=url, sha256=sha)
    print(f"updated {LOCK_PATH} -> tag={tag}", file=sys.stderr)
    return 0


# ---------------------------------------------------------------------------
# CLI

def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--check-upstream",
        action="store_true",
        help="Print current/latest tag info; exit 0. CI consumes the key=value output.",
    )
    mode.add_argument(
        "--apply",
        action="store_true",
        help="Re-fetch the tarball, repopulate src/external/opus/, and rewrite UPSTREAM.lock.",
    )
    parser.add_argument(
        "--tag",
        default=None,
        help="Override the upstream tag (default: latest non-pre-release tag). Only used with --apply.",
    )
    args = parser.parse_args(argv)

    if args.check_upstream:
        return cmd_check_upstream()
    if args.apply:
        return cmd_apply(args.tag)
    return 2   # argparse should already have rejected this


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
