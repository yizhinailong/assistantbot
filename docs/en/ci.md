# Version tags and Linux CI

English · [简体中文](../zh-CN/ci.md)

[Documentation index](README.md) · [Project home](../../README.md)

The repository has three workflows. Their logic is written directly in the YAML, with no extra scripts:

| Workflow | Purpose |
| --- | --- |
| [linux-ci.yml](../../.github/workflows/linux-ci.yml) (Linux CI) | Builds and runs the tests, and checks and builds the admin pages, on branch pushes, PRs, and version tags |
| [version-tag.yml](../../.github/workflows/version-tag.yml) (Create Version Tag) | Creates an annotated tag when the `VERSION` of `project()` changes, then starts Linux CI on that tag |
| [docs.yml](../../.github/workflows/docs.yml) (Documentation) | Generates the Doxygen documentation and publishes it to GitHub Pages; see the [Doxygen guide](doxygen.md) |

Other projects do not depend on the app, so a tag only records the version. There is no Release workflow, and no build artifacts are uploaded. Only Linux CI exists for now.

## Version tags

[version-tag.yml](../../.github/workflows/version-tag.yml) runs on pushes to `master` that change `CMakeLists.txt`.

It reads the `VERSION` of the first `project()` call in `CMakeLists.txt` at the commits before and after the push:

- It strips `#` line comments first, then takes the literal after `VERSION`.
- The version must have the form `major[.minor[.patch[.tweak]]]` and cannot be a variable.
- When a push contains several commits, it compares the two ends of the push.

When the version changes, it creates an annotated tag on the last commit of the push. For example, when the version goes from 0.1.0 to 0.2.0:

- the tag is `v0.2.0`, with the message `assistantbot 0.2.0`;
- the tagger is the GitHub Actions bot.

No tag is created when:

- The version is unchanged, and only dependencies, options, formatting, or comments changed.
- No version can be read from the commit before the push, for example when a branch is first created. That commit then serves only as the baseline.

Existing tags are never overwritten:

- When the version is already tagged on another commit, the workflow fails, and you need to pick an unused version.
- A retry of the same push reuses the existing tag and starts Linux CI on it again.

The workflow commits no files and never bumps the version itself.

Manual runs and concurrency:

- It can be run manually on the default branch. It then compares the selected commit with its first parent; an unchanged version still creates no tag.
- For a multi-commit push, re-run the original workflow run, so the original comparison ends are kept.
- Concurrent attempts for the same commit run one after another; changes to different versions are not merged into one group.

After creating the tag, the workflow runs `gh workflow run linux-ci.yml --ref <tag>` with the built-in token. A push made with `GITHUB_TOKEN` does not start new push workflows, but it can trigger `workflow_dispatch`; see [GitHub's trigger rules](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).

The tag job requests `contents: write` and `actions: write`, and the repository rules must allow it to create version tags. A successful version workflow only means that the tag exists and Linux CI has started. It does not mean the tests passed; check the result of Linux CI on that tag.

## Building and testing on Linux

[linux-ci.yml](../../.github/workflows/linux-ci.yml) is triggered by:

- a push of commits to any branch;
- a PR being opened, updated, or reopened;
- a manual run from the Actions page;
- the version tag workflow, on a new tag.

Run rules:

- Deleting a branch skips the tests.
- A newer run for the same ref cancels an older one that has not finished.
- Each job checks out the commit of the triggering event; for a PR, it verifies the merge commit that GitHub generates.

`debug` and `release` are two independent matrix jobs, so one failing does not stop the other from finishing. They use the `linux-debug` and `linux-release` presets, and run these steps on `ubuntu-24.04`:

1. Download the official CMake 4.2.3 Linux package and verify it against a pinned SHA-256.
2. Install LLVM 21's `clang` and `clang-format` from [apt.llvm.org](https://apt.llvm.org/), then Ninja and ccache from the Ubuntu archive.
   - Before adding the package source, check the fingerprint of the LLVM signing key.
   - Put `/usr/lib/llvm-21/bin` on `PATH`, so the presets' `clang++` resolves to LLVM 21.
3. `debug` job only: run `clang-format --dry-run -Werror` on every tracked `.cpp` and `.hpp` file, with the rules from the root `.clang-format`.
4. Shallow-clone vcpkg at the pinned commit `67972e92` (which provides drogon 1.9.13 port-version 2, the version verified locally), run `bootstrap-vcpkg.sh` and `vcpkg install 'drogon[sqlite3]'` (drogon with SQLite, which the bot's database needs), and set `VCPKG_ROOT`.
   - `actions/cache` saves vcpkg's binary cache directory.
   - The cache key includes the runner image, the vcpkg commit, the GCC version used to build the ports, the CMake version, and the installed features (`drogon-sqlite3`). A change to any of them rebuilds the dependencies.
   - The cache is saved right after a successful install, so a failure in a later step does not lose it.
   - When the dependency install fails, the vcpkg build logs of that run are printed.
5. `cmake --preset`, then `cmake --build --preset`, then `ctest --preset`.
   - During configure, CPM clones the drogon-openai and drogon-onebot release tags pinned in `CMakeLists.txt` from GitHub, so CI builds the same library versions until the pins change.
   - Configure sets `CPM_SOURCE_CACHE` to a directory that `actions/cache` restores and saves, with a key that covers `CMakeLists.txt` and `cmake/CPM.cmake`. It keeps CPM itself and the clones of the pinned tags, so a run with a cache hit clones nothing, and changing a pin changes the key.
   - The test preset prints the test log on failure, and finding no tests also counts as a failure.
   - All tests run offline; see the Tests section of the [bot guide](bot.md).

On a tag, a step after configure checks that the tag matches `CMAKE_PROJECT_VERSION` in `CMakeCache.txt`. CMake parses that value itself, independently of the text parsing in the version tag workflow.

The `frontend` job checks the [admin pages](admin.md) in `frontend/`. It runs beside the matrix, on `ubuntu-24.04` with `frontend/` as the working directory, and needs neither CMake nor vcpkg:

1. Install pnpm with `pnpm/action-setup`, at the version that `packageManager` in `frontend/package.json` names.
2. Install Node.js 24 (`NODE_VERSION`) with `actions/setup-node`, which also caches the pnpm store under a key on `frontend/pnpm-lock.yaml`.
3. `pnpm install --frozen-lockfile`, so a lockfile out of step with `package.json` fails the job.
4. `pnpm check`: the `vue-tsc` type check, `oxlint` and ESLint with no warnings allowed, and `prettier --check` on `src/`.
5. `pnpm build-only`, the Vite build without a second type check.

Only assistantbot has this job; the two plugin libraries have no frontend.

Other conventions:

- The test jobs need only read access to the repository, and `actions/checkout` does not keep the credentials.
- Third-party actions are pinned to commit SHAs, with the matching version in a comment; when updating one, check both the version and the commit.
- When upgrading CMake, update both the version and the checksum.
- Upgrading the vcpkg commit changes the versions of drogon and the other dependencies. Verify locally on the same commit first, then update the drogon version recorded in [AGENTS.md](../../AGENTS.md).

## Local checks

From the repository root:

```sh
actionlint .github/workflows/linux-ci.yml .github/workflows/version-tag.yml .github/workflows/docs.yml
just test
just test release
just frontend-check
just frontend-build
```
