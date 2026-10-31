# Doxygen: generating the API documentation

English · [简体中文](../zh-CN/doxygen.md)

[Documentation index](README.md) · [Project home](../../README.md)

Doxygen generates the descriptions of classes, functions, parameters, and return values from the source comments, and puts the Markdown guides into the same site.
The project configuration is the `Doxyfile` in the repository root. It was generated from the Doxygen 1.18.0 template and verified with that version.
For installation, see the [official Doxygen installation guide](https://www.doxygen.nl/manual/install.html).

## Generating and viewing

From the repository root:

```sh
just docs
```

It first creates `build/doxygen`, then runs `doxygen Doxyfile`. Doxygen does not create the output path itself, so without just, create the directory by hand:

```sh
mkdir -p build/doxygen
doxygen Doxyfile
```

See the [official guide to generating output](https://www.doxygen.nl/manual/starting.html#step2).

Open `build/doxygen/html/index.html` in a browser.
After changing source comments or Markdown, run `just docs` again to update it.
The output lives under `build/`, which Git ignores; `just clean` without arguments deletes it too.

Older Doxygen versions (for example 1.15) can generate it as well. They report `ignoring unsupported tag` for the configuration options added in 1.18, which you can ignore.

## Publishing to GitHub Pages automatically

The [Documentation workflow](../../.github/workflows/docs.yml) runs on pushes to `master` that change one of the following:

- `src/`, `docs/`;
- `README.md`, `README.zh-CN.md`, `AGENTS.md`, `Doxyfile`;
- the workflow itself.

It can also be run manually from the Actions page; only runs on the `master` branch publish the site.

The build job's steps:

1. On `ubuntu-24.04`, download the official Doxygen 1.18.0 Linux binary package and verify it against the SHA-256 from the [official download page](https://www.doxygen.nl/download.html).
2. Create the output directory and generate the documentation. The run appends `WARN_AS_ERROR = FAIL_ON_WARNINGS`, so any comment warning fails the workflow; see below.
3. Upload `build/doxygen/html/` as the Pages artifact.

A separate deploy job publishes that artifact after the build succeeds. The generated HTML travels as a workflow artifact and is never committed to Git; building the documentation needs neither vcpkg, nor drogon, nor a C++ toolchain.
When upgrading Doxygen, update both the version and the checksum in the workflow.

The first time, configure the GitHub repository as follows:

1. Open **Settings → Pages**.
2. Under **Build and deployment → Source**, select **GitHub Actions**.
3. Push the workflow and the related documentation to `master`, or select `master` in **Actions → Documentation → Run workflow** and run it.
4. Wait for the build and deploy jobs to succeed, then find the site address in **Settings → Pages** or in the workflow's `github-pages` deployment environment.

The repository must allow GitHub Actions to run. The workflow uses the built-in `GITHUB_TOKEN`: the build job has only `contents: read`, and the deploy job declares `pages: write` and `id-token: write`. No PAT or secrets are needed, and the repository's default token permissions can stay read-only.

About the `github-pages` deployment environment:

- if deployment branches are restricted, allow `master`;
- if required reviewers are configured, the deployment waits for approval.

For the requirements on the publishing source, permissions, and environment, see [GitHub's guide to custom Pages workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

## Project configuration

These are the main options that affect how this project's documentation is generated; search the Doxyfile for an option's name to change it.
Relative paths are resolved against the working directory Doxygen runs in, so always run it from the repository root.

| Option | Current value | Effect |
| --- | --- | --- |
| `PROJECT_NAME` | `assistantbot` | Project name shown on the pages |
| `PROJECT_BRIEF` | The project's English summary | Project description at the top of each page |
| `OUTPUT_DIRECTORY` | `build/doxygen` | Where the generated files go |
| `OUTPUT_LANGUAGE` | `English` | English navigation and built-in titles, matching the English home page |
| `INPUT_ENCODING` | `UTF-8` | Character encoding of the sources and Markdown |
| `INPUT` | `src README.md README.zh-CN.md docs AGENTS.md` | Sources, the English and Chinese READMEs, the guides, and the repository conventions |
| `FILE_PATTERNS` | `*.hpp *.cpp *.md` | File extensions to scan |
| `RECURSIVE` | `YES` | Scan the input directories recursively |
| `STRIP_FROM_INC_PATH` | `src` | Class pages show include paths relative to `src/`, such as `controllers/onebot/assistant_bot.hpp`, as the sources write them |
| `EXCLUDE_SYMBOLS` | `ADD_METHOD_TO WS_PATH_ADD` | Keeps drogon's route macro from appearing as an undocumented member function of each controller |
| `USE_MDFILE_AS_MAINPAGE` | `README.md` | The English README is the home page; the Simplified Chinese `README.zh-CN.md` becomes a separate page |
| `SOURCE_BROWSER` | `YES` | Generate browsable source pages |
| `GENERATE_HTML` | `YES` | Generate documentation viewable in a browser |
| `GENERATE_LATEX` | `NO` | Do not generate LaTeX documentation |
| `GENERATE_TREEVIEW` | `YES` | Show the tree navigation |
| `HAVE_DOT` | `NO` | Graphviz is not needed by default |

`OUTPUT_LANGUAGE` sets only the language of the navigation and built-in titles; it does not translate page content. The home page, the source comments, and `docs/en/` are in English; `README.zh-CN.md` and `docs/zh-CN/` are in Simplified Chinese.
To add a guide, write a file with the same name in both `docs/en/` and `docs/zh-CN/`, and add it to both documentation indexes (this directory's is the [documentation index](README.md)).

To see the options that differ from Doxygen's defaults:

```sh
doxygen -x Doxyfile
```

For the meaning of every option, see the [official configuration reference](https://www.doxygen.nl/manual/config.html).

## Scan scope

- `INPUT` explicitly lists the sources (`src/`) and the documentation files. It does not scan build directories, the test programs in `tests/`, or `scripts/`.
- Every type is internal to the app: the HTTP controllers and the bot plugin in `src/controllers/` (namespace `controllers`), the bot's chat workflow in `src/workflow/` (namespace `workflow`), and the text helpers in `src/common/` (namespace `common`).
- Helper functions in anonymous namespaces in `.cpp` files are not documented (`EXTRACT_ANON_NSPACES = NO`), but they can be viewed on the source pages.
- Doxygen parses the headers and comments directly. It needs no build first and does not read the drogon headers in vcpkg. `CLANG_ASSISTED_PARSING` stays `NO`, so the configuration is not tied to a local compiler or dependency paths.
- Generating documentation does not replace compiling and testing.

## Documentation warnings

Keep the following settings, which catch missing or mismatched comments:

```ini
EXTRACT_ALL           = NO
EXTRACT_PRIVATE       = NO
WARNINGS              = YES
WARN_IF_UNDOCUMENTED  = YES
WARN_IF_DOC_ERROR     = YES
WARN_AS_ERROR         = NO
```

The sources currently produce no comment warnings. The Documentation workflow appends `WARN_AS_ERROR = FAIL_ON_WARNINGS` through standard input, so any warning fails it:

```sh
( cat Doxyfile; echo 'WARN_AS_ERROR = FAIL_ON_WARNINGS' ) | doxygen -
```

The Doxyfile itself keeps `NO`, because older Doxygen versions warn about unknown options, and a local run should not fail because of that.

Common sources of warnings:

- A new public type, function, or member has no comment.
- The comment blocks of a declaration and its definition differ. The project conventions repeat the declaration's comment at the definition; when the two differ, Doxygen reports `has multiple @param documentation sections`.
- A Markdown link points at a file that is not in `INPUT`.

`EXTRACT_ALL = YES` would treat undocumented entities as documented and turn off the missing-documentation warnings, so keep `NO` when you want to check that the comments are complete.

When Markdown mentions a comment tag, write it as inline code, for example `@param`, so it is not interpreted as a documentation command.
Links between Markdown pages in the guides become links inside the generated site. Relative links to configuration files, scripts, or workflow files that are not documentation inputs still have to be viewed in the source repository.

## Optional relationship graphs

To draw class relationship graphs with Graphviz, install Graphviz, check that `dot -V` runs, then set:

```ini
HAVE_DOT = YES
```

The default HTML documentation and source browsing do not depend on Graphviz.
