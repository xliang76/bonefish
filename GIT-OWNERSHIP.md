# GIT-OWNERSHIP.md - Goal 1: everything under xliang76

## Current state (verified 2026-11-01)

- `origin` of this repo: `https://github.com/xliang76/bonefish.git` - DONE.
- Repos verified reachable (HTTP 200) under xliang76: `bonefish`, `msgpack-c`,
  `rapidjson`, `websocketpp`, `json-msgpack`, `cppcodec`, `asio`.
- `.gitmodules` on this branch STILL points at upstream orgs - the only real
  remaining work for goal 1.

## Submodule ownership table

| submodule | current upstream URL | target URL | pinned commit (this branch) |
|---|---|---|---|
| third-party/msgpack-c | github.com/msgpack/msgpack-c | github.com/xliang76/msgpack-c | `e59f6e67` (cpp-1.3.0-54) |
| third-party/rapidjson | github.com/miloyip/rapidjson | github.com/xliang76/rapidjson | `f54b0e47` (v1.1.0) |
| third-party/websocketpp | github.com/zaphoyd/websocketpp | github.com/xliang76/websocketpp | `378437ae` (0.7.0; will be re-pinned in P2g) |
| third-party/json-msgpack | github.com/tplgy/json-msgpack | github.com/xliang76/json-msgpack | `55a6d772` |
| third-party/cppcodec | github.com/tplgy/cppcodec | github.com/xliang76/cppcodec | `61d9b044` |
| third-party/asio (added in P2g) | github.com/chriskohlmann/asio | github.com/xliang76/asio | pin an asio 1.30.x tag |

## Steps

1. Rewrite `.gitmodules` URLs to the xliang76 table above, then
   `git submodule sync --recursive && git submodule update --init --recursive`.
2. Prove the pins resolve from the forks only:
   ```
   git clone --recursive https://github.com/xliang76/bonefish.git t
   # and, stricter (block upstream domains):
   # add 127.0.0.1 github.com exceptions for msgpack/miloyip/zaphoyd/tplgy/chriskohlmann
   ```
   If any pinned SHA is missing from a fork (GitHub "Fork" copies all commits,
   so this should not happen), push it explicitly:
   ```
   git -C third-party/<m> push xliang76 <sha>:refs/heads/bonefish-pin
   ```
3. In the xliang76 repos: enable GitHub Actions (forks have it OFF by default),
   set default branch, drop stale branches later.
4. When P2g lands, add the `asio` submodule pointing at `xliang76/asio` and
   re-verify recursive clone from scratch.

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
