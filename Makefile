# ---------------------------------------------------------------------
# To change the system for which you run PeakPatch, uncomment a 
# corresponding line in Makefile.systype.
# You can also add the compilers and flags for a new machine here.
# This multi-machine approach was shamelessly taken from GIZMO by 
# Vasilii  Pustovoit, April 2024
#----------------------------------------------------------------------

#┌──────────────────────────────────────────────────────┐
#│     ____             __               __       __    │
#│    / __ \___  ____ _/ /______  ____ _/ /______/ /_   │
#│   / /_/ / _ \/ __ `/ //_/ __ \/ __ `/ __/ ___/ __ \  │
#│  / ____/  __/ /_/ / ,< / /_/ / /_/ / /_/ /__/ / / /  │
#│ /_/    \___/\__,_/_/|_/ .___/\__,_/\__/\___/_/ /_/   │
#│                      /_/                             │
#└――――――――――――――――――――――――――――――――――――――――――――――――――――――┘

## read the systype information to use the blocks below for different machines
ifdef SYSTYPE
SYSTYPE := "$(SYSTYPE)"
-include Makefile.systype
else
include Makefile.systype
endif

ifeq ($(wildcard Makefile.systype), Makefile.systype)
INCL = Makefile.systype
else
INCL =
endif
FINCL =

CURRENT_DIR := $(shell pwd)
# Need to add src/hpkvd into library path, so that we can use hpkvd as a module
export LD_LIBRARY_PATH := $(CURRENT_DIR)/hpkvd:$(LD_LIBRARY_PATH)
# Parent of current directory is the run directory
RUNDIR := $(shell dirname $(shell pwd))

#--------------------------------------------------------------------------
# OPTIONS FOR RUNNING ON SCINET-NIAGARA WITH INTEL COMPILERS (RECOMMENDED)
#--------------------------------------------------------------------------

ifeq ($(SYSTYPE), "niagara")
F90 = mpif90
F77 = mpif77
#OPTIMIZE =  -O0 -w -mcmodel=large -shared-intel
OPTIMIZE =  -O4 -w -mcmodel=large -shared-intel
#OPTIMIZE += -Wall -g -traceback # Enable debugging
FFTWOMP = -lfftw3f_omp
FFTWFLAGS = -lfftw3f_mpi -lfftw3f
MODFLAG = -module 
OMPLIB = -fopenmp

FFTW_PATH = $(SCINET_FFTW_MPI_ROOT)

#CC = mpicc
#CXX = mpiCC
CCOPTIMIZE = -w -fno-exceptions
endif

#----------------------------------------------------------------------
# OPTIONS FOR RUNNING ON SCINET-NIAGARA WITH GCC COMPILERS (UNTESTED)
#----------------------------------------------------------------------

ifeq ($(SYSTYPE),"niagara-gcc")

F90 = mpifort -DOMPI_SKIP_MPICXX -fallow-argument-mismatch
F77 = mpifort -DOMPI_SKIP_MPICXX -fallow-argument-mismatch
OPTIMIZE = -O3 -Wall -mcmodel=large -fno-common -Wno-deprecated 
#OPTIMIZE += -Wall -g # Enable debugging
FFTWFLAGS = -lstdc++ -lfftw3f_mpi -lfftw3f 
FFTWOMP = -lfftw3f_omp 
MODFLAG = -J
OMPLIB = -fopenmp  # Enable OpenMP

FFTW_PATHFFTW_PATH = $(SCINET_FFTW_MPI_ROOT)

CC =  mpicc
CXX = mpiCC
CCOPTIMIZE = -Wall -DOMPI_SKIP_MPICXX
#LDFLAGS = -L$(MPI_PATH)/lib -Wl,-rpath,$(MPI_PATH)/lib -lmpi #-ldl -lm 
LDFLAGS = -lmpi -ldl -lm 

endif

#----------------------------------------------------------------------
# OPTIONS FOR RUNNING ON NIXOS OR WITH NIX PACKAGE MANAGER
#----------------------------------------------------------------------

ifeq ($(SYSTYPE),"nix")

F90 = mpifort -DOMPI_SKIP_MPICXX -fallow-argument-mismatch
F77 = mpifort -DOMPI_SKIP_MPICXX -fallow-argument-mismatch
OPTIMIZE = -O3 -mcmodel=large -fno-common #-Wno-deprecated -Wextra
FFTWFLAGS = -lstdc++ -lfftw3f_mpi -lfftw3f 
FFTWOMP = -lfftw3f_omp 
FFTW_PATH = $(FFTW_SINGLE_PATH)
MODFLAG = -J
OMPLIB = -fopenmp

CC =  mpicc
CXX = mpiCC
CCOPTIMIZE = -DOMPI_SKIP_MPICXX
LDFLAGS = -L$(MPI_PATH) -lmpifort -lmpi -ldl -lm -L$(GFORT_LPATH) -lgfortran

DEBUG = #-Wall -g # Enable debugging
OPTIMIZE += $(DEBUG) 
CCOPTIMIZE += $(DEBUG) 

endif

# Notes [Vasilii Pustovoit]
# This module was written in April 2024. This is the most general way to run 
# PeakPatch on any machine, as long as you can install nix package manager.
# See flake.nix on how to run. Tested successfully with MPI on
# Linux X86 architecture, MacOS or ARM havn't been tested yet.

#----------------------------------------------------------------------
# OPTIONS FOR RUNNING ON CITA STARQ MACHINES (UNTESTED)
#----------------------------------------------------------------------

ifeq ($(SYSTYPE),"starq")
F90 = mpifort -DOMPI_SKIP_MPICXX -fallow-argument-mismatch
F77 = mpifort -DOMPI_SKIP_MPICXX -fallow-argument-mismatch
OPTIMIZE = -O3 -ffast-math -march=znver3 -mtune=znver3 -funroll-loops \
		   -finline-functions -march=native -flto -fno-fat-lto-objects  # starq flags
OPTIMIZE += -Wextra -mcmodel=large -fno-common -Wno-deprecated		  # PeakPatch flags
#OPTIMIZE += -Wall -g # Enable debugging
FFTWFLAGS = -lstdc++ -lfftw3f_mpi -lfftw3f 
FFTWOMP = -lfftw3f_omp 
MODFLAG = -J
OMPLIB = -fopenmp

FFTW_PATH = /cita/modules/fftw/3.3.10-openmpi-ucx

CC =  mpicc
CXX = mpiCC
CCOPTIMIZE = -Wall -DOMPI_SKIP_MPICXX
LDFLAGS = -L$(MPI_PATH) -lmpif -lmpi -ldl -lm 
endif

# Notes [Vasilii Pustovoit]
# This module was written in June 2024 only for starq CITA queue. 
#
# Modules to be loaded are:
# module load openmpi/4.1.6-gcc-ucx fftw/3.3.10-openmpi-ucx gsl/2.7.1 cfitsio/4.0.0 python/3.10.2
#
# Neccessary edits: your job submission script should include the compilation of PeakPatch 
# (the code should NOT be compiled on kingcrab/lobster/ricky etc.)

#----------------------------------------------------------------------
# OPTIONS FOR RUNNING ON CITA NON-STARQ MACHINES
#----------------------------------------------------------------------

ifeq ($(SYSTYPE),"cita")
F90 = mpifort -DOMPI_SKIP_MPICXX -fallow-argument-mismatch
F77 = mpifort -DOMPI_SKIP_MPICXX -fallow-argument-mismatch
OPTIMIZE = -O3 -Wextra -mcmodel=large -fno-common -Wno-deprecated
#OPTIMIZE += -Wall -g # Enable debugging
FFTWFLAGS = -lstdc++ -lfftw3f_mpi -lfftw3f 
FFTWOMP = -lfftw3f_omp 
MODFLAG = -J
OMPLIB = -fopenmp

FFTW_PATH = /cita/modules/fftw/3.3.10-openmpi

CC =  mpicc
CXX = mpiCC
CCOPTIMIZE = -Wall -DOMPI_SKIP_MPICXX
LDFLAGS = -L$(MPI_PATH) -lmpif -lmpi -ldl -lm 
endif

# Notes [Vasilii Pustovoit]
# This module was written in June 2024 for all CITA systems. Modules to be loaded are:
#
# module load openmpi/4.1.6-gcc-node fftw/3.3.10-openmpi gsl/2.7.1 cfitsio/4.0.0 python/3.10.2
#
# The code should work well on both interactive (kingcrab, calamari, ...) and Sunnyvale
# (workq, greenq, ...) clusters. Tested on kingcrab.

#----------------------------------------------------------------------
# OPTIONS FOR RUNNING ON SCINET-GPC MACHINES (LEGACY)
#----------------------------------------------------------------------

ifeq ($(SYSTYPE),"scinet-gpc")
F90 = mpif90
F77 = mpif77
OPTIMIZE =  -O4 -w -mcmodel=large -shared-intel 
#OPTIMIZE += -Wall -g # Enable debugging
FFTWFLAGS = -lfftw3f_mpi -lfftw3f
FFTWOMP = -lfftw3f_omp
MODFLAG = -module 
OMPLIB = -openmp

CC = mpicc
CXX = mpiCC
CCOPTIMIZE = -w 
endif

#----------------------------------------------------------------------
# OPTIONS FOR RUNNING ON DARWIN (LEGACY)
#----------------------------------------------------------------------

ifeq ($(SYSTYPE), "darwin")
F90 = mpif90 -lstdc++ -lmpi_cxx -DDARWIN
F77 = mpif77
OPTIMIZE =  -O4 -w #
#OPTIMIZE += -Wall -g # Enable debugging
FFTWOMP = -lfftw3f_omp
MODFLAG = -J
FFTW_PATH = /usr/local
FFTWFLAGS = -lfftw3f_mpi -lfftw3f
OMPLIB = -lgomp

CC = mpicc 
CXX = mpic++ -DDARWIN
CCOPTIMIZE = -w
endif

#----------------------------------------------------------------------
#----------------------------------------------------------------------
# DO NOT MODIFY BELOW UNLESS YOU KNOW WHAT YOU'RE DOING
#----------------------------------------------------------------------
#----------------------------------------------------------------------

ppsrcdir=$(PP_DIR)/src

# GET PREPROCESSOR VALUES FROM CONFIG FILE
CONFIG_FILE ?= ./param/parameters.ini
get_nmesh = $(shell sed -n 's/^nmesh\s*=\s*\([0-9]\+\).*/\1/p' $(1))

# Needed for linking of hpkvd module
CCOPTIMIZE += -fPIC
OPTIMIZE += -fPIC

FFTLIB = -L$(FFTW_PATH)/lib $(FFTWFLAGS) $(FFTWOMP) 
FFTINC = -I$(FFTW_PATH)/include 

CCOPTIONS = $(CDEFS) $(CCOPTIMIZE)

moddir = $(ppsrcdir)/modules
bindir = $(ppsrcdir)/../bin

# EXTERNAL MODULES
exdir = $(moddir)/External
hpx_mods = \
		$(exdir)/healpix_types.o\
		$(exdir)/cgetEnvironment.o\
		$(exdir)/extension.o\
		$(exdir)/long_intrinsic.o\
		$(exdir)/misc_utils.o\
		$(exdir)/num_rec.o\
		$(exdir)/bit_manipulation.o\
		$(exdir)/indmed.o\
		$(exdir)/statistics.o\
		$(exdir)/pix_tools.o\
		$(exdir)/fitstools.o\
		$(exdir)/head_fits.o 

ex_mods  = $(exdir)/intreal_types.o\
	 $(exdir)/openmpvars.o\
	 $(exdir)/mpivars.o\
	 $(exdir)/textlib.o\
	 $(exdir)/Type_Kinds.o\
	 $(exdir)/Endian_Utility.o\
	 $(exdir)/timing_diagnostics.o\
	 $(exdir)/myio.o\
	 $(exdir)/memorytracking.o\
	 $(exdir)/memory_management.o

# GLOBAL VARIABLES
gvdir   = $(moddir)/GlobalVariables
gv_mods = \
	$(gvdir)/cosmoparams.o\
	$(gvdir)/input_parameters.o\
	$(gvdir)/params.o

# READING PARAMETERS FROM INI FILE
inidir = $(moddir)/ini_reader
ini_mods = $(inidir)/config_reader.o
ini_inputs = $(gv_mods) $(exdir)/mpivars.o $(hpdir)/arrays.o $(ti_mods) 


# HOMOGENEOUS ELLIPSOID 
hedir   = $(moddir)/HomogeneousEllipsoid
he_mods = $(hedir)/HomogeneousEllipsoid.o

# SOLVERS 
sldir   = $(moddir)/Solvers
sl_mods = $(sldir)/Solvers.o

# RANDOM FIELD
rfdir   = $(moddir)/RandomField
rf_mods  = \
	$(rfdir)/globalvars.o\
	$(rfdir)/grid.o\
	$(rfdir)/fftw_interface.o\
	$(rfdir)/tiles.o\
	$(rfdir)/cosmology.o\
	$(rfdir)/random.o\
	$(rfdir)/pktable.o\
	$(rfdir)/gaussian_field.o\
	$(rfdir)/time.o\
	$(rfdir)/collapse.o\
	$(rfdir)/growth.o\
	$(rfdir)/chi2zeta.o\
	$(rfdir)/RandomField.o

# SLAB TO CUBE FILES
scdir = $(moddir)/SlabToCube
sc_mods = $(scdir)/SlabToCube.o

# TABLE INTERPOLATION FILES
tidir = $(moddir)/TabInterp
ti_mods = $(tidir)/TabInterp.o

# LINEAR COSMOLOGY DIRECTORY
cosmodir = $(ppsrcdir)/cosmology
cosmo_mods = \
	$(cosmodir)/Dlin_params.o \

# HPKVD MODULE AND OBJECT FILES
hpdir  = $(ppsrcdir)/hpkvd
hpdir_full  = $(hpdir)
hpkvd_mods = \
	$(hpdir)/arrays.o \
	$(hpdir)/io.o \
	$(hpdir)/hpkvdmodule.o \
	$(hpdir)/hpkvd_c_wrapper.o
hpkvd_objs = \
	$(hpdir)/peakvoidsubs.o \
	$(cosmodir)/psubs_Dlinear.o
hpkvd_main = $(hpdir)/hpkvd.o

etab_objs = $(hpdir)/run_hom_ellipse_tab.o 	 

# MERGE_PKVD MODULE AND OBJECT FILES
mgdir  = $(ppsrcdir)/merge_pkvd
mgdir_full  = $(shell pwd)/merge_pkvd
merge_mods = \
	$(mgdir)/sort2.o\
	$(mgdir)/arrays_params.o\
	$(mgdir)/exclusion.o\
	$(mgdir)/merge_pkvd_module.o
mg_main=$(mgdir)/merge_pkvd.o

merge_objs = \
	$(cosmodir)/psubs_Dlinear.o\

# PKS2MAP MODULE AND OBJECT FILES
pmdir  = $(ppsrcdir)/pks2map/
p2m_mods = \
	 $(pmdir)/profiles.o\
	 $(exdir)/mpivars.o\
	 $(exdir)/textlib.o\
	 $(rfdir)/random.o\
	 $(pmdir)/healpixvars.o\
	 $(pmdir)/flatskyvars.o\
	 $(pmdir)/fitsvars.o\
	 $(pmdir)/cosmology.o\
	 $(pmdir)/bbps_profile.o\
	 $(pmdir)/line_profile.o\
	 $(pmdir)/integrate_profiles.o\
	 $(pmdir)/maptable.o\
	 $(pmdir)/haloproject.o\
	 $(pmdir)/pksc.o

p2m_objs = $(pmdir)/pks2map.o 	 

# PKS2CMB MODULE AND OBJECT FILES
pcdir  = $(ppsrcdir)/pks2cmb/
p2c_mods = \
	 $(pcdir)/profiles.o\
	 $(exdir)/mpivars.o\
	 $(exdir)/textlib.o\
	 $(rfdir)/random.o\
	 $(pcdir)/healpixvars.o\
	 $(pcdir)/flatskyvars.o\
	 $(pcdir)/fitsvars.o\
	 $(pcdir)/cosmology.o\
	 $(pcdir)/bbps_profile.o\
	 $(pcdir)/line_profile.o\
	 $(pcdir)/integrate_profiles.o\
	 $(pcdir)/maptable.o\
	 $(pcdir)/haloproject.o\
	 $(pcdir)/pksc.o

p2c_objs = $(pcdir)/pks2cmb.o 	 

mmtm_mods = \
	 $(exdir)/textlib.o\
	 $(pmdir)/cosmology.o\
	 $(pmdir)/profiles.o\
	 $(pmdir)/bbps_profile.o\
	 $(pmdir)/integrate_profiles.o\
	 $(pmdir)/maptable.o

mmtm_objs = $(pmdir)/make_maptable.o

mmtc_mods = \
	 $(exdir)/textlib.o\
	 $(pcdir)/cosmology.o\
	 $(pcdir)/profiles.o\
	 $(pcdir)/bbps_profile.o\
	 $(pcdir)/integrate_profiles.o\
	 $(pcdir)/maptable.o

mmtc_objs = $(pcdir)/make_maptable.o

# PREPROCESSOR ACTIONS: set n1, n2, n3 before compilation
$(hpdir)/arrays_tmp.f90: get-config $(hpdir)/arrays.f90
	sed 's/N_REPLACE/$(N_REPLACE)/g' $(hpdir)/arrays.f90 > $@
$(hpdir)/arrays.o : $(hpdir)/arrays_tmp.f90 $(exdir)/intreal_types.o 
	$(F90) $(OPTIONS) -c $< -o $@
	rm -f $(hpdir)/arrays_tmp.f90

-include Makefile.dep

# TESTS
testdir = $(ppsrcdir)/tests
ini_test_mod = $(testdir)/read_ini_test.o
SRCS_ftest = $(testdir)/test_args.f90 $(exdir)/textlib.f90
$(testdir)/test_args.o: $(exdir)/textlib.o
initest_mods = $(ini_mods) $(ini_test_mod)

argspassing_objs = $(testdir)/args_passing.f90 $(exdir)/mpivars.o $(exdir)/textlib.o
$(testdir)/args_passing.o: $(argspassing_objs)

OPTIONS	   =  $(OPTIMIZE) $(FFTLIB) $(FFTINC) $(MODFLAG)$(moddir) $(OMPLIB)
OPTIONS_MERGE =  $(OPTIMIZE) $(MODFLAG)$(moddir)
OPTIONS_INI =  $(OPTIMIZE) $(MODFLAG)$(moddir)

EXEC_h = hpkvd
LIB_h = $(hpdir)/libhpkvd.so
OBJS_h = $(ini_mods) $(ex_mods) $(gv_mods) $(sl_mods) $(he_mods) $(rf_mods) $(sc_mods)\
	   $(ti_mods) $(hpkvd_mods) $(cosmo_mods) $(hpkvd_objs) $(hpkvd_main) 

EXEC_m = merge_pkvd
LIB_m = $(mgdir)/libmergepkvd.so
OBJS_m = $(ini_mods) $(ex_mods) $(gv_mods) $(merge_mods) $(cosmo_mods) $(merge_objs) $(ti_mods) $(merge_main)

EXEC_t = make_maptable
OBJS_t = $(mmtm_mods) $(mmtm_objs)

EXEC_c = make_cmbtable
OBJS_c = $(mmtc_mods) $(mmtc_objs)

EXEC_pm = pks2map
OBJS_pm = $(hpx_mods) $(p2m_mods) $(p2m_objs)

EXEC_pc = pks2cmb
OBJS_pc = $(hpx_mods) $(p2c_mods) $(p2c_objs)

# Filter generator
EXEC_f = filter_gen
OBJS_f = $(ini_mods) $(ini_inputs) $(ex_mods) filter_generator/filter_gen.o 

# TESTS FOR MODULES AND PARTS OF THE CODE

EXEC_initest = $(testdir)/read_ini_test
OBJS_initest =  $(initest_mods) $(ini_inputs)

EXEC_ftest = $(testdir)/test_args
OBJS_ftest = $(SRCS_ftest:.f90=.o)

EXEC_ctest = $(testdir)/hpkvd_c_test
OBJS_ctest = $(testdir)/hpkvd_c_test.o

EXEC_argspasstest = $(testdir)/args_passing
OBJS_argspasstest = $(argspassing_objs)

.SUFFIXES: .o .f .f90 .F90 .c .C

# General pattern rules for Fortran files
%.o: %.f90
	$(F90) $(OPTIONS) -c $< -o $@
%.o: %.f
	$(F77) $(OPTIONS) -c $< -o $@
%.o: %.F90
	$(F90) $(OPTIONS) -c $< -o $@

# Specific directories with specific compiler options
$(mgdir)/%.o: $(mgdir)/%.f90
	$(F90) $(OPTIONS_MERGE) -c $< -o $@
$(mgdir)/%.o: $(mgdir)/%.f
	$(F90) $(OPTIONS_MERGE) -c $< -o $@
$(gvdir)/%.o: $(gvdir)/%.f
	$(F90) $(OPTIONS) -c $< -o $@

$(inidir)/%.o: $(inidir)/%.f90
	$(F90) $(OPTIONS_INI) -c $< -o $@

$(hpkvd_main): $(hpdir)/hpkvd.f90
	$(F90) $(OPTIONS) -c $< -o $@

$(inidir)/config_reader_tmp.f90: $(inidir)/config_reader.f90
	sed 's|RUNDIR|$(RUNDIR)|g' $< > $@

$(ini_mods) : $(inidir)/config_reader_tmp.f90 $(ini_inputs)
	$(F90) $(OPTIONS) -I$(moddir) -c $< -o $@
	rm -f $(inidir)/config_reader_tmp.f90

# Pattern rules for C and C++ files
$(exdir)/%.o: $(exdir)/%.c
	$(CC)  $(CCOPTIONS) -c $< -o $@
$(exdir)/%.o: $(exdir)/%.C
	$(CXX)  $(CCOPTIONS) -c $< -o $@
$(testdir)/%.o: $(testdir)/%.c
	$(CC) $(CCOPTIONS) -c $< -o $@
%.o: %.C
	$(CXX)  $(OPTIONS) -c $< -o $@

# Linking rules for executables
$(LIB_h): $(OBJS_h) # hpkvd libarary. Has fortran and c wrappers
	$(F90) -shared $(OPTIONS) $(OBJS_h) $(FFTLIB) \
		-L$(shell $(F90) -print-file-name=libgfortran.so | xargs dirname) \
		-lgfortran -lquadmath -o $(LIB_h)
$(EXEC_h): $(LIB_h) $(hpkvd_main) 
	$(F90) $(OPTIONS) $(hpkvd_main) -L$(hpdir_full) -lhpkvd -Wl,-rpath,$(hpdir_full) \
	$(FFTLIB) -o $(bindir)/$(EXEC_h)

$(LIB_m): $(OBJS_m)
	$(F90) -shared $(OPTIONS) $(OBJS_m)\
		-L$(shell $(F90) -print-file-name=libgfortran.so | xargs dirname) \
		-lgfortran -lquadmath -o $(LIB_m)
$(EXEC_m): $(LIB_m) $(mg_main)
	$(F90) $(OPTIONS) $(mg_main) -L$(mgdir_full) -lmergepkvd -Wl,-rpath,$(mgdir_full) \
	$(FFTLIB) -o $(bindir)/$(EXEC_m)
#$(EXEC_m): $(OBJS_m)
#	$(F90) $(OPTIONS) $(OBJS_m) -o  $(bindir)/$(EXEC_m)
$(EXEC_t): $(OBJS_t)
	$(F90) $(OPTIONS) $(OBJS_t) -lm -o  $(bindir)/$(EXEC_t)
$(EXEC_c): $(OBJS_c)
	$(F90) $(OPTIONS) $(OBJS_c) -lm -o  $(bindir)/$(EXEC_c)
$(EXEC_pm): $(OBJS_pm)
	$(F90) $(OPTIONS) $(OBJS_pm) -L$(CFITSIO_LIBDIR) -lm -lcfitsio -o\
	$(bindir)/$(EXEC_pm)
$(EXEC_pc): $(OBJS_pc)
	$(F90) $(OPTIONS) $(OBJS_pc) -L$(CFITSIO_LIBDIR) -lm -lcfitsio -o\
	$(bindir)/$(EXEC_pc)
$(EXEC_f): $(OBJS_f)
	$(F90) $(OPTIMIZE) -I$(moddir) -o $(bindir)/$(EXEC_f) $^

$(EXEC_initest): $(OBJS_initest)
	$(F90) $(OPTIMIZE) $(OMPLIB) -o $@ $^ $(FFTWFLAGS)
$(EXEC_ftest): $(OBJS_ftest)
	$(F90) $(OPTIONS) $(OBJS_ftest) $(FFTLIB) -o $@
$(EXEC_ctest): $(LIB_h) $(OBJS_ctest) 
	$(CC) $(CCOPTIONS) $(OBJS_ctest) -L$(hpdir_full) -lhpkvd -Wl,-rpath,$(hpdir_full) $(FFTLIB) -o $@
$(EXEC_argspasstest): $(OBJS_argspasstest)
	$(F90) $(OPTIONS) $(OBJS_argspasstest) $(FFTLIB) -o $@


EXEC = $(bindir)/$(EXEC_h) $(bindir)/$(EXEC_m) \
	   $(bindir)/$(EXEC_t) $(bindir)/$(EXEC_pm) $(bindir)/$(EXEC_pc) $(LIB_h) $(LIB_m)\
	   $(EXEC_ctest) $(EXEC_ftest) $(EXEC_initest) $(EXEC_argspasstest) $(EXEC_f)
	   
OBJS = $(OBJS_h) $(OBJS_m) $(OBJS_t) $(OBJS_pm) $(OBJS_pc) $(OBJS_ctest) $(OBJS_ftest) \
	   $(OBJS_initest) $(OBJS_f)

clean_pp:
	@rm -f $(EXEC) $(OBJS) $(moddir)/*.mod $(sldir)/*.o $(rfdir)/*.o $(cosmodir)/*.o $(hpdir)/*.o \
		  $(mgdir)/*.o $(exdir)/*.o $(scdir)/*.o $(tidir)/*.o $(testdir)/*.o $(hpdir)/*.so
	@echo "Cleanup successful!"

run_test: $(EXEC_ftest) $(EXEC_ctest) $(EXEC_initest)
	./$(EXEC_ftest) 1 12345 
	./$(EXEC_initest) # For this, make sure that you have hpkvd_params.bin in src directory
	./$(EXEC_ctest)   # for this, make sure that you have inirameters.ini in src directory

clean_test:
	rm -f $(OBJS_ftest) $(EXEC_ftest) $(EXEC_ctest)
clean_hpkvd:
	rm -f $(EXEC_h) 
get-config:
	@echo "CONFIG_FILE = $(CONFIG_FILE)"
	$(eval NMESH := $(shell sed -n 's/^nmesh\s*=\s*\([0-9]\+\).*/\1/p' $(CONFIG_FILE)))
	$(eval N_REPLACE := $(NMESH))
	@echo "N_REPLACE = $(N_REPLACE)"


#┌───────────────────────────────────┐
#│     __  _____  _______ __________ │
#│    /  |/  / / / / ___//  _/ ____/ │
#│   / /|_/ / / / /\__ \ / // /      │
#│  / /  / / /_/ /___/ // // /___    │
#│ /_/  /_/\____//____/___/\____/    │
#│                                   │
#└───────────────────────────────────┘

#┌──────────────────────────────────────────────────────────────────┐
#│    ____            __              __             __             │
#│   / __ \__________/ /_  ___  _____/ /__________ _/ /_____  _____ │
#│  / / / / ___/ ___/ __ \/ _ \/ ___/ __/ ___/ __ `/ __/ __ \/ ___/ │
#│ / /_/ / /  / /__/ / / /  __(__  ) /_/ /  / /_/ / /_/ /_/ / /     │
#│ \____/_/   \___/_/ /_/\___/____/\__/_/   \__,_/\__/\____/_/      │
#│                                                                  │
#└──────────────────────────────────────────────────────────────────┘

