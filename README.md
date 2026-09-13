# goget

A source-first, cross-distro-aware package manager. Given a repo spec,
`goget` clones it and builds from source using whatever build system the
project actually uses, falling back to a compatibility-checked prebuilt
release binary only when source build isn't available or desired.

## Status

Under active, incremental development. Implemented so far:

- **Repo spec parsing** — accepts full URLs (`https://github.com/owner/repo`),
  SSH specs (`git@github.com:owner/repo.git`, `ssh://git@host/owner/repo`),
  bare `host/owner/repo`, bare `owner/repo` (assumes github.com), and bare
  short names like `fastfetch` (host-less; resolving these requires the
  cross-host search described below, not yet implemented).
- **`goget build <repo>`** — clones into `~/.cache/goget/src/<host>/<owner>/<repo>`
  (or pulls if already cached), detects CMake / Make / Autotools via marker
  files, and builds + installs (`sudo cmake --install` / `sudo make install`).
  A build system that has no `install` target, or any step that fails, is
  reported as a loud nonzero-exit failure — never silently treated as success.

Not yet implemented: `goget fetch` (GitHub Releases + compatibility
checking), the GitHub/GitLab/Codeberg short-name disambiguation search,
`goget show`, `goget config` / `goget.conf` / USE flags, and `goget makeuser`.

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
- The system `git`, `cmake`, and `make` binaries are shelled out to at
  runtime (not linked against) — `git` for clone/pull, `cmake`/`make`/
  `./configure` for building whatever target package you point goget at.

## Usage

```sh
goget build <repo>    # clone/pull + build from source + install
goget fetch <repo>    # install a compatible prebuilt release binary (WIP)
goget show <repo>     # page the README + build script (WIP)
goget config          # print current configuration (WIP)
goget makeuser         # create a new user account, requires root (WIP)
```

`<repo>` accepts any of the forms listed above, e.g.:

```sh
goget build github.com/fastfetch-cli/fastfetch
goget build antirez/kilo
goget build git@github.com:owner/repo.git
```

## Project layout

```
src/
  main.c        command dispatch
  repospec.*    repo spec parsing (URL/SSH/host-owner-repo/bare name)
  cache.*       ~/.cache/goget/src/<host>/<owner>/<repo> path management
  gitops.*      clone/pull via the system git binary
  buildsys.*    CMake/Make/Autotools detection + build/install
  proc.*        fork+execvp helper (no shell, ever, for repo-derived strings)
  util.*        string/path helpers
vendor/
  cJSON.*       vendored JSON parser
```

## Development notes

Every dynamically-allocated string (parsed specs, paths, subprocess
argument arrays) has clear ownership and is freed by its owner. Builds are
checked under `make debug` (`-fsanitize=address,undefined`) during
development; no leaks or UB found so far in goget's own process (child
processes it execs, like `cmake`/`make`, aren't instrumented by this).


## HAIIIII :3 if you wanna suppoirt me please donate here [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/L6N725GI1V)
