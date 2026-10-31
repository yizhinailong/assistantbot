project_name := "assistantbot"
platform := os()
build_dir := "./build"
publish_dir := "./publish"
exe_suffix := if platform == "windows" { ".exe" } else { "" }

default:
    @cmake --version
    @ninja --version
    @ccache --version
    @just --list

init:
    @vcpkg install 'drogon[sqlite3]' 'lua[cpp]'

configure profile="debug" *args:
    @cmake --preset "{{ platform }}-{{ profile }}" {{ args }}

build profile="debug":
    @[ -f "{{ build_dir }}/{{ platform }}-{{ profile }}/CMakeCache.txt" ] || cmake --preset "{{ platform }}-{{ profile }}"
    @cmake --build --preset "{{ platform }}-{{ profile }}"
    @cmake -E copy_if_different "{{ build_dir }}/{{ platform }}-{{ profile }}/compile_commands.json" "{{ build_dir }}/compile_commands.json"

publish profile="release": (build profile) frontend-build
    @cmake --install "{{ build_dir }}/{{ platform }}-{{ profile }}"

run profile="debug": (build profile)
    @{{ build_dir }}/{{ platform }}-{{ profile }}/{{ project_name }}{{ exe_suffix }}

test profile="debug": (build profile)
    @ctest --preset "{{ platform }}-{{ profile }}"

docs:
    @cmake -E make_directory "{{ build_dir }}/doxygen"
    @doxygen Doxyfile

clean profile="":
    @cmake -E rm -rf {{ if profile == "" { build_dir + " " + publish_dir } else { build_dir + "/" + platform + "-" + profile } }}

frontend-init:
    @pnpm --dir frontend install --frozen-lockfile

frontend-dev:
    @pnpm --dir frontend dev

frontend-build:
    @pnpm --dir frontend build

frontend-check:
    @pnpm --dir frontend check

frontend-fix:
    @pnpm --dir frontend lint
    @pnpm --dir frontend format

docker-build:
    @docker build --tag {{ project_name }}:local .

docker-up:
    @docker compose up --detach --build

docker-down:
    @docker compose down

docker-logs:
    @docker compose logs --follow assistantbot
