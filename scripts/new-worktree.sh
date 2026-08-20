#!/usr/bin/env bash
# new-worktree.sh — spin up an isolated git worktree for a parallel Claude session.
#
# Usage:
#   scripts/new-worktree.sh <slug> [--base <branch>] [--build] [--release]
#
# Example:
#   scripts/new-worktree.sh fighter-hitbox-review
#   scripts/new-worktree.sh ui-refactor --base main --build
#
# What it does:
#   1. Creates a worktree at .claude/worktrees/<slug> on new branch agent/<slug>
#   2. Symlinks every baserom.{us,jp}.{z64,n64,v64} that exists in the main
#      tree (gitignored, too big to duplicate). The compiled binary picks
#      its region's baserom at first-run; symlinking both means the same
#      worktree can build either flavor without re-linking.
#   3. (Single repo) decomp/, libultraship/, and torch/ are in-tree folders,
#      so `git worktree add` already brings them along — no submodule clones.
#   4. Regenerates gitignored codegen (reloc stubs, yamls, reloc table, credits).
#   5. Runs `cmake -B build` (configure only; add --build to also compile).
#
# Resulting worktree is fully editable:
#   - Edit any file in decomp/, port/, libultraship/, torch/
#   - Commit normally on the worktree's agent/<slug> branch — one repo, one
#     history, no submodule pointers to bump.
#
# Parallel windows in separate worktrees never collide on source or build
# outputs.

set -euo pipefail

# ── Parse args ──
SLUG=""
BASE="main"
DO_BUILD=0
CONFIG="Debug"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --base)    BASE="$2"; shift 2 ;;
        --build)   DO_BUILD=1; shift ;;
        --release) CONFIG="Release"; shift ;;
        --debug)   CONFIG="Debug"; shift ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        -*)        echo "Unknown flag: $1" >&2; exit 1 ;;
        *)
            if [[ -n "$SLUG" ]]; then
                echo "ERROR: multiple slugs given ($SLUG, $1)" >&2; exit 1
            fi
            SLUG="$1"; shift ;;
    esac
done

if [[ -z "$SLUG" ]]; then
    echo "usage: $(basename "$0") <slug> [--base <branch>] [--build] [--release]" >&2
    exit 1
fi

# ── Paths ──
ROOT="$(git rev-parse --show-toplevel)"
WT_DIR="$ROOT/.claude/worktrees/$SLUG"
BRANCH="agent/$SLUG"

step()  { printf '\n\033[36m=== %s ===\033[0m\n' "$1"; }
fail()  { printf '\033[31mERROR: %s\033[0m\n' "$1" >&2; exit 1; }

[[ -e "$WT_DIR" ]] && fail "$WT_DIR already exists. Remove it or pick a different slug."

# Discover every baserom variant present in the main tree. The worktree
# inherits whatever the user has, so a US-only or JP-only setup still
# works — only a tree with neither US nor JP fails.
declare -a ROMS=()
for region in us jp; do
    for ext in z64 n64 v64; do
        if [[ -f "$ROOT/baserom.$region.$ext" ]]; then
            ROMS+=("baserom.$region.$ext")
            break  # first extension wins per region
        fi
    done
done
[[ ${#ROMS[@]} -gt 0 ]] || fail "no baserom.{us,jp}.{z64,n64,v64} in $ROOT"

# ── 1. Worktree + branch ──
step "Creating worktree $WT_DIR on branch $BRANCH (base: $BASE)"
git -C "$ROOT" worktree add "$WT_DIR" -b "$BRANCH" "$BASE"

# ── 2. ROM symlinks (gitignored, ~12 MB each) ──
step "Symlinking baseroms (${ROMS[*]})"
for rom in "${ROMS[@]}"; do
    ln -sf "$ROOT/$rom" "$WT_DIR/$rom"
done

# ── 3. (Single repo — nothing to clone) ──
# decomp/, libultraship/, and torch/ are ordinary in-tree folders (absorbed
# from submodules 2026-08-20 when this became a self-owned hard fork). The
# `git worktree add` above already populated them at the branch's commit, so
# there is nothing to clone or check out here.

# ── 4. Regenerate gitignored codegen ──
# reloc_data.h, yamls/us/reloc_*.yml, credits .encoded/.metadata are all
# gitignored and must be rebuilt on every fresh checkout before CMake runs.
step "Regenerating reloc codegen"
# Each tool resolves its root via __file__, so absolute-path invocation is safe.
python3 "$WT_DIR/tools/generate_reloc_stubs.py"
( cd "$WT_DIR" && python3 tools/generate_yamls.py )
( cd "$WT_DIR" && python3 tools/generate_reloc_table.py )

step "Encoding credits text"
(
    cd "$WT_DIR/decomp/src/credits"
    for f in staff.credits.us.txt titles.credits.us.txt; do
        python3 "$WT_DIR/tools/creditsTextConverter.py" "$f" > /dev/null
    done
    for f in info.credits.us.txt companies.credits.us.txt; do
        python3 "$WT_DIR/tools/creditsTextConverter.py" -paragraphFont "$f" > /dev/null
    done
)

# ── 5. CMake configure ──
if command -v ninja >/dev/null 2>&1; then GEN="Ninja"; else GEN="Unix Makefiles"; fi

step "Configuring CMake ($GEN, $CONFIG)"
cmake -S "$WT_DIR" -B "$WT_DIR/build" -G "$GEN" -DCMAKE_BUILD_TYPE="$CONFIG"

# ── 5b. Symlink extracted assets ──
# Torch extraction (SmashBrotatoes.o2r) is slow and produces bytewise-identical
# output for a given baserom. The binary loads SmashBrotatoes.o2r (ROM-derived)
# and f3d.o2r (shaders) from its CWD on launch — without them the game
# prints "archive ... does not exist" and exits. Reuse the main tree's
# extracted assets via symlink so parallel worktrees don't each re-extract.
step "Symlinking extracted assets (SmashBrotatoes.o2r / f3d.o2r)"
linked_any=0
for asset in SmashBrotatoes.o2r f3d.o2r; do
    src=""
    for cand in "$ROOT/build/$asset" "$ROOT/$asset"; do
        if [[ -f "$cand" ]]; then src="$cand"; break; fi
    done
    if [[ -n "$src" ]]; then
        ln -sf "$src" "$WT_DIR/build/$asset"
        linked_any=1
    else
        printf '  warn: %s not found in main tree (%s/build or %s) — extract it there first\n' \
            "$asset" "$ROOT" "$ROOT" >&2
    fi
done
if [[ $linked_any -eq 0 ]]; then
    printf '\033[33m  No assets symlinked. Build + extract in the main tree first (cmake --build %s/build --target ExtractAssets) before launching the worktree binary.\033[0m\n' "$ROOT" >&2
fi

# ── 6. Optional: compile ──
if [[ $DO_BUILD -eq 1 ]]; then
    if command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu)"
    elif command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS=4
    fi
    step "Building ssb64 ($JOBS jobs)"
    cmake --build "$WT_DIR/build" --target ssb64 --config "$CONFIG" -j "$JOBS"
fi

# ── Done ──
step "Worktree ready"
cat <<EOF
  Path:     $WT_DIR
  Branch:   $BRANCH  (base: $BASE)
  Build:    $WT_DIR/build       ($GEN, $CONFIG)
  ROM:      symlinked
  Submods:  libultraship, torch, decomp — independent clones,
            origin set to fork

  Point a new Claude window at: $WT_DIR
  Build:    cmake --build $WT_DIR/build --target ssb64 -j
  Remove:   git worktree remove $WT_DIR && git branch -D $BRANCH
EOF
