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
```

You can do it for instance, with the following command:

```
export PP_DIR = /path/to/peakpatch
export MUSIC_DIR = /path/to/music
```

## Compilation

Change the `Makefile.systype` to the name of your system.
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
