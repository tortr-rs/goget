# goget

A source-first, cross-distro-aware package manager. Given a repo spec,
`goget` clones it and builds from source using whatever build system the
project actually uses, falling back to a compatibility-checked prebuilt
release binary only when source build isn't available or desired.

## Status

All core features from the original spec are implemented:

- **Repo spec parsing** — full URLs (`https://github.com/owner/repo`), SSH
  specs (`git@github.com:owner/repo.git`, `ssh://git@host/owner/repo`), bare
  `host/owner/repo`, bare `owner/repo` (assumes github.com), and bare short
  names like `fastfetch` (resolved via the cross-host search below).
- **`goget build <repo>`** — clones into `~/.cache/goget/src/<host>/<owner>/<repo>`
  (or pulls if already cached), detects CMake / Make / Autotools via marker
  files, and builds + installs (`sudo cmake --install` / `sudo make install`).
  A build system that has no `install` target, or any step that fails, is
  reported as a loud nonzero-exit failure — never silently treated as success.
  USE flags (see below) become extra `-D` args on the CMake configure step.
- **`goget fetch <repo>`** — looks up GitHub Releases, filters assets to
  plausible Linux binaries for this host's architecture, verifies
  compatibility (see below), extracts archives and picks the right binary
  out of them (preferring a same-named file under `bin/`, then one with the
  executable bit set, then any same-named match — handles archives that
  ship a binary and a same-named completion script), verifies a checksum
  manifest if one is present (warns and continues if absent, hard-fails on
  mismatch), and installs via `sudo install -Dm755`.
- **Compatibility-aware fetch** — before trusting a release asset: matches
  architecture against `uname -m`, detects musl vs glibc hosts
  (`/lib/ld-musl-*.so.1`), rejects a musl/glibc mismatch outright, and for
  glibc-on-glibc compares the binary's minimum required glibc version
  (parsed from `objdump -T` / `GLIBC_x.y` symbol versions) against the
  host's actual version. A binary failing any check is treated as "no
  release exists" and the next candidate asset is tried.
- **Build/fetch fallback** — `build` offers to fetch a prebuilt binary if no
  recognized build system is found; `fetch` offers to build from source if
  no compatible release exists. Both go through one shared stdin-prompt
  module so chained prompts in a single run never lose typed-ahead input.
- **Short-name search** — bare names are resolved by searching GitHub,
  GitLab, and Codeberg's real APIs for an exact case-insensitive name
  match, capped at 3 per host and sorted by stars, skipping any host
  disabled in config. Zero results, one result (Y/n), and multiple results
  (a numbered picker grouped github → gitlab → codeberg, `c` to cancel) are
  all handled.
- **`goget.conf`** (`~/.config/goget/goget.conf`) — a small hand-written
  parser for `pkg.<key>{...}` blocks: `pkg.repos{github.on gitlab.on
  codeberg.on}` is the host allow-list (created with all-on defaults if
  missing; an unrecognized block key or malformed entry hard-errors).
  `pkg.USE{flag.on/off ...}` sets global USE flags, `pkg.<name>.USE{...}`
  overrides them per-package (override beats global beats default-on). A
  small hardcoded table maps flags to build options, starting with
  fastfetch (`wayland`→`-DENABLE_WAYLAND`, `x11`→both
  `-DENABLE_XCB_RANDR`/`-DENABLE_XRANDR`, `pulseaudio`→`-DENABLE_PULSE`).
  If a direct build/fetch target resolves to a host disabled in config, you're
  prompted Once/Always/Cancel (Always persists the change; this only ever
  applies to the explicitly-requested top-level package). `goget config`
  with no args prints the fully-resolved current configuration.
- **`goget show <repo>`** — fetches just the README and a recognized
  build-system file (not a full clone) via each host's raw-file API,
  concatenates them with `=== README ===` / `=== BUILD SCRIPT ===`
  banners, and pages the result via `$PAGER` (falling back to `less`).
  Leaves nothing in goget's cache.
- **`goget makeuser`** — interactively creates a user account: username,
  masked password (real termios echo suppression, entered twice to
  confirm, retried on mismatch), and an admin-group yes/no. Detects
  whether the system uses `wheel` or `sudo` as the admin group; if both or
  neither exist, asks the operator running it rather than guessing. Wraps
  `useradd` + `chpasswd` + `usermod -aG <group>` (`chpasswd` rather than
  `passwd`, since `passwd` has no non-interactive stdin mode on this and
  many distros). Requires root; fails immediately and clearly otherwise.

## Building goget itself

```sh
make          # release build
make debug    # ASan+UBSan build, used during development
sudo make install   # installs to /usr/local/bin
```

Plain Make + `gcc`, no CMake — goget only targets Linux, has a flat
single-binary build graph, and `pkg-config` already resolves libcurl, so
CMake's main advantages (portable dependency discovery, multi-platform
generators) don't buy anything here.

### Dependencies

- `libcurl` (via `pkg-config`) — for GitHub/GitLab/Codeberg API calls
- [cJSON](https://github.com/DaveGamble/cJSON) — vendored directly in
  `vendor/` (avoids depending on a distro package for the tool that's
  meant to help you install packages)
- The system `git`, `cmake`, `make`, `tar`/`unzip`, `sha256sum`,
  `objdump`/`readelf`/`ldd`, and (for `makeuser`) `useradd`/`chpasswd`/
  `usermod`/`groupadd` binaries are shelled out to at runtime, not linked
  against.

## Usage

```sh
goget build <repo>    # clone/pull + build from source + install
goget fetch <repo>    # install a compatible prebuilt release binary
goget show <repo>     # page the README + build script
goget config          # print current configuration
goget makeuser        # create a new user account, requires root
```

`<repo>` accepts any of the forms listed above, e.g.:

```sh
goget build github.com/fastfetch-cli/fastfetch
goget build antirez/kilo
goget build git@github.com:owner/repo.git
goget fetch fastfetch          # bare name -> cross-host search
```

## Project layout

```
src/
  main.c        command dispatch, repo-spec resolution (incl. search/config gating)
  repospec.*    repo spec parsing (URL/SSH/host-owner-repo/bare name)
  cache.*       ~/.cache/goget/src/<host>/<owner>/<repo> path management
  gitops.*      clone/pull via the system git binary
  buildsys.*    CMake/Make/Autotools detection + build/install
  release.*     GitHub Releases lookup, asset selection, extraction, install
  compat.*      arch / musl-vs-glibc / glibc-version compatibility checks
  checksum.*    checksum manifest discovery + sha256 verification
  search.*      GitHub/GitLab/Codeberg short-name search
  config.*      goget.conf parser, host allow-list, USE flags
  show.*        README/build-script raw fetch + pager
  makeuser.*    interactive user creation (termios password masking)
  prompt.*      shared stdin Y/n, numbered-picker, and Once/Always/Cancel prompts
  net.*         libcurl GET/download wrapper
  proc.*        fork+execvp helpers (no shell, ever, for repo-derived strings)
  util.*        string/path helpers
vendor/
  cJSON.*       vendored JSON parser
```

## Development notes

Every dynamically-allocated string (parsed specs, paths, subprocess
argument arrays, downloaded content) has clear ownership and is freed by
its owner. Builds are checked under `make debug`
(`-fsanitize=address,undefined`) during development; no leaks or UB found
in goget's own process (child processes it execs, like `cmake`/`make`,
aren't instrumented by this).

`makeuser`'s actual account-creation flow (`useradd`/`chpasswd`/`usermod`)
hasn't been run end-to-end against a real system as part of automated
testing, since that's a real, hard-to-reverse system change — try it in
your own terminal as root to confirm.

## HAIIIII :3 if you wanna suppoirt me please donate here [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/L6N725GI1V)
