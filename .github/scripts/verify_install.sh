#!/usr/bin/env bash
# Checks every way of installing cpplua from a finished release build:
# `cmake --install` (everything, and the lib component alone), the packages
# CPack makes, add_subdirectory, and on Linux pkg-config and the .deb files.
# Each install is used the way an outside project would use it
# (tests/install/). CI runs it on Windows (MSYS2 CLANG64) and Ubuntu.
#
#   .github/scripts/verify_install.sh <build dir>
#
# The C++ compiler is $CXX (default clang++); it must use libc++.
set -euo pipefail

build=$(cd "$1" && pwd)
source_dir=$(cd "$(dirname "$0")/../.." && pwd)
cxx=${CXX:-clang++}
work=$build/verify-install
rm -rf "$work"
mkdir -p "$work"

exe=""
if [ "${OS:-}" = Windows_NT ]; then
    exe=.exe
fi

step() { echo "::group::$*"; }
end() { echo "::endgroup::"; }

# Builds tests/install against an install (find_package) or a source tree
# (add_subdirectory), then runs it.
consumer() {
    local name=$1
    shift
    cmake -S "$source_dir/tests/install" -B "$work/consumer-$name" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$cxx" \
        -DCMAKE_CXX_FLAGS=-stdlib=libc++ "$@"
    cmake --build "$work/consumer-$name"
    "$work/consumer-$name/consumer$exe"
}

# Runs an installed interpreter on a few lines of Lua.
run_cli() {
    "$1" -v
    "$1" "$source_dir/examples/hello_world.lua"
    "$1" -e 'assert(_VERSION == "Lua 5.4"); print("cli OK")'
}

step "cmake --install: everything"
cmake --install "$build" --prefix "$work/full"
run_cli "$work/full/bin/cpplua$exe"
consumer full -DCMAKE_PREFIX_PATH="$work/full"
end

step "cmake --install --component lib: the library only"
cmake --install "$build" --prefix "$work/lib" --component lib
if [ -e "$work/lib/bin" ]; then
    echo "the lib component installed bin/" >&2
    exit 1
fi
consumer lib -DCMAKE_PREFIX_PATH="$work/lib"
end

step "add_subdirectory (as FetchContent does it)"
consumer subdirectory -DCPPLUA_SOURCE="$source_dir"
end

step "CPack: the full and the library-only package"
(cd "$build" && cpack && cpack --config CPackLibConfig.cmake)
mkdir -p "$work/unpacked"
for archive in "$build"/cpplua-*.zip "$build"/cpplua-*.tar.gz "$build"/libcpplua-*.zip "$build"/libcpplua-*.tar.gz; do
    [ -e "$archive" ] || continue
    (cd "$work/unpacked" && cmake -E tar xf "$archive")
done
full_pkg=$(find "$work/unpacked" -maxdepth 1 -name 'cpplua-*' -type d)
lib_pkg=$(find "$work/unpacked" -maxdepth 1 -name 'libcpplua-*' -type d)
run_cli "$full_pkg/bin/cpplua$exe"
[ ! -e "$lib_pkg/bin" ]
consumer package -DCMAKE_PREFIX_PATH="$lib_pkg"
end

if [ "$(uname -s)" = Linux ]; then
    step "the binary needs no libc++ at run time"
    ldd "$work/full/bin/cpplua"
    if ldd "$work/full/bin/cpplua" | grep -q 'libc++'; then
        echo "cpplua links libc++ dynamically" >&2
        exit 1
    fi
    end

    step "pkg-config"
    export PKG_CONFIG_PATH="$work/lib/lib/pkgconfig"
    pkg-config --modversion cpplua
    # shellcheck disable=SC2046 # the flags are separate words
    "$cxx" -std=c++2c -stdlib=libc++ -o "$work/consumer-pkg-config" \
        "$source_dir/tests/install/main.cpp" $(pkg-config --cflags --libs cpplua)
    "$work/consumer-pkg-config"
    unset PKG_CONFIG_PATH
    end

    step ".deb: cpplua (everything)"
    sudo apt-get install -y "$build"/cpplua_*.deb
    run_cli /usr/bin/cpplua
    consumer deb-full
    sudo apt-get remove -y cpplua
    end

    step ".deb: libcpplua-dev (the library only)"
    sudo apt-get install -y "$build"/libcpplua-dev_*.deb
    if [ -e /usr/bin/cpplua ]; then
        echo "libcpplua-dev installed the interpreter" >&2
        exit 1
    fi
    consumer deb-lib
    sudo apt-get remove -y libcpplua-dev
    end
fi

echo "every install works"
