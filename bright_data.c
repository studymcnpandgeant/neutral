#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "bright_data.h"
#include "../profiler.h"
#include "../shared.h"
#include "../params.h"

// Reads a cross section file
void read_cs_file(
    const char* filename, CrossSection* cs, Mesh* mesh);

// Initialises the set of cross sections
void initialise_cross_sections(
    BrightData* bright_data, Mesh* mesh);

// Initialises a new particle ready for tracking
void initialise_particle(
    const int global_nx, const int global_ny, const int local_nx, 
    const int local_ny, const double mesh_width, const double mesh_height, 
    const double particle_off_x, const double particle_off_y, 
    const int local_particle_nx, const int local_particle_ny, 
    const int x_off, const int y_off, const double dt, const double* edgex, 
    const double* edgey, const double initial_energy, RNPool* rn_pool,
    Particle* particle);

// Initialises all of the Bright-specific data structures.
void initialise_bright_data(
    BrightData* bright_data, Mesh* mesh, RNPool* rn_pool)
{
  const int local_nx = mesh->local_nx-2*PAD;
  const int local_ny = mesh->local_ny-2*PAD;

  bright_data->nparticles = 
    get_int_parameter("nparticles", bright_data->neutral_params_filename);
  bright_data->initial_energy = 
    get_double_parameter("initial_energy", bright_data->neutral_params_filename);

  int global_particle_nx;
  int global_particle_ny;
  int global_particle_start_x;
  int global_particle_start_y;

  const int source_location = 
    get_int_parameter("source_location", bright_data->neutral_params_filename);

  if(source_location == 0) {
    // SMALL 1/5 SQUARE AT LEFT OF MESH
    global_particle_start_x = 0;
    global_particle_start_y = (2*mesh->global_ny/10);
    global_particle_nx = (2*mesh->global_nx/10);
    global_particle_ny = (2*mesh->global_ny/10);

    if(mesh->rank == MASTER) {
      printf("Source is small square at left of mesh.\n");
    }
  }
  else if(source_location == 1) {
    // RANDOM ACROSS WHOLE MESH
    global_particle_start_x = 0;
    global_particle_start_y = 0;
    global_particle_nx = mesh->global_nx;
    global_particle_ny = mesh->global_ny;

    if(mesh->rank == MASTER) {
      printf("Source is uniformly distributed across mesh.\n");
    }
  }
  else {
    TERMINATE("The 'source_location' parameter has been set incorrectly.\n");
  }

  // Check if the start of data region is before or after our patch starts
  const int local_particle_left_off = 
    max(0, global_particle_start_x-mesh->x_off);
  const int local_particle_bottom_off = 
    max(0, global_particle_start_y-mesh->y_off);
  const int local_particle_right_off = 
    max(0, (mesh->x_off+local_nx)-(global_particle_start_x+global_particle_nx));
  const int local_particle_top_off = 
    max(0, (mesh->y_off+local_ny)-(global_particle_start_y+global_particle_ny));

  // The area of the active region shaded by this rank
  const int local_particle_nx =
    max(0, (local_nx-local_particle_left_off-local_particle_right_off));
  const int local_particle_ny = 
    max(0, (local_ny-local_particle_bottom_off-local_particle_top_off));

  bright_data->nlocal_particles = bright_data->nparticles*
    ((double)local_particle_nx*local_particle_ny)/
    ((double)global_particle_nx*global_particle_ny);

  // TODO: FIX THE OVERESTIMATED PARTICLE POPULATION MAXIMUMS
  bright_data->local_particles = 
    (Particle*)malloc(sizeof(Particle)*bright_data->nparticles*2);
  if(!bright_data->local_particles) {
    TERMINATE("Could not allocate particle array.\n");
  }

  bright_data->out_particles = 
    (Particle*)malloc(sizeof(Particle)*bright_data->nparticles);
  if(!bright_data->out_particles) {
    TERMINATE("Could not allocate particle array.\n");
  }

  allocate_data(&bright_data->scalar_flux_tally, local_nx*local_ny);
  allocate_data(&bright_data->energy_deposition_tally, local_nx*local_ny);
  for(int ii = 0; ii < local_nx*local_ny; ++ii) {
    bright_data->scalar_flux_tally[ii] = 0.0;
    bright_data->energy_deposition_tally[ii] = 0.0;
  }

  // Check we are injecting some particle into this part of the mesh
  if(global_particle_start_x+global_particle_nx >= mesh->x_off && 
      global_particle_start_x < mesh->x_off+local_nx &&
      global_particle_start_y+global_particle_ny >= mesh->y_off && 
      global_particle_start_y < mesh->y_off+local_ny) {

    inject_particles(
        mesh, local_nx, local_ny, local_particle_left_off, 
        local_particle_bottom_off, local_particle_nx, local_particle_ny, 
        bright_data->nlocal_particles, bright_data->initial_energy, 
        rn_pool, bright_data->local_particles);
  }

  initialise_cross_sections(
      bright_data, mesh);

#ifdef MPI
  // Had to initialise this in the package directly as the data structure is not
  // general enough to place in the multi-package 
  const int blocks[2] = { 8, 1 };
  MPI_Datatype types[2] = { MPI_DOUBLE, MPI_INT };
  MPI_Aint displacements[2] = { 0, blocks[0]*sizeof(double) };
  MPI_Type_create_struct(
      2, blocks, displacements, types, &particle_type);
  MPI_Type_commit(&particle_type);
#endif
}

// Acts as a particle source
void inject_particles(
    Mesh* mesh, const int local_nx, const int local_ny, 
    const int local_particle_left_off, const int local_particle_bottom_off,
    const int local_particle_nx, const int local_particle_ny, 
    const int nparticles, const double initial_energy, RNPool* rn_pool,
    Particle* particles)
{
  START_PROFILING(&compute_profile);
  for(int ii = 0; ii < nparticles; ++ii) {
    initialise_particle(
        mesh->global_nx, mesh->global_ny, local_nx, local_ny, mesh->width, 
        mesh->height, mesh->edgex[local_particle_left_off+PAD], 
        mesh->edgey[local_particle_bottom_off+PAD], local_particle_nx, 
        local_particle_ny, mesh->x_off, mesh->y_off, mesh->dt, mesh->edgex, 
        mesh->edgey, initial_energy, rn_pool, &particles[ii]);
  }
  STOP_PROFILING(&compute_profile, "initialising particles");
}

// Initialises a new particle ready for tracking
void initialise_particle(
    const int global_nx, const int global_ny, const int local_nx, 
    const int local_ny, const double mesh_width, const double mesh_height, 
    const double particle_off_x, const double particle_off_y, 
    const int local_particle_nx, const int local_particle_ny, 
    const int x_off, const int y_off, const double dt, const double* edgex, 
    const double* edgey, const double initial_energy, RNPool* rn_pool, 
    Particle* particle)
{
  // Set the initial random location of the particle inside the source region
  particle->x = particle_off_x + 
    genrand(rn_pool)*(((double)local_particle_nx/global_nx)*mesh_width);
  particle->y = particle_off_y +
    genrand(rn_pool)*(((double)local_particle_ny/global_ny)*mesh_height);

  int cellx = 0;
  int celly = 0;

  // Have to check this way as mesh doesn't have to be uniform
  for(int ii = 0; ii < local_nx; ++ii) {
    if(particle->x >= edgex[ii+PAD] && particle->x < edgex[ii+PAD+1]) {
      cellx = x_off+ii;
      break;
    }
  }
  for(int ii = 0; ii < local_ny; ++ii) {
    if(particle->y >= edgey[ii+PAD] && particle->y < edgey[ii+PAD+1]) {
      celly = y_off+ii;
      break;
    }
  }
  particle->cell = celly*global_nx+cellx;

  // Generating theta has uniform density, however 0.0 and 1.0 produce the same 
  // value which introduces very very very small bias...
  const double theta = 2.0*M_PI*genrand(rn_pool);
  particle->omega_x = cos(theta);
  particle->omega_y = sin(theta);

  // This approximation sets mono-energetic initial state for source particles  
  particle->e = initial_energy;

  // Set a weight for the particle to track absorption
  particle->weight = 1.0;
  particle->dt_to_census = dt;
  particle->mfp_to_collision = 0.0;
}

// Reads in a cross-sectional data file
void read_cs_file(
    const char* filename, CrossSection* cs, Mesh* mesh) 
{
  FILE* fp = fopen(filename, "r");
  if(!fp) {
    TERMINATE("Could not open the cross section file: %s\n", filename);
  }

  // Count the number of entries in the file
  int ch;
  cs->nentries = 0;
  while ((ch = fgetc(fp)) != EOF) {
    if(ch == '\n') {
      cs->nentries++;
    }
  }

  if(mesh->rank == MASTER) {
    printf("File %s contains %d entries\n", filename, cs->nentries);
  }

  rewind(fp);

  cs->key = (double*)malloc(sizeof(double)*cs->nentries);
  cs->value = (double*)malloc(sizeof(double)*cs->nentries);

  for(int ii = 0; ii < cs->nentries; ++ii) {
    // Skip whitespace tokens
    while((ch = fgetc(fp)) == ' ' || ch == '\n' || ch == '\r'){};

    // Jump out if we reach the end of the file early
    if(ch == EOF) {
      cs->nentries = ii;
      break;
    }

    ungetc(ch, fp);
    fscanf(fp, "%lf", &cs->key[ii]);
    while((ch = fgetc(fp)) == ' '){};
    ungetc(ch, fp);
    fscanf(fp, "%lf", &cs->value[ii]);
  }
}

// Initialises the state 
void initialise_cross_sections(
    BrightData* bright_data, Mesh* mesh)
{
  bright_data->cs_scatter_table = (CrossSection*)malloc(sizeof(CrossSection));
  bright_data->cs_absorb_table = (CrossSection*)malloc(sizeof(CrossSection));
  read_cs_file(CS_SCATTER_FILENAME, bright_data->cs_scatter_table, mesh);
  read_cs_file(CS_CAPTURE_FILENAME, bright_data->cs_absorb_table, mesh);
}

