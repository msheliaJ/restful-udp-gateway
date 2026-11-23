#!/usr/bin/env bash
# Runs inside the scanner container with the repository mounted at /usr/src.
# Build -> unit tests -> coverage -> SonarScanner, against the local server.
set -u
cd /usr/src

CROW_VERSION="${CROW_VERSION:-v1.2.0}"
BUILD_DIR=server/build-local

if [ ! -f server/crow_all.h ]; then
    echo "==> Downloading Crow ${CROW_VERSION}"
    curl -fsSL -o server/crow_all.h \
        "https://github.com/CrowCpp/Crow/releases/download/${CROW_VERSION}/crow_all.h" || exit 1
fi

echo "==> Configure and build"
rm -rf "$BUILD_DIR"
cmake -S server -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
      -DGATEWAY_BUILD_TESTS=ON -DGATEWAY_COVERAGE=ON \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON || exit 1
cmake --build "$BUILD_DIR" -j 4 || exit 1

echo "==> Unit tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure
TEST_RESULT=$?

echo "==> Coverage"
gcovr --root . --filter 'server/main\.cpp' --filter 'server/protocol\.h' \
      --sonarqube coverage-local.xml --print-summary "$BUILD_DIR"

echo "==> SonarScanner -> ${SONAR_HOST_URL}"
sonar-scanner -Dproject.settings=tools/sonar-local/sonar-local.properties
SCAN_RESULT=$?

echo "Tests exit code: ${TEST_RESULT}   Scanner exit code: ${SCAN_RESULT}"
exit $SCAN_RESULT
