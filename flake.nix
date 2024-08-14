#======================================================== =#
# This Nix flake is for use with NixOS or nix package      #
# manager. Nix package manager is available on Linux,      #
# MacOS, or (if you are into this kind of stuff) Windows.  #
#                                                          #
# PURPOSE:                                                 #
# With nix, you don't have to worry about dependencies,    #
# libraries, activation scripts, etc. If it works on one   #
# machine, it works on all of them (as long as they have   #
# nix packages)                                            #
#                                                          #
# USAGE:                                                   #
# To enter the PeakPatch environment, defined in this      #
# flake, after you git cloned this repository and enabled  #
# flakes (as of April 2024, they are experimental), just   #
# run:                                                     #
# `nix develop`                                            #
# All the packages will be downloaded and you will         #
# automatically enter the PeakPatch Nix environment. You   #
# do not have to do anything else.                         #
#                                                          #
# TROUBLESHOOTING:                                         #
# If the command fail, that is probably because you didn't #
# enable flakes. In that case, run the command with        # 
# temporarily enabling them:                               #
#`nix develop --experimental-features 'nix-command flakes'`#
#                                                          #
# CREDITS:                                                 #
# This flake was packaged by Vasilii Pustovoit in April    #
# 2024                                                     #
#======================================================== =#

{
  description = "PeakPatch developement environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
          };
        };
        customFftw_single = pkgs.fftw.override {
          precision = "single";
          enableMpi = true;    
          mpi = pkgs.openmpi;  
        };
		
        customFftw_double = pkgs.fftw.override {
          precision = "double"; 
          enableMpi = true;     
          mpi = pkgs.openmpi;   
        };
        fortran_compiler = pkgs.gfortran13;

        # Healpy (failed nix port)
        healpy = pkgs.python311Packages.buildPythonPackage rec {
          pname = "healpy";
          version = "1.16.6";

          src = pkgs.python311Packages.fetchPypi{
            inherit pname;
            inherit version;
            sha256 = "sha256-CrJugo/NJRoUEJWvbZvz26Q87G8PXNSLZb8K+PVjKfE=";
          };

          buildInputs = [ 
            pkgs.python311Packages.numpy 
            pkgs.python311Packages.matplotlib 
            pkgs.python311Packages.astropy 
            pkgs.python311Packages.numpydoc 
            pkgs.python311Packages.cython 
            pkgs.pkg-config
            pkgs.cfitsio
            pkgs.which
            pkgs.zlib
            pkgs.coreutils
            pkgs.bash
          ];

          nativeBuildInputs = [ pkgs.pkg-config ];

          propagatedBuildInputs = [ 
            pkgs.python311Packages.numpy 
            pkgs.python311Packages.matplotlib 
            pkgs.python311Packages.astropy 
            pkgs.python311Packages.numpydoc 
            pkgs.python311Packages.cython 
            pkgs.pkg-config
            pkgs.cfitsio
            pkgs.which
            pkgs.zlib
          ];
          
          preBuild = ''
            export PKG_CONFIG="${pkgs.pkg-config}/bin/pkg-config"
            export PATH=${pkgs.coreutils}/bin:$PATH
            export PKG_CONFIG_PATH="${pkgs.zlib}/lib/pkgconfig"

            # Add the existing PKG_CONFIG_PATH if it's set
            if [ -n "$PKG_CONFIG_PATH" ]; then
              export PKG_CONFIG_PATH="${pkgs.zlib}/lib:$PKG_CONFIG_PATH"
            else
              export PKG_CONFIG_PATH="${pkgs.zlib}/lib"
            fi
            echo "Environment Variables:"
            env
          '';

          postPatch = ''
            substituteInPlace setup.py \
              --replace "find_packages()" "find_packages(include=['healpy', 'healpy.*'])"
          '';
          };
      in
      {
        devShell = pkgs.mkShell {
          buildInputs = with pkgs; [
            (python3.withPackages (ps: with ps; [
              #healpy # HEALPY IS NOT IN NIXPKGS, SO NEED TO MANUALLY PACKAGE IT!
              pandas
              matplotlib
              numpy
              astropy
              scipy
            ]))
            which
            gsl
            cfitsio
            gcc
            mpi
			fortran_compiler
            llvmPackages.openmp
            customFftw_single
            customFftw_double
			hdf5

			# Debuggers - can remove this if you want
			gdb
			valgrind
			# These are needed for tmpi
			reptyr
			mpich

			# These are needed for MUSIC
			gfortran.cc
          ];
          shellHook = ''
            # PeakPatch system variables, used by peakpatchtools.py
            export PP_DIR=$(dirname $(pwd))/peakpatch
            export MUSIC_DIR=$(dirname $(pwd))/music
            export PYTHONPATH=$PP_DIR/python
            export PATH=$PATH:$PP_DIR/bin:$PP_DIR/python
            # Make Python scripts executable
            chmod +x $PP_DIR/python/peak-patch*.py

            # Set the paths to FFTW, GFORT, MPI libraries used by PeakPatch
            export FFTW_SINGLE_PATH=${customFftw_single.dev}
            export FFTW_DOUBLE_PATH=${customFftw_double.dev}
            #export MPI_PATH=$(dirname "$(echo $PATH |  sed 's/:/\n/g' | grep -i mpi | tail -n 1)")
            export MPI_PATH=${pkgs.mpich}

			# MUSIC-required inputs
            export FFTW_PATH=${customFftw_single}
            export HDF5_PATH=${pkgs.hdf5}
		    export GFORTCC_PATH=${pkgs.gfortran.cc}
		    export GFORT_LPATH=${fortran_compiler.cc.lib}/lib
		    export GCC_PATH=${pkgs.gcc}

            export GSL_INCLUDE_PATH=${pkgs.gsl.dev}/include
            export GSL_LIBRARY_PATH=${pkgs.gsl}/lib
            export HDF5_INCLUDE_PATH=${pkgs.hdf5.dev}/include
            export HDF5_LIBRARY_PATH=${pkgs.hdf5}/lib
			export LD_LIBRARY_PATH=${pkgs.lib.makeLibraryPath [ 
															 	pkgs.mpich
															 	pkgs.gcc.cc.lib
															 	#fortran_compiler.cc.lib
														       ]
								     }:$LD_LIBRARY_PATH


            # This flag will let peakpatchtools.py know that we're running on nix.
            # As of April 2024, healpy isn't packaged in nixpkgs, and I wasted a whole day trying 
            # to package it myself (see above). You are welcome to continue packaging it, or
            # allow peakpatchtools.py to use healpy once it's packaged in nixpkgs
			# This flag is also required to run MUSIC
            export NIX_BUILD=1

            # Create useful aliases and utility environment variables
            alias ppclean="$PP_DIR/cleanup.sh ./"
            alias ppcopy="python $PP_DIR/python/peak-patch.py ./param/param.params"
            alias ppcopyini="python $PP_DIR/python/peak-patch.py ./param/parameters.ini"
            alias pprun="python $PP_DIR/python/peak-patch.py ./param/param.params; ./bin/hpkvd 1; \
						 chmod +x *.sh; ./*.sh"
            alias ppcpep="cp -r $PP_DIR/example/param ./"
            alias hpkvdtest="./bin/hpkvd 1 13579 ./hpkvd_params.bin; ./bin/hpkvd 0 13579 ./hpkvd_params.bin"
            alias hpkvdtestini="./bin/hpkvd 1 13579 ./param/parameters.ini; ./bin/hpkvd 0 13579 ./param/parameters.ini"
			alias pptest="$PP_DIR/cleanup.sh ./; \
						   cp -r $PP_DIR/example/param ./; \
						   python $PP_DIR/python/peak-patch.py ./param/param.params"
            alias vasreb="$PP_DIR/scripts/rebase_main_to_vasdev.sh"
			# Alias for  merging
		    vimmerge() {
		        local file=$1
		        vimdiff "$file" <(git show 070640d2e03466a8bc20269464c650d18c938d75:"$file")
		    }

            export PP_ALIASES='
Useful aliases that you can run in directory where you
will be running PeakPatch:

ppcopy:
Will run peak-patch.py and copy the source to current dir 
(param.params file)

ppcopyini:
Will run peak-patch.py and copy the source to current dir 
(parameters.ini file)

hpkvdtest:
Will create collapse tables once PeakPatch compiled,
and run hpkvd with seed 13579 (param.params file)

hpkvdtestini:
Will create collapse tables once PeakPatch compiled,
and run hpkvd with seed 13579 (parameters.ini file)

pprun:
Will do the same as ppcopy, but also will run peakpatch with:
./bin/hpkvd 1; ./<your_runscript>

ppclean: 
Will clean the current directory of anything but params

ppcpep: 
(PeakPatch CoPy Example Params File) - copies the example 
parameter file to the current dir

pptest:
Copy the source code and compile all the files

pphelp:
Display this message

vasreb:
Brings main branch up to date with vasdev and pushes all the changes. 
Use with CAUTION! DO NOT USE if not sure if main was changed!
'
		    alias pphelp="echo \"$PP_ALIASES\""
            # Welcome message
			echo "
#########################################################
########  Welcome to PeakPatch-MUSIC Interface! #########
#########################################################
		    "
			echo "$PP_ALIASES"
          '';
      };
    }
  );
}

