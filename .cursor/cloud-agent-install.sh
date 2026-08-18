#!/usr/bin/env bash
# Idempotent Cloud Agent / environment-build install for EQEmu.
# Installs system deps, builds server binaries, seeds peq DB, and preps runtime paths.
set -euo pipefail
cd /workspace

echo "[eqemu-install] Installing system packages..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build ccache \
  libperl-dev uuid-dev libssl-dev libmariadb-dev mariadb-server mariadb-client \
  libcurl4-openssl-dev libsodium-dev \
  autoconf automake autoconf-archive libtool pkg-config unzip wget

echo "[eqemu-install] Forcing GCC as default cc/c++ (Clang lacks usable libstdc++ here)..."
sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc 100 || true
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++ 100 || true
sudo update-alternatives --set cc /usr/bin/gcc
sudo update-alternatives --set c++ /usr/bin/g++

echo "[eqemu-install] Updating git submodules..."
git submodule update --init --recursive

echo "[eqemu-install] Configuring and building (vcpkg + cmake)..."
mkdir -p .vcpkg-binary-cache
export CC=gcc
export CXX=g++
export VCPKG_BINARY_SOURCES="clear;files,/workspace/.vcpkg-binary-cache,readwrite"
cmake -S . -B build -G Ninja \
  -DEQEMU_BUILD_TESTS=ON \
  -DEQEMU_BUILD_LOGIN=ON \
  -DEQEMU_BUILD_LUA=ON \
  -DEQEMU_BUILD_PERL=ON \
  -DEQEMU_BUILD_CLIENT_FILES=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build --parallel

echo "[eqemu-install] Preparing runtime directories and configs..."
mkdir -p build/bin/logs build/bin/shared build/bin/assets \
  .devcontainer/repo .devcontainer/cache/db .devcontainer/base
cp -R -u utils/patches .devcontainer/base/
if [ ! -e build/bin/assets/patches ]; then
  ln -s ../../../.devcontainer/base/patches build/bin/assets/patches
fi

# Localhost-oriented configs (override LAN defaults from base templates).
if [ ! -f build/bin/eqemu_config.json ]; then
  cp .devcontainer/base/eqemu_config.json build/bin/eqemu_config.json
  sed -i \
    -e 's/"address": ".*"/"address": "127.0.0.1"/' \
    -e 's/"localaddress": ".*"/"localaddress": "127.0.0.1"/' \
    -e 's/"host": "login.projecteq.net"/"host": "127.0.0.1"/' \
    -e 's/"key": ".*"/"key": "eqemu-cloud-devbox-key"/' \
    -e 's/"longname": ".*"/"longname": "Cloud Devbox"/' \
    build/bin/eqemu_config.json
fi
if [ ! -f build/bin/login.json ]; then
  cp .devcontainer/base/login.json build/bin/login.json
fi

echo "[eqemu-install] Cloning quests (required for zone CheckHandin plugins)..."
if [ ! -d .devcontainer/repo/quests ]; then
  git clone --depth 1 https://github.com/ProjectEQ/projecteqquests.git .devcontainer/repo/quests
fi
for link in quests plugins lua_modules mods; do
  target="../../.devcontainer/repo/quests"
  case "$link" in
    plugins|lua_modules|mods) target="../../.devcontainer/repo/quests/$link" ;;
  esac
  if [ ! -e "build/bin/$link" ]; then
    ln -s "$target" "build/bin/$link"
  fi
done

echo "[eqemu-install] Ensuring MariaDB is up and peq DB is seeded..."
sudo service mariadb start
# Wait briefly for socket readiness.
for _ in 1 2 3 4 5 6 7 8 9 10; do
  if sudo mariadb -e "SELECT 1" >/dev/null 2>&1; then break; fi
  sleep 1
done
sudo mariadb -e "CREATE DATABASE IF NOT EXISTS peq;"
sudo mariadb -e "CREATE USER IF NOT EXISTS 'peq'@'127.0.0.1' IDENTIFIED BY 'peqpass';"
sudo mariadb -e "CREATE USER IF NOT EXISTS 'peq'@'localhost' IDENTIFIED BY 'peqpass';"
sudo mariadb -e "GRANT ALL PRIVILEGES ON *.* TO 'peq'@'127.0.0.1';"
sudo mariadb -e "GRANT ALL PRIVILEGES ON *.* TO 'peq'@'localhost';"
sudo mariadb -e "FLUSH PRIVILEGES;"

TABLE_COUNT="$(sudo mariadb -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='peq'" || echo 0)"
if [ "${TABLE_COUNT}" -lt 10 ]; then
  echo "[eqemu-install] Seeding peq from db.eqemu.dev/latest (tables=${TABLE_COUNT})..."
  if [ ! -f .devcontainer/cache/db/db.sql.zip ]; then
    wget -O .devcontainer/cache/db/db.sql.zip https://db.eqemu.dev/latest
  fi
  if [ ! -d .devcontainer/cache/db/peq-dump ]; then
    (cd .devcontainer/cache/db && unzip -o db.sql.zip)
  fi
  (
    cd .devcontainer/cache/db/peq-dump
    for f in create_tables_content create_tables_login create_tables_player create_tables_state create_tables_system; do
      echo "[eqemu-install] Sourcing ${f}.sql..."
      sudo mariadb --database peq -e "source ${f}.sql"
    done
  )
else
  echo "[eqemu-install] peq already seeded (tables=${TABLE_COUNT}), skipping dump import."
fi

echo "[eqemu-install] Generating shared_memory content files..."
(cd build/bin && ./shared_memory)

echo "[eqemu-install] Done."
