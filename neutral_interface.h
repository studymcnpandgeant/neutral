#pragma once

#include "neutral_data.h"
#include "../mesh.h"
#include "../shared_data.h"

#ifdef __cplusplus
extern "C" {
#endif

  // Performs a solve of dependent variables for particle transport.
  void solve_transport_2d(
      const int nx, const int ny, const int global_nx, const int global_ny, 
      const int x_off, const int y_off, const double dt, const int nparticles_total,
      int* nlocal_particles, uint64_t* master_key, const int* neighbours, 
      Particles* particles, const double* density, const double* edgex, 
      const double* edgey, const double* edgedx, const double* edgedy, 
      CrossSection* cs_scatter_table, CrossSection* cs_absorb_table, 
      double* energy_deposition_tally, RNPool* rn_pools, int* reduce_array0, 
      int* reduce_array1);

  // Acts as a particle source
  void inject_particles(
      Mesh* mesh, const int local_nx, const int local_ny, 
      const double local_particle_left_off, const double local_particle_bottom_off,
      const double local_particle_width, const double local_particle_height, 
      const int nparticles, const double initial_energy, RNPool* rn_pool,
      Particles* particles);

  // Validates the results of the simulation
  void validate(
      const int nx, const int ny, const char* params_filename, 
      const int rank, double* energy_tally);

#ifdef __cplusplus
}
#endif
