// feasibility.cpp - implementation of include/ppmi/feasibility.hpp
//
// See the header for the algorithm sources. The filter bank and mass range
// are a direct transcription of peakpatch/src/filter_generator/filter_gen.f90
// lines 56-96. required_levelmin inverts that relation. expected_halo_count is
// a self-contained Sheth-Tormen estimate: an Eisenstein & Hu transfer
// function, a top-hat-windowed sigma(M) normalised to sigma_8, and a
// Carroll-Press-Turner growth factor, integrated over the box volume.
//
// Cosmology (catalog.hpp) carries only Omega_m and h -- no Omega_b, no
// curvature. So this file uses the Omega_b -> 0 limit of the Eisenstein & Hu
// (1998) transfer function, which reduces to the Bardeen-Bond-Kaiser-Szalay
// shape, and assumes flatness (Omega_L = 1 - Omega_m) with no radiation term.
// Those are fine for a go/no-go feasibility check -- the header is explicit
// that this is an order-of-magnitude guide, not a prediction -- but they are
// not what you'd want for a paper. See feasibility.hpp's own comment on this.

#include "ppmi/feasibility.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace ppmi {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeltaC = 1.686;  // linear collapse threshold

// ---------------------------------------------------------------------------
// Growth factor: Carroll, Press & Turner (1992) fitting formula for a flat
// matter+Lambda universe, normalised to D(z=0) = 1.
// ---------------------------------------------------------------------------
double growth_factor(double z, double omega_m0) {
  const double omega_l0 = 1.0 - omega_m0;
  auto g = [&](double zz) {
    double a3 = (1.0 + zz) * (1.0 + zz) * (1.0 + zz);
    double e2 = omega_m0 * a3 + omega_l0;
    double omz = omega_m0 * a3 / e2;
    double olz = omega_l0 / e2;
    return 2.5 * omz /
           (std::pow(omz, 4.0 / 7.0) - olz +
            (1.0 + omz / 2.0) * (1.0 + olz / 70.0));
  };
  double g0 = g(0.0);
  double gz = g(z);
  return gz / (g0 * (1.0 + z));
}

// ---------------------------------------------------------------------------
// Eisenstein & Hu (1998) transfer function, Omega_b -> 0 limit (their
// baryon-free reduction, equal to the Bardeen et al. 1986 BBKS shape).
// k_mpc in physical Mpc^-1.
// ---------------------------------------------------------------------------
double eh_transfer(double k_mpc, double omega_m, double h) {
  double omhh = omega_m * h * h;
  if (omhh <= 0.0 || k_mpc <= 0.0) return 1.0;
  double theta27 = 2.725 / 2.7;
  double k_hmpc = k_mpc / h;  // convert to h/Mpc, the formula's native units
  double q = k_hmpc * theta27 * theta27 / omhh;
  if (q <= 0.0) return 1.0;
  double t = std::log(1.0 + 2.34 * q) / (2.34 * q);
  double poly = 1.0 + 3.89 * q + std::pow(16.1 * q, 2) + std::pow(5.46 * q, 3) +
                std::pow(6.71 * q, 4);
  return t * std::pow(poly, -0.25);
}

// Real-space top-hat window, small-x series to avoid cancellation error.
double top_hat_window(double x) {
  if (x < 1e-4) {
    double x2 = x * x;
    return 1.0 - x2 / 10.0 + x2 * x2 / 280.0;
  }
  return 3.0 * (std::sin(x) - x * std::cos(x)) / (x * x * x);
}

// sigma^2(R) for the *unnormalised* power spectrum P(k) = k^nspec T(k)^2.
// Caller rescales by sigma_8. Integrated in log k with the trapezoid rule.
double sigma2_raw(double R_mpc, double omega_m, double h, double nspec) {
  constexpr int kN = 2000;
  const double lnkmin = std::log(1e-5);
  const double lnkmax = std::log(2e4);
  const double dlnk = (lnkmax - lnkmin) / kN;
  double sum = 0.0;
  for (int i = 0; i <= kN; ++i) {
    double lnk = lnkmin + i * dlnk;
    double k = std::exp(lnk);
    double t = eh_transfer(k, omega_m, h);
    double pk = std::pow(k, nspec) * t * t;
    double w = top_hat_window(k * R_mpc);
    // extra factor of k from the d(ln k) = dk/k change of variable, on top
    // of the k^2 in the usual sigma^2 = 1/(2 pi^2) INT k^2 P(k) W^2 dk.
    double integrand = k * k * k * pk * w * w;
    double weight = (i == 0 || i == kN) ? 0.5 : 1.0;
    sum += weight * integrand;
  }
  sum *= dlnk;
  return sum / (2.0 * kPi * kPi);
}

// Everything expected_halo_count needs that only depends on cosmology, not on
// mass, bundled so it is computed once per call rather than once per mass
// bin.
struct SigmaEval {
  double omega_m;
  double h;
  double nspec;
  double rho_m;
  double norm;  // sigma_8^2 / sigma2_raw(8/h)
  double growth;

  double operator()(double mass) const {
    double r_mpc = std::cbrt(3.0 * mass / (4.0 * kPi * rho_m));
    double s2 = sigma2_raw(r_mpc, omega_m, h, nspec) * norm;
    return std::sqrt(s2) * growth;
  }
};

SigmaEval make_sigma_eval(const Cosmology& cosmo, double sigma_8, double nspec,
                          double redshift) {
  SigmaEval s;
  s.omega_m = cosmo.Omega_m;
  s.h = cosmo.h;
  s.nspec = nspec;
  s.rho_m = cosmo.rho_m();
  double r8 = 8.0 / cosmo.h;
  double s2_8_raw = sigma2_raw(r8, cosmo.Omega_m, cosmo.h, nspec);
  s.norm = (sigma_8 * sigma_8) / s2_8_raw;
  s.growth = growth_factor(redshift, cosmo.Omega_m);
  return s;
}

// Sheth-Tormen multiplicity function f(sigma), and dn/dlnM at a given mass.
double dn_dlnM(double mass, const SigmaEval& sig) {
  const double frac = 1e-3;
  double mp = mass * (1.0 + frac);
  double mm = mass * (1.0 - frac);
  double sp = sig(mp);
  double sm = sig(mm);
  double s0 = sig(mass);
  if (s0 <= 0.0) return 0.0;
  double dlnsigma_dlnM = (std::log(sp) - std::log(sm)) / (std::log(mp) - std::log(mm));

  const double a = 0.707, p = 0.3, norm_a = 0.3222;
  double nu = kDeltaC / s0;
  double fsigma = norm_a * std::sqrt(2.0 * a / kPi) *
                  (1.0 + std::pow(1.0 / (a * nu * nu), p)) * nu *
                  std::exp(-a * nu * nu / 2.0);

  return (sig.rho_m / mass) * fsigma * std::fabs(dlnsigma_dlnM);
}

}  // namespace

std::vector<double> filter_bank(double cellsize, double rsmooth_max) {
  double rmin = kRminCellFactor * cellsize;
  if (rmin > rsmooth_max) {
    std::ostringstream os;
    os << "filter_bank: Rsmooth_max=" << rsmooth_max
       << " is below the smallest filter radius " << rmin
       << " (1.65 * cellsize=" << cellsize
       << "); the filter bank would be empty, meaning no halos at all";
    throw std::invalid_argument(os.str());
  }

  std::vector<double> radii;
  radii.push_back(rmin);
  while (radii.back() * kFilterRatio < rsmooth_max) {
    radii.push_back(radii.back() * kFilterRatio);
  }

  double undershoot_limit = rsmooth_max * kFilterSnapFraction;
  if (radii.back() < rsmooth_max - undershoot_limit) {
    radii.push_back(rsmooth_max);
  }
  return radii;
}

MassRange mass_range(const PeakPatchGrid& grid, const Cosmology& cosmo,
                     double rsmooth_max) {
  MassRange mr;
  mr.cellsize = grid.cellsize();
  std::vector<double> bank = filter_bank(mr.cellsize, rsmooth_max);
  mr.n_filters = static_cast<int>(bank.size());
  mr.m_min = halo_mass(bank.front(), cosmo);
  mr.m_max = halo_mass(bank.back(), cosmo);
  return mr;
}

int required_levelmin(double boxlength, double target_mass,
                      const Cosmology& cosmo, int ntile, int nbuff) {
  (void)nbuff;  // cellsize = boxlength/2^levelmin regardless of nbuff, given
                // the pairing constraint nsub*ntile == 2^levelmin (see
                // geometry.hpp): dcore_box/nsub = (boxlength/ntile) /
                // (2^levelmin/ntile) = boxlength/2^levelmin.

  // Invert M = halo_mass(1.65*cellsize, cosmo) for cellsize, using
  // halo_mass itself (called at r=1) to pick up its constant rather than
  // reimplementing 4/3 pi rho_m here.
  double c = halo_mass(1.0, cosmo);
  double r_needed = std::cbrt(target_mass / c);
  double cellsize_needed = r_needed / kRminCellFactor;

  auto m_min_at = [&](int level) {
    double cellsize = boxlength / std::pow(2.0, level);
    return halo_mass(kRminCellFactor * cellsize, cosmo);
  };

  int lm = static_cast<int>(std::ceil(std::log2(boxlength / cellsize_needed)));
  if (lm < 1) lm = 1;

  // Floating-point guard: nudge lm so it is truly the smallest level whose
  // m_min does not exceed target_mass.
  while (lm > 1 && m_min_at(lm - 1) <= target_mass) --lm;
  while (m_min_at(lm) > target_mass) ++lm;

  // Round up further to the next level whose 2^levelmin divides evenly by
  // ntile.
  while (ntile > 1 && ((1LL << lm) % ntile) != 0) ++lm;

  return lm;
}

double expected_halo_count(double boxlength, double min_mass, double redshift,
                           const Cosmology& cosmo, double sigma_8,
                           double nspec) {
  if (min_mass <= 0.0) return 0.0;

  SigmaEval sig = make_sigma_eval(cosmo, sigma_8, nspec, redshift);

  double mmax = std::max(min_mass * 1.0e5, 1.0e16);
  mmax = std::min(mmax, 1.0e19);
  if (mmax <= min_mass) mmax = min_mass * 10.0;

  constexpr int kM = 300;
  double lnmmin = std::log(min_mass);
  double lnmmax = std::log(mmax);
  double dlnm = (lnmmax - lnmmin) / kM;

  double integral = 0.0;
  for (int i = 0; i <= kM; ++i) {
    double lnm = lnmmin + i * dlnm;
    double m = std::exp(lnm);
    double val = dn_dlnM(m, sig);
    double weight = (i == 0 || i == kM) ? 0.5 : 1.0;
    integral += weight * val;
  }
  integral *= dlnm;

  double volume = boxlength * boxlength * boxlength;
  double n = volume * integral;
  return n < 0.0 ? 0.0 : n;
}

std::string feasibility_report(double boxlength, int levelmin, double redshift,
                               const Cosmology& cosmo, double sigma_8,
                               double nspec, int ntile, int nbuff,
                               double rsmooth_max) {
  (void)nbuff;
  // cellsize = boxlength/2^levelmin, independent of nbuff, per the pairing
  // constraint documented in geometry.hpp and derived in required_levelmin
  // above.
  double cellsize = boxlength / std::pow(2.0, levelmin);

  std::ostringstream os;
  os << "PeakPatch feasibility check\n";
  os << "  box            : " << boxlength << " Mpc, levelmin " << levelmin
     << " (" << (1LL << levelmin) << "^3 cells), ntile " << ntile << "\n";
  os << "  cell size      : " << cellsize << " Mpc\n";

  std::vector<double> bank;
  bool bank_empty = false;
  std::string bank_error;
  try {
    bank = filter_bank(cellsize, rsmooth_max);
  } catch (const std::invalid_argument& e) {
    bank_empty = true;
    bank_error = e.what();
  }

  if (bank_empty) {
    os << "  filter bank    : EMPTY -- Rsmooth_max=" << rsmooth_max
       << " does not reach the smallest filter radius "
       << (kRminCellFactor * cellsize) << " Mpc\n";
    os << "  verdict        : NOT FEASIBLE -- no smoothing scale in this "
          "configuration can find a halo. "
       << bank_error << "\n";
    return os.str();
  }

  double m_min = halo_mass(bank.front(), cosmo);
  double m_max = halo_mass(bank.back(), cosmo);
  double n_halos = expected_halo_count(boxlength, m_min, redshift, cosmo,
                                       sigma_8, nspec);

  os << "  filter count   : " << bank.size() << " scales, "
     << bank.front() << " to " << bank.back() << " Mpc\n";
  os << "  mass range     : " << m_min << " to " << m_max << " Msun\n";
  os << "  redshift       : " << redshift << "\n";
  os << "  expected count : ~" << n_halos
     << " halos above the minimum resolvable mass (Sheth-Tormen estimate, "
        "order of magnitude only)\n";

  os << "  verdict        : ";
  if (n_halos < 1.0) {
    os << "NOT FEASIBLE -- fewer than one halo above the minimum resolvable "
          "mass is expected in this box at z=" << redshift
       << "; no configuration of this run will find one.\n";
  } else if (n_halos < 10.0) {
    os << "MARGINAL -- only ~" << n_halos
       << " halos are expected above the minimum resolvable mass; consider a "
          "larger box or coarser target mass.\n";
  } else {
    os << "FEASIBLE -- the mass range and expected halo count both look "
          "workable.\n";
  }

  return os.str();
}

}  // namespace ppmi
