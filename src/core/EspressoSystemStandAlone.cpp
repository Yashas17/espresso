/*
 * Copyright (C) 2021-2022 The ESPResSo project
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

#include "config.hpp"

#include "EspressoSystemStandAlone.hpp"
#include "communication.hpp"
#include "grid.hpp"
#include "integrate.hpp"
#include "virtual_sites.hpp"
#include "virtual_sites/VirtualSitesOff.hpp"

#include <utils/Vector.hpp>

#include <boost/mpi.hpp>
#include <boost/mpi/environment.hpp>

#include <memory>

EspressoSystemStandAlone::EspressoSystemStandAlone(int argc, char **argv) {
  m_mpi_env = mpi_init(argc, argv);

  boost::mpi::communicator world;
  head_node = world.rank() == 0;

  // initialize the MpiCallbacks framework
  Communication::init(m_mpi_env);

  // default-construct global state of the system
#ifdef VIRTUAL_SITES
  set_virtual_sites(std::make_shared<VirtualSitesOff>());
#endif

  // initialize the MpiCallbacks loop (blocking on worker nodes)
  mpi_loop();
}

EspressoSystemStandAlone::~EspressoSystemStandAlone() {
  Communication::deinit();
}

void EspressoSystemStandAlone::set_box_l(Utils::Vector3d const &box_l) const {
  if (!head_node)
    return;
  mpi_set_box_length(box_l);
}

void EspressoSystemStandAlone::set_node_grid(
    Utils::Vector3i const &node_grid) const {
  if (!head_node)
    return;
  mpi_set_node_grid(node_grid);
}

void EspressoSystemStandAlone::set_time_step(double time_step) const {
  if (!head_node)
    return;
  mpi_set_time_step(time_step);
}

void EspressoSystemStandAlone::set_skin(double new_skin) const {
  if (!head_node)
    return;
  mpi_set_skin(new_skin);
}
