#!/usr/bin/env bash

set -euo pipefail

SOURCE_DIR="${SOURCE_DIR:-.}"
BUILD_DIR="${BUILD_DIR:-/tmp/lgx-ci-build}"
VCPKG_DIR="${VCPKG_DIR:-/opt/vcpkg}"
STATIC_BUILD="${STATIC_BUILD:-ON}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"

if [[ "${BUILD_TYPE}" != "Release" ]]; then
  echo "This script is intended for release builds on CI machines. Override BUILD_TYPE only if you really mean it." >&2
fi

SOURCE_DIR="$(cd "${SOURCE_DIR}" && pwd)"
BUILD_DIR="$(mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}" && pwd)"
VCPKG_DIR="$(cd "${VCPKG_DIR}" && pwd)"

if [[ ! -f "${SOURCE_DIR}/CMakeLists.txt" ]]; then
  echo "SOURCE_DIR does not look like the lgx source tree: ${SOURCE_DIR}" >&2
  exit 1
fi

if [[ ! -d "${VCPKG_DIR}" ]]; then
  echo "VCPKG_DIR does not exist: ${VCPKG_DIR}" >&2
  exit 1
fi

if [[ ! -x "${VCPKG_DIR}/vcpkg" ]]; then
  "${VCPKG_DIR}/bootstrap-vcpkg.sh"
fi

if [[ "${STATIC_BUILD}" == "ON" ]]; then
  VCPKG_LIBRARY_LINKAGE="static"
  VCPKG_TRIPLET="x64-linux-static-release"
else
  VCPKG_LIBRARY_LINKAGE="dynamic"
  VCPKG_TRIPLET="x64-linux-release"
fi

VCPKG_OVERLAY_TRIPLETS="${BUILD_DIR}/vcpkg-triplets"
mkdir -p "${VCPKG_OVERLAY_TRIPLETS}"

cat > "${VCPKG_OVERLAY_TRIPLETS}/${VCPKG_TRIPLET}.cmake" <<EOF
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_LIBRARY_LINKAGE ${VCPKG_LIBRARY_LINKAGE})
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
EOF

VCPKG_DEPS=(
  qtbase
  qtdeclarative
  qtwayland
  boost-unordered
)

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

echo "Installing vcpkg dependencies for triplet ${VCPKG_TRIPLET}"
"${VCPKG_DIR}/vcpkg" install \
  --recurse \
  --triplet "${VCPKG_TRIPLET}" \
  --overlay-triplets="${VCPKG_OVERLAY_TRIPLETS}" \
  --clean-buildtrees-after-build \
  --clean-packages-after-build \
  --clean-downloads-after-build \
  "${VCPKG_DEPS[@]}"

echo "Configuring lgx in ${BUILD_DIR}"
cmake \
  -S "${SOURCE_DIR}" \
  -B "${BUILD_DIR}" \
  "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET="${VCPKG_TRIPLET}" \
  -DVCPKG_OVERLAY_TRIPLETS="${VCPKG_OVERLAY_TRIPLETS}" \
  -DLGX_ENABLE_TESTS=OFF

echo "Building lgx"
cmake --build "${BUILD_DIR}" --parallel "${BUILD_JOBS}"

echo "Build complete: ${BUILD_DIR}/bin/lgx"
