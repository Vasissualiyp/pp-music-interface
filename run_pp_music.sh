#!/bin/bash 
##SBATCH -p debug
#SBATCH --account=rrg-rbond-ac
#SBATCH --nodes=10
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --time=12:00:00
#SBATCH --job-name=pp_mus_z17
#SBATCH --output=output_pp_mus_z17_lvl11
# General flags to enable/disable certain script behaviors
CREATE_Z_PARAMS=1
CREATE_TF=0
RUN_PP=0
RUN_MUSIC=1
COMPILE=1
CREATE_ZOOMIN_ICS=0
RUN_MUSIC_ZOOMIN=0

PP_DIR="/scratch/m/murray/vasissua/PeakPatch/peakpatch"
RUNDIR=$INTERFACE_DIR
INIT_PARAMS_PATH="./param/parameters.ini"

create_TF_modules="gcc python"

# MUSIC Intel modules
#run_modules="NiaEnv/2019b intel/2019u4 intelmpi/2019u4 fftw/3.3.8 gsl/2.5 \
#    cfitsio/4.4.0 python/3.6.8 mkl/2019u4 hdf5/1.8.21"

# MUSIC gcc modules
run_modules="gcc openmpi fftw/3.3.8 gsl/2.5 \
    cfitsio/4.4.0 python/3.6.8 mkl/2019u4 hdf5/1.8.21"

# Go to SLURM submit directory
#cd $SLURM_SUBMIT_DIR
NUM_TASKS="$SLURM_JOB_NUM_NODES"
# Set OpenMP threading
#export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
export OMP_NUM_THREADS=1
#NUM_TASKS=2

# Need to symlink the src directory to get some of the tables from the correct location
ln -s $PP_DIR/src $RUNDIR/
mkdir bin output logfiles fields
source $PP_DIR/scripts/vasiliis_scripts/movestuff.sh


######################################
########### CREATE DATASETS ##########
######################################

create_params_at_z() {
	local Z_RUN="$1"
    module load python
	echo "Creating z=$Z_RUN parameter file from ${INIT_PARAMS_PATH}..."
    $PP_DIR/python/params_tools/change_params_to_z.sh "$Z_RUN" "$INIT_PARAMS_PATH"
}
create_tfs_at_z() {
	local Z_RUN="$1"
	local tf_red="$2"
    module load python
    local params=$(get_params_from_z $Z_RUN)
	create_TF_tables $params $tf_red
}

create_TF_tables() {
    local params_basename=$(basename $1)
	tf_red=$2
    module load $create_TF_modules
    source $INTERFACE_DIR/env/bin/activate
    python $INTERFACE_DIR/scripts/create_TFs_from_parameter_file.py $RUNDIR $params_basename $tf_red
}

setup_output_files_from_z() {
	local Z_RUN="$1"
	echo "Z_RUN in setup_output_files_from_z: $Z_RUN"
    local params=$(get_params_from_z "$Z_RUN")
	echo "PARAMS: $params"
	stdout=$(get_stdout_from_params $params)
	stderr=$(get_stderr_from_params $params)
	echo "STDOUT: $stdout"
	echo "STDERR: $stderr"
	rm $stdout
	rm $stderr
	echo "" > $stdout
	echo "" > $stderr
}

####################################
########### GET VARIABLES ##########
####################################

get_params_from_z() {
  red="$1"
  if [[ $HYDRO -eq 1 ]]; then
    hydro_str="_hydro"
  else
    hydro_str=""
  fi
  echo "./param/parameters_z${red}${hydro_str}.ini"
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
	local stdout=$(realpath ./logfiles/$lname\_${seed}.stdout)
	echo "$stdout"
}
get_stderr_from_params() {
	local params=$(realpath "$1")
    local lname=$(get_lname $params)
	local seed=$(get_seed $params)
	local stderr=$(realpath ./logfiles/$lname\_${seed}.stderr)
	echo "$stderr"
}

#######################################
########### COMPILE PROGRAMS ##########
#######################################

compile_pp_music_from_params_at_z() {
  local red="$1"
  local params=$(realpath $(get_params_from_z $1))
  local stdout=$(get_stdout_from_params $params)
  local stderr=$(get_stderr_from_params $params)
  compile_pp_music_from_params $params $stdout $stderr
}

compile_pp_music_from_params() {
	params="$1"
	stdout="$2"
	stderr="$3"
    module load $run_modules
	make clean
	make hpkvd      CONFIG_FILE="$params" 2>> $stderr 1>> $stdout
    make filter_gen CONFIG_FILE="$params" 2>> $stderr 1>> $stdout
    make merge_pkvd CONFIG_FILE="$params" 2>> $stderr 1>> $stdout
	if [[ "$RUN_MUSIC" == "1" ]]; then
        make -j20 MUSIC 2>> $stderr 1>> $stdout
	fi
}

###################################
########### RUN PROGRAMS ##########
###################################

remove_old_catalogue() {
    local lname="$1"
    local seed="$2"
    local old_catalogue="output/${lname}_nt2_merge.pksc.$seed"
    if [ -f "$old_catalogue" ]; then
        rm -f $old_catalogue
    fi
}

run_hpkvd_from_params_at_z() {
  red="$1"
  local params=$(get_params_from_z $1)
  run_hpkvd_from_params_file $params
}

replace_parameter() {
  params_file="$1"
  param_name="$2"
  param_new_value="$3"

  sed -i "s|^$param_name=.*|$param_name=$param_new_value|" $params_file
  #sed -i -rz "s|^$param_name=.*$|$param_name=$param_new_value|mg; T; s|^#$param_name=.*$|$param_name=$param_new_value|m" $params_file
}

create_zoomin_ics_params() {
  red="$1"
  levelmax="$2"
  zstart="$3"
  zoomin_out_fname="$4"
  export HYDRO=0
  (
    echo "Moving the files for redshift $red into ./z$red ..."
    move_single_red $red "./"
    cd "./z$red"
    module load python
    echo "We're currently in: $(pwd)"
    python $PP_DIR/scripts/vasiliis_scripts/make_merge_csv.py
    python $PP_DIR/scripts/vasiliis_scripts/get_zoomin_params.py > zoomin_params.tmp
  )
  center_string=$(head -n 1 "./z$red/zoomin_params.tmp")
  extent_string=$(tail -n 1 "./z$red/zoomin_params.tmp")
  local params_nohydro=$(get_params_from_z $1)
  export HYDRO=1
  local params_hydro=$(get_params_from_z $1)
  cp $params_nohydro $params_hydro
  replace_parameter "$params_hydro" "calculate_velocities" "yes"
  replace_parameter "$params_hydro" "calculate_displacements" "yes"
  replace_parameter "$params_hydro" "calculate_potential" "yes"
  replace_parameter "$params_hydro" "baryons" "yes"
  replace_parameter "$params_hydro" "zstart" "$zstart"
  replace_parameter "$params_hydro" "format" "gadget2"
  replace_parameter "$params_hydro" "levelmax" "$levelmax"
  replace_parameter "$params_hydro" "filename" "$zoomin_out_fname"
  replace_parameter "$params_hydro" "ref_center" "$center_string"
  replace_parameter "$params_hydro" "ref_extent" "$extent_string"

  sed -i "s/.*ref_center.*/$center_string/" "$params_hydro"
  sed -i "s/.*ref_extent.*/$extent_string/" "$params_hydro"
  export HYDRO=0
}

run_music_from_params_at_z() {
  red="$1"
  params=$(get_params_from_z $1)
  stdout=$(get_stdout_from_params $params)
  stderr=$(get_stderr_from_params $params)
  module load $run_modules
  mpirun -np $NUM_TASKS ./bin/MUSIC "$params" 2>> $stderr 1>> $stdout
}

run_hpkvd_from_params_file() {
	params=$(realpath "$1")
    lname=$(get_lname $params)
    seed=$(get_seed $params)
	echo "Running from the parameters file: $params"
	stdout=$(get_stdout_from_params $params)
	stderr=$(get_stderr_from_params $params)
	echo "stdout: $stdout"
	echo "stderr: $stderr"
    module load $run_modules
    
    # Remove existing merged catalogues from output
	remove_old_catalogue "$lname" "$seed"
    
    mpirun ./bin/filter_gen "$params" 2>> $stderr 1>> $stdout
    
    # MPI run of hierarchical peak/void finding script hpkvd.f90
	echo "Running hpkvd from parameter file: $params"
    mpirun ./bin/hpkvd 1 $seed "$params" 2>> $stderr 1>> $stdout
    mpirun ./bin/hpkvd 0 $seed "$params" 2>> $stderr 1>> $stdout
    
    # MPI run of merging & exclusion script merge_pkvd.f90
	echo "Running merge_pkvd from parameter file: $params"
    mpirun ./bin/merge_pkvd $seed "$params" 2>> $stderr 1>> $stdout
}

run_music_pp_at_z() {
    Z_RUN="$1"
	levelmax=12
	zstart=99
	zoomin_out_fname="./IC_zoomin_pp.dat"
	HYDRO=0
	echo "Z_RUN in run_music_pp: $Z_RUN"
	setup_output_files_from_z "$Z_RUN"
	if [[ "$CREATE_Z_PARAMS" == "1" ]]; then
        create_params_at_z "$Z_RUN"
	fi
	if [[ "$CREATE_TF" == "1" ]]; then
        create_tfs_at_z "$Z_RUN" "0"
	fi
	if [[ "$COMPILE" == "1" ]]; then
	  compile_pp_music_from_params_at_z "$Z_RUN"
	fi
	if [[ "$RUN_MUSIC" == "1" ]]; then
        run_music_from_params_at_z "$Z_RUN"
	fi
	if [[ "$RUN_PP" == "1" ]]; then
        run_hpkvd_from_params_at_z "$Z_RUN"
	fi
	if [[ "$CREATE_ZOOMIN_ICS" == "1" ]]; then
        create_zoomin_ics_params "$Z_RUN" "$levelmax" "$zstart" "$zoomin_out_fname"
	    HYDRO=1
	    if [[ "$RUN_MUSIC_ZOOMIN" == "1" ]]; then
            create_tfs_at_z "$Z_RUN" "$zstart"
            run_music_from_params_at_z "$Z_RUN"
	    fi
	fi
}

#run_music_pp_at_z 0
#run_music_pp_at_z 5
#run_music_pp_at_z 11
#run_music_pp_at_z 13
run_music_pp_at_z 17
