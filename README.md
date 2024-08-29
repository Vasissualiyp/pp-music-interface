# PeakPatch-MUSIC Interface

This repository is supposed to be used in combination with GIZMO-setup to be able to run PeakPatch
as a submodule of MUSIC.

**This code will only work if used from GIZMO-setup!**
The link for the GIZMO-setup: https://github.com/Vasissualiyp/GIZMO-setup

## Setting up the code

To set the code up, you must have the following environment variables set with `export` command:
```
PP_DIR (For PeakPatch directory)
MUSIC_DIR (For MUSIC directory)
INTERFACE_DIR (For directory of this repo)
```

You can do it for instance, with the following command:

```
export PP_DIR = /path/to/peakpatch
export MUSIC_DIR = /path/to/music
export INTERFACE_DIR = /path/to/this_repository
```

Then you have to copy the tables to the parent directory of this code:
```
cp -r $PP_DIR/tables ../tables
```

## Necessary Modules

When running the interface on one of the clusters, load the modules like this before compilation/run:

### CITA
```
module load openmpi/4.1.6-gcc-ucx fftw/3.3.10-openmpi-ucx gsl/2.7.1 cfitsio/4.0.0 python/3.10.2
```

### CITA-starq
```
module load openmpi/4.1.6-gcc-ucx fftw/3.3.10-openmpi-ucx gsl/2.7.1 cfitsio/4.0.0 python/3.10.2
```

### Niagara
```
module load NiaEnv/2019b intel/2019u4 fftw/3.3.8 cfitsio/4.4.0 python/3.6.8 intelmpi/2019u4 gsl/2.5
```

## Building argparse

Argparse is a C++ module that is relevant for functioning of MUSIC after rewrite
(it allows for a better treatment of CLI arguments)

After you set `INTERFACE_DIR` variable in your `.bashrc`, head over to the `scripts`
directory and run `install_argparse.sh`. Don't forget to have cmake loaded before than.

The script should automatically install argparse for you.

## Compilation

Change the `Makefile.systype` to the name of your system.
After that, load the modules.
Then, compile the code with the following command:
```
make hpkvd; make filter_gen; make -j### MUSIC
```
Here `###` stands for the number of parallel cores you want to use for compilation.
You can compile MUSIC in parallel fashion, but not PeakPatch (hpkvd).

To clean object and other compiled files, you can do:
```
make clean_music (Only for MUSIC)
make clean_pp (Only for PeakPatch)
make clean (For both)
```

## Running

Edit the parameter file in `param/parameters.ini`, to set up parameters for both MUSIC and hpkvd runs.

Here are a few important things restrictions for the parameters:

* `seed` means nothing in `random`, it originally was used to generate PeakPatch overdensity field, 
but since our overdensity is made by MUSIC, it is irrelevant.
* Make sure that `levelmin`=`levelmax`, and that `nmesh`=2^`levelmin`. 
Otherwise MUSIC-generated field will not be shaped the way that PeakPatch expects it to be 
* `boxlength` should be equal to `boxsize`. Otherwise MUSIC-generated field in a box of a certain size
will incorrectly be interpreted by PeakPatch. You will get the results, they will just be plain wrong.

Then, generate filter banks with:
```
./bin/filter_gen <path/to/parameters.ini>
```

Finally, you can run `hpkvd + MUSIC` with:
```
./MUSIC <path/to/parameters.ini>
```

To learn more about available MUSIC flags, run it without parameter file, like `./MUSIC`
