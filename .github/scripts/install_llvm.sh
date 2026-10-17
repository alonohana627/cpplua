#!/bin/sh
# Installs clang and libc++ of LLVM version $1 from apt.llvm.org on Ubuntu,
# plus the extra packages named after it (clang-tidy-23, ...). The .deb files
# are kept in ~/apt-cache, which the workflow caches between runs, so a run
# downloads them only when the cache is cold.
set -eu
version=$1
shift
cache="$HOME/apt-cache"
mkdir -p "$cache/partial"
echo "Dir::Cache::Archives \"$cache\";" | sudo tee /etc/apt/apt.conf.d/99cpplua-cache >/dev/null
wget -qO llvm.sh https://apt.llvm.org/llvm.sh
sudo bash llvm.sh "$version"
sudo apt-get install -y "libc++-$version-dev" "libc++abi-$version-dev" ninja-build pkg-config "$@"
sudo chown -R "$(id -u):$(id -g)" "$cache"
rm -f "$cache/lock"
"clang++-$version" --version
