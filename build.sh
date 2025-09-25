#!/bin/bash

PLATFORM=$(uname -s)

# Possible options:
# -v|--verbose
# -d|--debug
# -r|--release
# -w|--rel-with-debug-info
# -i|--install-dir <INSTALL_DIR>
# -c|--clean
# -r|--rebuild (no reconfigure)
# -g|--reconfigure
# -a|--clang

# set default values
BUILD_TYPE=Debug
OS=`uname -o`
if [ "$OS" = "Msys" ]; then
    INSTALL_DIR=/c/install/commutil
else
    INSTALL_DIR=~/install/commutil
fi
VERBOSE=0
CLEAN=0
REBUILD=0
RE_CONFIG=0
CLANG=0

# parse options
TEMP=$(getopt -o vdrwcrgai: -l verbose,debug,release,rel-with-debug-info,clean,rebuild,reconfigure,clang,install-dir: -- "$@")
eval set -- "$TEMP"

declare -a CONNS=()
while true; do
  case "$1" in
    -v | --verbose ) VERBOSE=1; shift ;;
    -d | --debug ) BUILD_TYPE=Debug; shift ;;
    -r | --release ) BUILD_TYPE=Release; shift ;;
    -w | --rel-with-debug-info ) BUILD_TYPE=RelWithDebInfo; shift ;;
    -i | --install-dir) INSTALL_DIR="$2"; shift 2 ;;
    -c | --clean) CLEAN=1; shift ;;
    -r | --rebuild) REBUILD=1; CLEAN=1; shift ;;
    -g | --reconfigure) RE_CONFIG=1; REBUILD=1; CLEAN=1; shift ;;
    -a | --clang) CLANG=1; shift ;;
    -- ) shift; break ;;
    * ) echo "ERROR: Invalid option $1, aborting"; exit 1; break ;;
  esac
done

# set normal options
echo "[INFO] Build type: $BUILD_TYPE"
echo "[INFO] Install dir: $INSTALL_DIR"
OPTS="-DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}"
if [ $VERBOSE -eq 1 ]; then
    OPTS+=" -DCMAKE_VERBOSE_MAKEFILE=ON"
    VERBOSE_OPT=--verbose
fi
if [ "$CLANG" == "1" ]; then
    export CXX=`which clang++`;
    if [ -z "$CXX" ]; then
        echo "[ERROR] clang not found, aborting"
        exit 1
    fi
fi

# prepare build directory
BUILD_DIR=cmake_build/${PLATFORM}-${BUILD_TYPE}
echo "[INFO] Using build directory: '$BUILD_DIR'"
mkdir -p $BUILD_DIR
pushd $BUILD_DIR > /dev/null

if [ $CLEAN -eq 1 ]; then
    echo "[INFO] Running target clean"
    cmake --build . -j $VERBOSE_OPT --target clean
    if [ $? -ne 0 ]; then
        echo "[ERROR] Clean failed, see errors above, aborting"
        popd > /dev/null
        exit 1
    fi
    if [ $REBUILD -eq 0 ]; then
        popd > /dev/null
        exit 0
    fi
fi

# Run fresh if re-configure is requested
if [ $RE_CONFIG -eq 1 ]; then 
    echo "[INFO] Forcing fresh configuration"
    OPTS="--fresh $OPTS"
fi

# configure phase
echo "[INFO] Configuring project"
echo "[INFO] Executing: cmake $OPTS $* ../../"
cmake $OPTS $* ../../
if [ $? -ne 0 ]; then
    echo "ERROR: Configure phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

# build phase
echo "[INFO] Building project"
cmake --build . -j $VERBOSE_OPT
if [ $? -ne 0 ]; then
    echo "ERROR: Build phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

# install phase
echo "[INFO] Installing"
cmake --install . $VERBOSE_OPT
if [ $? -ne 0 ]; then
    echo "ERROR: Install phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

popd > /dev/null