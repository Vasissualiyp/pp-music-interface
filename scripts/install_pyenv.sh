#!/usr/bin/env bash
#
# Fallback Python environment for machines WITHOUT the Nix dev shell.
#
# On NixOS, or any machine that can install Nix, use `nix develop` instead:
# this script exists only for cluster login nodes where Nix is unavailable.
# Every path is derived from $INTERFACE_DIR, so it carries no hardcoded
# scratch-filesystem paths.

set -euo pipefail

if [[ -z "${INTERFACE_DIR:-}" ]]; then
	echo "Please set the INTERFACE_DIR environment variable" >&2
	exit 1
fi

command -v gcc >/dev/null || { echo "Please load a gcc module before proceeding" >&2; exit 1; }
command -v python >/dev/null || { echo "Please load a python module before proceeding" >&2; exit 1; }

CLASS_DIR="$INTERFACE_DIR/class_public"
CLASS_INCLUDE_DIR="$CLASS_DIR/include"
CLASS_LIB="$CLASS_DIR/lib"

# Python venv setup
cd "$INTERFACE_DIR"
python -m venv env
# shellcheck disable=SC1091
source ./env/bin/activate
pip install setuptools cython numpy scipy matplotlib pandas camb

# Install CLASS: F90 interface
if [[ ! -d "$CLASS_DIR" ]]; then
	git clone https://github.com/lesgourg/class_public.git "$CLASS_DIR"
fi
cd "$CLASS_DIR"
git submodule update --init --recursive  # Critical for HyRec/Recfast data
make -j4 CLASS_DIR="$CLASS_DIR"

# Set critical environment variables, derived from INTERFACE_DIR
export CLASS_INCLUDE_DIR
export CLASS_LIB
export LD_LIBRARY_PATH="${CLASS_LIB}:${LD_LIBRARY_PATH:-}"

# Install CLASS: python interface
cd "$CLASS_DIR/python"
CFLAGS="-I${CLASS_INCLUDE_DIR} -L${CLASS_LIB}" \
	LDFLAGS="-L${CLASS_LIB}" \
	pip install -e .
