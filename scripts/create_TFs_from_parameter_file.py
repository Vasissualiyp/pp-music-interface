from create_TF import calculate_TF 
from peakpatchtools import PeakPatch
import os
from pathlib import Path
import argparse

def create_TFs_from_parameter_file(run_dir,
                                   extrap_params,
                                   tables_dir_outside=True,
                                   params_file_name="parameters.ini", 
                                   debug_pptools=False,
                                   log=False):
    """
    Creates relevant PS/TFs for both MUSIC and PeakPatch given a parameter file

    Args:
        run_dir (str): directory of the run
        extrap_params (extrapParams): Extrapolation parameters
        tables_dir_outside (bool): whether the tables dir for PeakPatch is inside of rundir or outside of it
        params_file_name (str): name of the parameters file
        bebug_pptools (bool): whether to printout debug info for peakpatchtools
        log (bool): whether to printout logs
    """
    params_file = os.path.join(run_dir, "param", params_file_name)

    # Create PeakPatch object
    TFCalc = calculate_TF(4, log=log)

    TFCalc.extrap_params(maxkh_extrap = extrap_params.maxkh_extrap,
                         extrap_fraction = extrap_params.extrap_fraction,
                         highk_mode = extrap_params.highk_mode)

    # Variant 1: import cosmology from a run
    run = PeakPatch(params_file=params_file, run_dir=run_dir, debug=debug_pptools)
    TFCalc.init_cosmology_from_run(run, minkh=minkh, maxkh=maxkh)

    # Get the names of the tables from the parameter file
    class_file_pp = run.pkfile
    music_transfer = run.transfer
    camb_file = "camb_file"
    if music_transfer.lower() != camb_file:
        raise ValueError(f"Value of tranfer in the parmaeter file must be {camb_file}, otherwise the table won't be read!")
    try:
        class_file_music = run.transfer_file
    except:
        raise ImportError(f"transfer_file value not present in the parameter file!")

    # Change the directories of the tables
    if tables_dir_outside:
        pp_tables_dir = os.path.join(Path(run_dir).parent, "", "tables")
    else:
        pp_tables_dir = os.path.join(run_dir, "tables")
    class_file_pp = os.path.join(pp_tables_dir, class_file_pp)
    try:
        os.makedirs(pp_tables_dir, exist_ok=True)
    except OSError as e:
        raise InterruptedError(f"Could not create directory {pp_tables_dir}") from e
    print(f"pp_tables_dir: {pp_tables_dir}")
    print(f"class_file_pp: {class_file_pp}")
    class_file_music = os.path.join(run_dir, class_file_music)

    #Save CLASS-generated PS in PeakPatch format
    _ = TFCalc.create_and_save_TF_class(class_file_pp)
    #Save CLASS-generated PS in MUSIC format
    TFCalc.set_output_type(13)
    header, data = TFCalc.get_formatted_data()
    TFCalc.Save_output(header, data, class_file_music)

class extrapParams():
    """Extrapolation parameters"""
    def __init__(self, maxkh_extrap, extrap_fraction, highk_mode):
        """Create a new instance"""
        self.maxkh_extrap = maxkh_extrap
        self.extrap_fraction = extrap_fraction
        self.highk_mode = highk_mode

if __name__ == "__main__":


    parser = argparse.ArgumentParser(prog="TF creator from MUSIC+PP Parameter files",
                                     description="Creates required transfer function tables" +
                                                 "from the given parameter file")
    parser.add_argument("run_dir", default=".")
    parser.add_argument("params_file_name", default="parameters.ini")
    args = parser.parse_args()

    log = True
    debug_pptools = False

    #run_dir = "."
    #params_file_name = "parameters.ini"
    run_dir = args.run_dir
    params_file_name = args.params_file_name
    #extrapolation_scheme = "analytic"
    extrapolation_scheme = "loglin_extrap"
    minkh = 1e-6
    maxkh = 1e2
    maxkh_extrap = 5e6
    extrap_fraction = 2 # Last 2 points

    extrap_params = extrapParams(maxkh_extrap, extrap_fraction, extrapolation_scheme)
    create_TFs_from_parameter_file(run_dir, extrap_params,
                                   params_file_name=params_file_name, 
                                   debug_pptools=debug_pptools, log=log)
