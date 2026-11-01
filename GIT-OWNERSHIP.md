# GIT-OWNERSHIP.md - Goal 1: everything under xliang76

## Current state (verified 2026-11-01)

- `origin` of this repo: `https://github.com/xliang76/bonefish.git` - DONE.
- Repos verified reachable (HTTP 200) under xliang76: `bonefish`, `msgpack-c`,
  `rapidjson`, `websocketpp`, `json-msgpack`, `cppcodec`, `asio`.
- `.gitmodules` on this branch: xliang76-only since P1a (rewritten 2026-11-01).

## Submodule ownership table

| submodule | current upstream URL | target URL | pinned commit (this branch) |
|---|---|---|---|
| third-party/msgpack-c | github.com/msgpack/msgpack-c | github.com/xliang76/msgpack-c | `e59f6e67` (cpp-1.3.0-54) |
| third-party/rapidjson | github.com/miloyip/rapidjson | github.com/xliang76/rapidjson | `f54b0e47` (v1.1.0) |
| third-party/websocketpp | github.com/zaphoyd/websocketpp | github.com/xliang76/websocketpp | `1b11fd30` (0.8.2-1 = v0.8.2 release + FUNDING.yml; re-pinned in P2f' for `ASIO_STANDALONE` support; old 0.7.0 pin `378437ae` is a direct ancestor) |
| third-party/json-msgpack | github.com/tplgy/json-msgpack | github.com/xliang76/json-msgpack | `55a6d772` |
| third-party/cppcodec | github.com/tplgy/cppcodec | github.com/xliang76/cppcodec | `61d9b044` |
| third-party/asio (added in P2f') | github.com/chriskohlmann/asio | github.com/xliang76/asio | `12e0ce9e` (tag `asio-1-30-2`) |

## Steps

1. Rewrite `.gitmodules` URLs to the xliang76 table above, then
   `git submodule sync --recursive && git submodule update --init --recursive`.
   **DONE 2026-11-01** (P1a). Verified: every top-level pin fetches from its
   xliang76 fork (`git -C third-party/<m> fetch <fork-url> <pin>` = OK for all
   five), and a fresh `git clone` + `git submodule update --init` of this
   branch checks out all five pins through the xliang76 URLs only.
2. **Nested-submodule caveat (found in P1a).** The pinned commits of the
   third-party projects still carry their own `.gitmodules` pointing at
   upstream (`msgpack-c` -> boostorg/predef + boostorg/preprocessor,
   `rapidjson` -> google/googletest, `cppcodec` -> philsquared/Catch). None
   of these nested trees are on any bonefish include path (we only use
   msgpack-c/include, rapidjson/include, cppcodec, json-msgpack,
   websocketpp), so `git submodule update --init` WITHOUT `--recursive`
   gives a fully xliang76-only working tree; use that in docs. A strict
   `git clone --recursive` would still reach boostorg/google/philsquared.
   Fixing that means committing `.gitmodules` cleanups inside the forks
   (new pin SHAs) AND forking googletest/Catch/boostorg-predef - requires
   push/creation approval from the owner. Until then this is a known,
   documented deviation, not a regression.
3. Prove the pins resolve from the forks only:
   ```
   git clone https://github.com/xliang76/bonefish.git t
   git -C t submodule update --init   # non-recursive, xliang76-only
   ```
   If any pinned SHA is missing from a fork (GitHub "Fork" copies all commits,
   so this should not happen), push it explicitly:
   ```
   git -C third-party/<m> push xliang76 <sha>:refs/heads/bonefish-pin
   ```
4. In the xliang76 repos: enable GitHub Actions (forks have it OFF by default),
   set default branch, drop stale branches later.
5. When P2g lands, add the `asio` submodule pointing at `xliang76/asio` and
   re-verify fresh clone from scratch.

## Known caveats

- GitHub UI forks keep a "forked from <upstream>" relationship (visible link +
  fork network). If a fully detached copy is required: create a *new* empty
  repo (not "Fork"), then `git push --mirror`, and contact GitHub support to
  sever the network link. Cosmetic unless policy requires true detachment.
- Upstream tag names may collide with local pins; prefer pinning by SHA
  (already the case) and create fork-local tags like `bonefish-pin-2025`
  pointing at those SHAs for readability.
- Never commit a drifted submodule pointer (a `+ third-party/...` line in
  `git status`): restore with `git submodule update`.
