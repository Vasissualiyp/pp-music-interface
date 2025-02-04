#!/bin/bash 
#SBATCH -p debug
#SBATCH --nodes=1
#SBATCH --ntasks=2
#SBATCH --ntasks-per-node=2
#SBATCH --cpus-per-task=1
#SBATCH --time=1:00:00
#SBATCH --job-name=pp_mus_z0
#SBATCH --output=output_pp_mus_z0

PP_DIR="/scratch/m/murray/vasissua/PeakPatch/peakpatch"
RUNDIR=$INTERFACE_DIR
INIT_PARAMS_PATH="./param/parameters.ini"
INIT_MAXZ=12.5
INIT_MAXZ=13.03125
this_file="./run_binary_hpkvd_search.sh"
script_logfile="./pp_binary_search.log"

create_TF_modules="gcc python"
run_modules="NiaEnv/2019b intel/2019u4 intelmpi/2019u4 fftw/3.3.8 gsl/2.5 \
    cfitsio/3.450 intelpython2 mkl/2019u4 hdf5"

echo "" > $script_logfile

# Load Niagara modules
module load $run_modules
# Go to SLURM submit directory
cd $SLURM_SUBMIT_DIR

# Need to symlink the src directory to get some of the tables from the correct location
ln -s $PP_DIR/src $RUNDIR/
mkdir bin output logfiles fields

run_hpkvd_from_params_at_z() {
  red="$1"
  params=$(get_params_from_z $1)
  run_hpkvd_from_params $params
}

compile_pp_music_from_params_at_z() {
  red="$1"
  params=$(realpath $(get_params_from_z $1))
  stdout=$(get_stdout_from_params $params)
  stderr=$(get_stderr_from_params $params)
  compile_pp_music_from_params $params $stdout $stderr
}

get_params_from_z() {
  echo "./param/parameters_z${red}.ini"
}

run_music_from_params_at_z() {
  red="$1"
  params=$(get_params_from_z $1)
  ./bin/MUSIC "$params" 2>> $stderr 1>> $stdout
}

get_seed() {
	local params="$1"
    echo "$(grep '^seed' $params | grep -v "]" | grep -v "chi" | awk -F'=' '{print $2}'| sed 's/ //')"
}

get_lname() {
	local params="$1"
    echo "$(grep 'run_name' $params | awk -F'=' '{print $2}'| sed 's/ //')"
}

get_stdout_from_params() {
	local params=$(realpath "$1")
    local lname=$(get_lname $params)
    local seed=$(get_seed $params)
	local stdout=$(realpath logfiles/$lname\_${seed}.stdout)
	echo "$stdout"
}
get_stderr_from_params() {
	local params=$(realpath "$1")
    local lname=$(get_lname $params)
	local seed=$(get_seed $params)
	local stderr=$(realpath logfiles/$lname\_${seed}.stderr)
	echo "$stderr"
}

remove_old_catalogue() {
    local lname="$1"
    local seed="$2"
    local old_catalogue="output/${lname}_nt2_merge.pksc.$seed"
    if [ -f "$old_catalogue" ]; then
        rm -f $old_catalogue
    fi
}

compile_pp_music_from_params() {
	params="$1"
	stdout="$2"
	stderr="$3"
	make clean
	make hpkvd      CONFIG_FILE="$params" 2>> $stderr 1>> $stdout
    make filter_gen CONFIG_FILE="$params" 2>> $stderr 1>> $stdout
    make merge_pkvd CONFIG_FILE="$params" 2>> $stderr 1>> $stdout
    make -j20 MUSIC 2>> $stderr 1>> $stdout
}

run_hpkvd_from_params_file() {
	short_params="$1"
	params=$(realpath "$1")
    lname=$(get_lname $params)
    seed=$(get_seed $params)
	echo "Running from the parameters file: $params"
	stdout=$(get_stdout_from_params $params)
	stderr=$(get_stderr_from_params $params)
	echo "" > $stdout
	echo "" > $stderr
	echo "stdout: $stdout"
	echo "stderr: $stderr"
    
    # Remove existing merged catalogues from output
	remove_old_catalogue "$lname" "$seed"
    
    # Set OpenMP threading
    export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
    
    mpirun ./bin/filter_gen "$short_params" 2>> $stderr 1>> $stdout
    
    # MPI run of hierarchical peak/void finding script hpkvd.f90
    mpirun ./bin/hpkvd 1 $seed "$short_params" 2>> $stderr 1>> $stdout
    mpirun ./bin/hpkvd 0 $seed "$short_params" 2>> $stderr 1>> $stdout
    
    # MPI run of merging & exclusion script merge_pkvd.f90
    mpirun ./bin/merge_pkvd $seed "$short_params" 2>> $stderr 1>> $stdout

}

create_params_and_tfs_at_z() {
	local Z_RUN="$1"
    module load intelpython3
    $PP_DIR/python/params_tools/change_params_to_z.sh "$Z_RUN" "$INIT_PARAMS_PATH"
    local params="./param/parameters_z${Z_RUN}.ini"
	create_TF_tables $params
}

create_TF_tables() {
    local params_basename=$(basename $1)
    module load $create_TF_modules
    source $INTERFACE_DIR/env/bin/activate
    python $INTERFACE_DIR/scripts/create_TFs_from_parameter_file.py $RUNDIR $params_basename
}

run_music_pp_at_z() {
    Z_RUN="$1"
    create_params_and_tfs_at_z "$Z_RUN"
    module load $run_modules
	compile_pp_music_from_params_at_z "$Z_RUN"
    run_music_from_params_at_z "$Z_RUN"
    run_hpkvd_from_params_at_z "$Z_RUN"
}

run_music_pp_at_z 0
