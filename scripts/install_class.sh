#!/usr/bin/env bash

# Catch exceptions
if [[ "$INTERFACE_DIR" == "" ]]; then
	echo "Please, set INTERFACE_DIR environment variable"
	exit 1
fi
gcc --verstion || { echo "Plese, load gcc module before proceeding" ; exit 1; }

# Python venv setup
python -m venv env
source ./env/bin/activate
pip install setuptool cython numpy scipy matplotlib

# Install CLASS: F90 interface
git clone https://github.com/lesgourg/class_public.git
cd class_public
git submodule update --init --recursive  # Critical for HyRec/Recfast data
make -j4 CLASS_DIR=$INTERFACE_DIR/class_public

# Set critical environment variables
export CLASS_INCLUDE_DIR="/gpfs/fs0/scratch/m/murray/vasissua/PeakPatch/pp-music-interface/class_public/include"
export CLASS_LIB="/gpfs/fs0/scratch/m/murray/vasissua/PeakPatch/pp-music-interface/class_public/lib"
export LD_LIBRARY_PATH="${CLASS_LIB}:${LD_LIBRARY_PATH}"

# Install CLASS: python interface
cd python
# Install with explicit library paths
CFLAGS="-I${CLASS_INCLUDE_DIR} -L${CLASS_LIB}" \
		LDFLAGS="-L${CLASS_LIB}" \
		pip install -e .
