/*
 * Copyright (C) 2010-2022 The ESPResSo project
 * Copyright (C) 2002,2003,2004,2005,2006,2007,2008,2009,2010
 *   Max-Planck-Institute for Polymer Research, Theory Group
 *
 * This file is part of ESPResSo.
 *
 * ESPResSo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ESPResSo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/** \file
 *  Statistical tools to analyze simulations.
 *
 *  The corresponding header file is statistics.hpp.
 */

#include "statistics.hpp"

#include "Particle.hpp"
#include "cells.hpp"
#include "communication.hpp"
#include "errorhandling.hpp"
#include "grid.hpp"
#include "grid_based_algorithms/lb_interface.hpp"
#include "partCfg_global.hpp"

#include <utils/Vector.hpp>
#include <utils/constants.hpp>
#include <utils/contains.hpp>
#include <utils/math/sqr.hpp>

#include <cassert>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

/****************************************************************************************
 *                                 basic observables calculation
 ****************************************************************************************/

double mindist(PartCfg &partCfg, const std::vector<int> &set1,
               const std::vector<int> &set2) {
  using Utils::contains;

  auto mindist_sq = std::numeric_limits<double>::infinity();

  for (auto jt = partCfg.begin(); jt != partCfg.end(); ++jt) {
    /* check which sets particle j belongs to (bit 0: set1, bit1: set2) */
    auto in_set = 0u;
    if (set1.empty() || contains(set1, jt->type()))
      in_set = 1u;
    if (set2.empty() || contains(set2, jt->type()))
      in_set |= 2u;
    if (in_set == 0)
      continue;

    for (auto it = std::next(jt); it != partCfg.end(); ++it)
      /* accept a pair if particle j is in set1 and particle i in set2 or vice
       * versa. */
      if (((in_set & 1u) && (set2.empty() || contains(set2, it->type()))) ||
          ((in_set & 2u) && (set1.empty() || contains(set1, it->type()))))
        mindist_sq = std::min(
            mindist_sq, box_geo.get_mi_vector(jt->pos(), it->pos()).norm2());
  }

  return std::sqrt(mindist_sq);
}

static Utils::Vector3d mpi_particle_momentum_local() {
  auto const particles = cell_structure.local_particles();
  auto const momentum =
      std::accumulate(particles.begin(), particles.end(), Utils::Vector3d{},
                      [](Utils::Vector3d &m, Particle const &p) {
                        return m + p.mass() * p.v();
                      });

  return momentum;
}

REGISTER_CALLBACK_REDUCTION(mpi_particle_momentum_local,
                            std::plus<Utils::Vector3d>())

Utils::Vector3d calc_linear_momentum(bool include_particles,
                                     bool include_lbfluid) {
  Utils::Vector3d linear_momentum{};
  if (include_particles) {
    linear_momentum +=
        mpi_call(::Communication::Result::reduction,
                 std::plus<Utils::Vector3d>(), mpi_particle_momentum_local);
  }
  if (include_lbfluid) {
    linear_momentum += lb_lbfluid_calc_fluid_momentum();
  }
  return linear_momentum;
}

Utils::Vector3d centerofmass(PartCfg &partCfg, int type) {
  Utils::Vector3d com{};
  double mass = 0.0;

  for (auto const &p : partCfg) {
    if ((p.type() == type) || (type == -1))
      if (not p.is_virtual()) {
        com += p.pos() * p.mass();
        mass += p.mass();
      }
  }
  com /= mass;
  return com;
}

Utils::Vector3d angularmomentum(PartCfg &partCfg, int type) {
  Utils::Vector3d am{};

  for (auto const &p : partCfg) {
    if ((p.type() == type) || (type == -1))
      if (not p.is_virtual()) {
        am += p.mass() * vector_product(p.pos(), p.v());
      }
  }
  return am;
}

void momentofinertiamatrix(PartCfg &partCfg, int type, double *MofImatrix) {
  double massi;
  Utils::Vector3d p1{};

  for (unsigned int i = 0; i < 9; i++)
    MofImatrix[i] = 0.;

  auto const com = centerofmass(partCfg, type);
  for (auto const &p : partCfg) {
    if (type == p.type() and (not p.is_virtual())) {
      p1 = p.pos() - com;
      massi = p.mass();
      MofImatrix[0] += massi * (p1[1] * p1[1] + p1[2] * p1[2]);
      MofImatrix[4] += massi * (p1[0] * p1[0] + p1[2] * p1[2]);
      MofImatrix[8] += massi * (p1[0] * p1[0] + p1[1] * p1[1]);
      MofImatrix[1] -= massi * (p1[0] * p1[1]);
      MofImatrix[2] -= massi * (p1[0] * p1[2]);
      MofImatrix[5] -= massi * (p1[1] * p1[2]);
    }
  }
  /* use symmetry */
  MofImatrix[3] = MofImatrix[1];
  MofImatrix[6] = MofImatrix[2];
  MofImatrix[7] = MofImatrix[5];
}

std::vector<int> nbhood(PartCfg &partCfg, const Utils::Vector3d &pos,
                        double dist) {
  std::vector<int> ids;
  auto const dist_sq = dist * dist;

  for (auto const &p : partCfg) {
    auto const r_sq = box_geo.get_mi_vector(pos, p.pos()).norm2();
    if (r_sq < dist_sq) {
      ids.push_back(p.id());
    }
  }

  return ids;
}

void calc_part_distribution(PartCfg &partCfg, std::vector<int> const &p1_types,
                            std::vector<int> const &p2_types, double r_min,
                            double r_max, int r_bins, bool log_flag,
                            double *low, double *dist) {
  int ind, cnt = 0;
  double inv_bin_width = 0.0;
  double min_dist, min_dist2 = 0.0, start_dist2;

  auto const r_max2 = Utils::sqr(r_max);
  auto const r_min2 = Utils::sqr(r_min);
  start_dist2 = Utils::sqr(r_max + 1.);
  /* bin preparation */
  *low = 0.0;
  for (int i = 0; i < r_bins; i++)
    dist[i] = 0.0;
  if (log_flag)
    inv_bin_width = (double)r_bins / (log(r_max / r_min));
  else
    inv_bin_width = (double)r_bins / (r_max - r_min);

  /* particle loop: p1_types */
  for (auto const &p1 : partCfg) {
    if (Utils::contains(p1_types, p1.type())) {
      min_dist2 = start_dist2;
      /* particle loop: p2_types */
      for (auto const &p2 : partCfg) {
        if (p1 != p2) {
          if (Utils::contains(p2_types, p2.type())) {
            auto const act_dist2 =
                box_geo.get_mi_vector(p1.pos(), p2.pos()).norm2();
            if (act_dist2 < min_dist2) {
              min_dist2 = act_dist2;
            }
          }
        }
      }
      if (min_dist2 <= r_max2) {
        if (min_dist2 >= r_min2) {
          min_dist = sqrt(min_dist2);
          /* calculate bin index */
          if (log_flag)
            ind = (int)((log(min_dist / r_min)) * inv_bin_width);
          else
            ind = (int)((min_dist - r_min) * inv_bin_width);
          if (ind >= 0 && ind < r_bins) {
            dist[ind] += 1.0;
          }
        } else {
          *low += 1.0;
        }
      }
      cnt++;
    }
  }
  if (cnt == 0)
    return;

  /* normalization */
  *low /= (double)cnt;
  for (int i = 0; i < r_bins; i++)
    dist[i] /= (double)cnt;
}

void calc_structurefactor(PartCfg &partCfg, std::vector<int> const &p_types,
                          int order, std::vector<double> &wavevectors,
                          std::vector<double> &intensities) {

  if (order < 1)
    throw std::domain_error("order has to be a strictly positive number");

  auto const order_sq = Utils::sqr(static_cast<std::size_t>(order));
  std::vector<double> ff(2 * order_sq + 1);
  auto const twoPI_L = 2 * Utils::pi() * box_geo.length_inv()[0];

  for (int i = 0; i <= order; i++) {
    for (int j = -order; j <= order; j++) {
      for (int k = -order; k <= order; k++) {
        auto const n = i * i + j * j + k * k;
        if ((static_cast<std::size_t>(n) <= order_sq) && (n >= 1)) {
          double C_sum = 0.0, S_sum = 0.0;
          for (auto const &p : partCfg) {
            if (Utils::contains(p_types, p.type())) {
              auto const qr = twoPI_L * (Utils::Vector3i{{i, j, k}} * p.pos());
              C_sum += cos(qr);
              S_sum += sin(qr);
            }
          }
          ff[2 * n - 2] += C_sum * C_sum + S_sum * S_sum;
          ff[2 * n - 1]++;
        }
      }
    }
  }

  long n_particles = 0l;
  for (auto const &p : partCfg) {
    if (Utils::contains(p_types, p.type())) {
      n_particles++;
    }
  }

  int length = 0;
  for (std::size_t qi = 0; qi < order_sq; qi++) {
    if (ff[2 * qi + 1] != 0) {
      ff[2 * qi] /= static_cast<double>(n_particles) * ff[2 * qi + 1];
      length++;
    }
  }

  wavevectors.resize(length);
  intensities.resize(length);

  int cnt = 0;
  for (std::size_t i = 0; i < order_sq; i++) {
    if (ff[2 * i + 1] != 0) {
      wavevectors[cnt] = twoPI_L * sqrt(static_cast<long>(i + 1));
      intensities[cnt] = ff[2 * i];
      cnt++;
    }
  }
}
