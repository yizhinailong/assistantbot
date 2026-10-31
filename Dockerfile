# syntax=docker/dockerfile:1

FROM node:24-bookworm-slim AS frontend
WORKDIR /src/frontend
RUN npm install --global pnpm@12.15.0
COPY frontend/package.json frontend/pnpm-lock.yaml ./
RUN --mount=type=cache,id=assistantbot-pnpm,target=/pnpm/store \
    pnpm install --frozen-lockfile --store-dir /pnpm/store
COPY frontend/ ./
RUN pnpm build

FROM ubuntu:24.04 AS build
ARG TARGETARCH
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        autoconf automake build-essential ca-certificates ccache curl \
        g++-14 gcc-14 git gnupg libtool ninja-build pkg-config unzip zip \
    && rm -rf /var/lib/apt/lists/*

# Match Linux CI's LLVM signing key and compiler version.
RUN curl --fail --location --retry 3 https://apt.llvm.org/llvm-snapshot.gpg.key \
        --output /tmp/llvm.key \
    && test "$(gpg --show-keys --with-colons /tmp/llvm.key | awk -F: '/^fpr/ { print $10; exit }')" \
        = 6084F3CF814B57C1CF12EFD515CF4D18AF4F7421 \
    && gpg --dearmor --output /usr/share/keyrings/apt.llvm.org.gpg /tmp/llvm.key \
    && printf '%s\n' \
        'deb [signed-by=/usr/share/keyrings/apt.llvm.org.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main' \
        > /etc/apt/sources.list.d/apt.llvm.org.list \
    && apt-get update \
    && apt-get install --yes --no-install-recommends clang-21 \
    && rm -rf /var/lib/apt/lists/* /tmp/llvm.key

# Verify official CMake archives for each supported Linux architecture.
RUN case "$TARGETARCH" in \
        amd64) cmake_arch=x86_64; cmake_sha=5bb505d5e0cca0480a330f7f27ccf52c2b8b5214c5bba97df08899f5ef650c23 ;; \
        arm64) cmake_arch=aarch64; cmake_sha=e529c75f18f27ba27c52b329efe7b1f98dc32ccc0c6d193c7ab343f888962672 ;; \
        *) printf 'Unsupported architecture: %s\n' "$TARGETARCH" >&2; exit 1 ;; \
    esac \
    && curl --fail --location --retry 3 \
        "https://github.com/Kitware/CMake/releases/download/v4.2.3/cmake-4.2.3-linux-${cmake_arch}.tar.gz" \
        --output /tmp/cmake.tar.gz \
    && printf '%s  %s\n' "$cmake_sha" /tmp/cmake.tar.gz | sha256sum --check \
    && mkdir /opt/cmake \
    && tar -xzf /tmp/cmake.tar.gz --strip-components=1 -C /opt/cmake \
    && rm /tmp/cmake.tar.gz

ENV PATH="/opt/cmake/bin:/usr/lib/llvm-21/bin:${PATH}" \
    CC=gcc-14 \
    CXX=g++-14 \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1 \
    VCPKG_DEFAULT_BINARY_CACHE=/var/cache/vcpkg \
    CPM_SOURCE_CACHE=/var/cache/cpm \
    CCACHE_DIR=/var/cache/ccache
ARG VCPKG_COMMIT=67972e92edd181d433927b42d8ab4bfa52ac164a
RUN git init --quiet "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" fetch --quiet --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_COMMIT" \
    && git -C "$VCPKG_ROOT" checkout --quiet FETCH_HEAD \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
ARG BUILD_JOBS=2
RUN --mount=type=cache,id=assistantbot-vcpkg-downloads,target=/opt/vcpkg/downloads,sharing=locked \
    --mount=type=cache,id=assistantbot-vcpkg-${TARGETARCH},target=/var/cache/vcpkg,sharing=locked \
    VCPKG_MAX_CONCURRENCY="$BUILD_JOBS" "$VCPKG_ROOT/vcpkg" install 'drogon[sqlite3]' 'lua[cpp]'

WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake/ ./cmake/
COPY src/ ./src/
RUN --mount=type=cache,id=assistantbot-cpm,target=/var/cache/cpm,sharing=locked \
    --mount=type=cache,id=assistantbot-ccache-${TARGETARCH},target=/var/cache/ccache,sharing=locked \
    cmake --preset linux-release -DASSISTANTBOT_BUILD_TESTS=OFF \
    && cmake --build --preset linux-release --parallel "$BUILD_JOBS"
COPY --from=frontend /src/frontend/dist/ ./frontend/dist/
RUN cmake --install build/linux-release --prefix /out --strip

FROM ubuntu:24.04 AS runtime
RUN apt-get update \
    && apt-get install --yes --no-install-recommends ca-certificates curl libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 assistantbot \
    && useradd --uid 10001 --gid assistantbot --no-create-home --shell /usr/sbin/nologin assistantbot \
    && mkdir -p /app/data \
    && chown assistantbot:assistantbot /app/data
WORKDIR /app
COPY --from=build /out/assistantbot/ ./
COPY config.json LICENSE ./
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt \
    SSL_CERT_DIR=/etc/ssl/certs
USER 10001:10001
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
    CMD ["curl", "--fail", "--silent", "--show-error", "http://127.0.0.1:8080/api/admin/health"]
ENTRYPOINT ["/app/assistantbot"]
