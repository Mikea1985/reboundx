/**
 * Ephemeris-quality integrations
 *
 * This example uses the IAS15 integrator to integrate
 * the orbits of test particles in the field of the sun, moon, planets
 * and massive asteroids.  The positions and velocities of the
 * massive bodies are taken from JPL ephemeris files.  Solar GR
 * is included.
 *
 * To do:
 * 
 * 0. Write a wrapper function that takes an initial time, the initial positions and
 *    velocities of a set of one or more test particles, an integration time span or 
 *    final time, and time step.  The function should call the appropriate rebound
 *    routines and load the results into arrays that are accessible upon return (through
 *    pointers.  The return value of the function should represent success, failure, etc.
 * 
 * 1. Modify the code so that the initial conditions of the particle, the time
 *    span of the integration, and the time step come from a file.  We probably want to 
 *    allow the user to specific barycentric or geocentric. DONE.
 * 
 * 2. Rearrange the ephem() function so that it returns all the positions in one shot.
 * 
 * 3. Check position of the moon.  DONE.
 * 
 * 4. Separate ephem() and ast_ephem() from the rest of ephemeris_forces code.  DONE.
 * 
 * 5. Streamline ephem() function.  DONE.
 * 
 * 6. Put in earth J2 and J4.  DONE.  Could put in the orientation of the spin 
 *    axis.  Can't include both J2/J4 of earth and sun without this.  DONE.
 * 
 * 7. Put in J2 of the sun.  This will require thinking about the orientation of the spin 
 *    axis.  DONE.
 *
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "rebound.h"
#include "reboundx.h"

/**
 * @brief Struct containing pointers to intermediate values
 */
struct reb_dpconst7 {
    double* const restrict p0;  ///< Temporary values at intermediate step 0 
    double* const restrict p1;  ///< Temporary values at intermediate step 1 
    double* const restrict p2;  ///< Temporary values at intermediate step 2 
    double* const restrict p3;  ///< Temporary values at intermediate step 3 
    double* const restrict p4;  ///< Temporary values at intermediate step 4 
    double* const restrict p5;  ///< Temporary values at intermediate step 5 
    double* const restrict p6;  ///< Temporary values at intermediate step 6 
};

static struct reb_dpconst7 dpcast(struct reb_dp7 dp){
    struct reb_dpconst7 dpc = {
        .p0 = dp.p0, 
        .p1 = dp.p1, 
        .p2 = dp.p2, 
        .p3 = dp.p3, 
        .p4 = dp.p4, 
        .p5 = dp.p5, 
        .p6 = dp.p6, 
    };
    return dpc;
}

typedef struct {
  double t, x, y, z, vx, vy, vz, ax, ay, az;
} tstate;

// Gauss Radau spacings
static const double h[9]    = { 0.0, 0.0562625605369221464656521910318, 0.180240691736892364987579942780, 0.352624717113169637373907769648, 0.547153626330555383001448554766, 0.734210177215410531523210605558, 0.885320946839095768090359771030, 0.977520613561287501891174488626, 1.0};

int integration_function(double tstart, double tstep, double trange, int geocentric,
			  double xi, double yi, double zi, double vxi, double vyi, double vzi,
			  tstate *outstate, int* n_out){

    void store_function(struct reb_simulation* r, tstate* outstate, tstate last, int n_out);
    struct reb_simulation* r = reb_create_simulation();
    

    // Set up simulation constants
    r->G = 0.295912208285591100E-03; // Gravitational constant (AU, solar masses, days)
    r->integrator = REB_INTEGRATOR_IAS15;
    r->heartbeat = NULL;
    r->display_data = NULL;
    r->collision = REB_COLLISION_DIRECT;
    r->collision_resolve = reb_collision_resolve_merge;
    r->gravity = REB_GRAVITY_NONE;
    //r->usleep = 20000.;
    
    int nsteps = fabs(trange/tstep) + 1;
    int nouts = 8*fabs(trange/tstep) + 1;

    double* times = malloc(nouts*sizeof(double));

    for(int i=0; i<nsteps; i++){
      times[i] = tstart + i*tstep;
    }

    struct rebx_extras* rebx = rebx_attach(r);

    // Also add "ephemeris_forces" 
    struct rebx_force* ephem_forces = rebx_load_force(rebx, "ephemeris_forces");
    rebx_add_force(rebx, ephem_forces);

    rebx_set_param_int(rebx, &ephem_forces->ap, "geocentric", geocentric);

    // Set number of ephemeris bodies
    rebx_set_param_int(rebx, &ephem_forces->ap, "N_ephem", 11);

    // Set number of massive asteroids
    rebx_set_param_int(rebx, &ephem_forces->ap, "N_ast", 16);

    // Set speed of light in right units (set by G & initial conditions).
    // Here we use default units of AU/(yr/2pi)
    rebx_set_param_double(rebx, &ephem_forces->ap, "c", 173.144632674);

    rebx_set_param_int(rebx, &ephem_forces->ap, "n_out", 0);
    rebx_set_param_pointer(rebx, &ephem_forces->ap, "outstate", outstate);

    struct reb_particle tp = {0};

    tp.x  =  xi;
    tp.y  =  yi;
    tp.z  =  zi;
    tp.vx =  vxi;
    tp.vy =  vyi;
    tp.vz =  vzi;
    
    reb_add(r, tp);

    r->t = tstart;    // set simulation internal time to the time of test particle initial conditions.
    //tmax  = r->t + trange;
    r->dt = tstep;    // time step in days

    outstate[0].t = r->t;	
    outstate[0].x = r->particles[0].x;
    outstate[0].y = r->particles[0].y;
    outstate[0].z = r->particles[0].z;
    outstate[0].vx = r->particles[0].vx;
    outstate[0].vy = r->particles[0].vy;
    outstate[0].vz = r->particles[0].vz;

    tstate last;

    reb_integrate(r, times[0]);    
    reb_update_acceleration(r); // This should not be needed but is.
    for(int j=1; j<nsteps; j++){

      last.t = r->t;	
      last.x = r->particles[0].x;
      last.y = r->particles[0].y;
      last.z = r->particles[0].z;
      last.vx = r->particles[0].vx;
      last.vy = r->particles[0].vy;
      last.vz = r->particles[0].vz;
      last.ax = r->particles[0].ax;
      last.ay = r->particles[0].ay;
      last.az = r->particles[0].az;

      reb_integrate(r, times[j]);
      //reb_step(r);
      store_function(r, outstate, last, 8*(j-1));
      reb_update_acceleration(r);

    }
    
    *n_out = (nsteps-1)*8;

    return(1);
}

void store_function(struct reb_simulation* r, tstate* outstate, tstate last, int n_out){

      int N = r->N;
      int N3 = 3*N;
      double s[9]; // Summation coefficients

      outstate[n_out].t = last.t;
      outstate[n_out].x = last.x;
      outstate[n_out].y = last.y;
      outstate[n_out].z = last.z;
      outstate[n_out].vx = last.vx;
      outstate[n_out].vy = last.vy;
      outstate[n_out].vz = last.vz;

      // Convenience variable.  The 'br' field contains the 
      // set of coefficients from the last completed step.
      const struct reb_dpconst7 b  = dpcast(r->ri_ias15.br);

      double* x0 = malloc(sizeof(double)*N3);
      double* v0 = malloc(sizeof(double)*N3);
      double* a0 = malloc(sizeof(double)*N3);

      for(int i=0;i<N;i++) {

	const int k0 = 3*i+0;
	const int k1 = 3*i+1;
	const int k2 = 3*i+2;

	x0[k0] = last.x;
	x0[k1] = last.y;
	x0[k2] = last.z;	

	v0[k0] = last.vx;
	v0[k1] = last.vy;
	v0[k2] = last.vz;	

	a0[k0] = last.ax;
	a0[k1] = last.ay;
	a0[k2] = last.az;

      }

      // Loop over interval using Gauss-Radau spacings      
      for(int n=1;n<8;n++) {                          

	s[0] = r->dt_last_done * h[n];

	s[1] = s[0] * s[0] / 2.;
	s[2] = s[1] * h[n] / 3.;
	s[3] = s[2] * h[n] / 2.;
	s[4] = 3. * s[3] * h[n] / 5.;
	s[5] = 2. * s[4] * h[n] / 3.;
	s[6] = 5. * s[5] * h[n] / 7.;
	s[7] = 3. * s[6] * h[n] / 4.;
	s[8] = 7. * s[7] * h[n] / 9.;

	// Predict positions at interval n using b values	
	for(int i=0;i<N;i++) {  
	  //int mi = i;
	  const int k0 = 3*i+0;
	  const int k1 = 3*i+1;
	  const int k2 = 3*i+2;

	  double xx0 = x0[k0] + (s[8]*b.p6[k0] + s[7]*b.p5[k0] + s[6]*b.p4[k0] + s[5]*b.p3[k0] + s[4]*b.p2[k0] + s[3]*b.p1[k0] + s[2]*b.p0[k0] + s[1]*a0[k0] + s[0]*v0[k0] );
	  double xy0 = x0[k1] + (s[8]*b.p6[k1] + s[7]*b.p5[k1] + s[6]*b.p4[k1] + s[5]*b.p3[k1] + s[4]*b.p2[k1] + s[3]*b.p1[k1] + s[2]*b.p0[k1] + s[1]*a0[k1] + s[0]*v0[k1] );
	  double xz0 = x0[k2] + (s[8]*b.p6[k2] + s[7]*b.p5[k2] + s[6]*b.p4[k2] + s[5]*b.p3[k2] + s[4]*b.p2[k2] + s[3]*b.p1[k2] + s[2]*b.p0[k2] + s[1]*a0[k2] + s[0]*v0[k2] );

	  double t = r->t + r->dt_last_done * (h[n] - 1.0);
	  
	  outstate[n_out+n].t = t;
	  outstate[n_out+n].x = xx0;
	  outstate[n_out+n].y = xy0;
	  outstate[n_out+n].z = xz0;
	  
	}

	s[0] = r->dt_last_done * h[n];
	s[1] =      s[0] * h[n] / 2.;
	s[2] = 2. * s[1] * h[n] / 3.;
	s[3] = 3. * s[2] * h[n] / 4.;
	s[4] = 4. * s[3] * h[n] / 5.;
	s[5] = 5. * s[4] * h[n] / 6.;
	s[6] = 6. * s[5] * h[n] / 7.;
	s[7] = 7. * s[6] * h[n] / 8.;

	// Predict velocities at interval n using b values	
	for(int i=0;i<N;i++) {

	  const int k0 = 3*i+0;
	  const int k1 = 3*i+1;
	  const int k2 = 3*i+2;

	  double vx0 = v0[k0] + s[7]*b.p6[k0] + s[6]*b.p5[k0] + s[5]*b.p4[k0] + s[4]*b.p3[k0] + s[3]*b.p2[k0] + s[2]*b.p1[k0] + s[1]*b.p0[k0] + s[0]*a0[k0];
	  double vy0 = v0[k1] + s[7]*b.p6[k1] + s[6]*b.p5[k1] + s[5]*b.p4[k1] + s[4]*b.p3[k1] + s[3]*b.p2[k1] + s[2]*b.p1[k1] + s[1]*b.p0[k1] + s[0]*a0[k1];
	  double vz0 = v0[k2] + s[7]*b.p6[k2] + s[6]*b.p5[k2] + s[5]*b.p4[k2] + s[4]*b.p3[k2] + s[3]*b.p2[k2] + s[2]*b.p1[k2] + s[1]*b.p0[k2] + s[0]*a0[k2];

	  outstate[n_out+n].vx = vx0;
	  outstate[n_out+n].vy = vy0;
	  outstate[n_out+n].vz = vz0;
	  
	}
	
      }

      free(x0);
      free(v0);
      free(a0);

}
