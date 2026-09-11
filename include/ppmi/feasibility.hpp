// feasibility.hpp - can this run find the halo you want, before the node-hours.
//
// PeakPatch's accessible halo mass range is fixed by its filter bank, built in
// peakpatch/src/filter_generator/filter_gen.f90 lines 56-96: the smallest
// smoothing radius is 1.65*cellsize, radii climb geometrically by 1.15, and the
// largest is Rsmooth_max, with a final radius forced exactly at Rsmooth_max
// when the last geometric step undershoots it by more than 7 percent.
// Converting radii to masses with M = 4/3 pi R^3 rho_m gives the range.
//
// So the smallest resolvable halo is set by grid resolution and the largest by
// Rsmooth_max. If the halo you want at z=30 falls below the first, no amount of
// reconfiguring the rest of the pipeline will find it. That is the question
// this header exists to answer cheaply.

#pragma once

#include <string>
#include <vector>

#include "ppmi/catalog.hpp"
#include "ppmi/geometry.hpp"

namespace ppmi {

// Smallest filter radius as a multiple of the cell size.
inline constexpr double kRminCellFactor = 1.65;
// Geometric ratio between adjacent filter radii.
inline constexpr double kFilterRatio = 1.15;
// Final radius is forced to Rsmooth_max if the last step undershoots by more
// than this fraction.
inline constexpr double kFilterSnapFraction = 0.07;

// Linear growth factor D(z), Carroll-Press-Turner for flat LCDM, normalised so
// that D(0) = 1. Exposed because the pipeline needs D(0)/D(zstart) to convert
// MUSIC's field, written at zstart, into the z=0 linear field PeakPatch expects.
double growth_factor(double z, double omega_m0);

struct MassRange {
  double m_min = 0.0;     // mass at the smallest filter radius, M_sun
  double m_max = 0.0;     // mass at Rsmooth_max, M_sun
  int n_filters = 0;
  double cellsize = 0.0;
};

// The smoothing radii PeakPatch will actually use.
// Throws std::invalid_argument if rsmooth_max is below the smallest radius,
// which means an empty bank and therefore no halos at all.
std::vector<double> filter_bank(double cellsize, double rsmooth_max);

MassRange mass_range(const PeakPatchGrid& grid, const Cosmology& cosmo,
                     double rsmooth_max);

// The smallest levelmin that resolves target_mass in a given box. Inverts
// mass_range, then rounds up to the next power of two that also divides evenly
// by ntile.
int required_levelmin(double boxlength, double target_mass,
                      const Cosmology& cosmo, int ntile = 1, int nbuff = 0);

// Roughly how many halos above min_mass a box should contain at z, via a
// Sheth-Tormen mass function with an Eisenstein and Hu transfer function.
// Accurate enough for a feasibility check and avoids a Boltzmann-code
// dependency here. An order-of-magnitude guide, not a prediction; its job is to
// catch the case where the answer is far below one, which means the run cannot
// succeed no matter how it is configured.
double expected_halo_count(double boxlength, double min_mass, double redshift,
                           const Cosmology& cosmo, double sigma_8,
                           double nspec);

// Human-readable summary for `ppmi feasibility`. States the cell size, the mass
// range, the filter count and the expected halo count, and ends with a plain
// verdict on whether the configuration can find what was asked for.
std::string feasibility_report(double boxlength, int levelmin, double redshift,
                               const Cosmology& cosmo, double sigma_8,
                               double nspec, int ntile, int nbuff,
                               double rsmooth_max);

}  // namespace ppmi
