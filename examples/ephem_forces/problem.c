/**
 * Highly eccentric orbits
 *
 * This example uses the IAS15 integrator to simulate
 * a very eccentric planetary orbit. The integrator
 * automatically adjusts the timestep so that the pericentre passages
 * resolved with high accuracy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "rebound.h"
#include "reboundx.h"

double tmax;
void heartbeat(struct reb_simulation* r);
double jac0;

void ephem(const double G, const int i, const double t, double* const m,
		  double* const x, double* const y, double* const z,
		  double* const vx, double* const vy, double* const vz);


/*
static void ephem(const int i, const double t, double* const m, double* const x, double* const y, double* const z){
    const double n = 1.;
    const double mu = 1.e-3;
    const double m0 = 1.-mu;
    const double m1 = mu; 
    if (i==0){
        *m = m0;
        const double mfac = -m1/(m0+m1);
        *x = mfac*cos(n*t);
        *y = mfac*sin(n*t);
        *z = 0.;
    }

    if (i==1){
        *m = m1;
        const double mfac = m0/(m0+m1);
        *x = mfac*cos(n*t);
        *y = mfac*sin(n*t);
        *z = 0.;
    }
}

double jac(struct reb_simulation* r){
    const double n = 1;
    const struct reb_particle* p = &r->particles[0];
    const double v2 = p->vx*p->vx + p->vy*p->vy;
    const double coriolis = 2*n*(p->x*p->vy-p->y*p->vx);
    double m, x, y, z, vx, vy, vz;
    ephem(0, r->t, &m, &x, &y, &z);
    const double r1 = sqrt((x-p->x)*(x-p->x) + (y-p->y)*(y-p->y));
    const double mu1 = r->G*m;
    ephem(1, r->t, &m, &x, &y, &z);
    const double r2 = sqrt((x-p->x)*(x-p->x) + (y-p->y)*(y-p->y));
    const double mu2 = r->G*m;
    const double j = 2.*(mu1/r1 + mu2/r2) + coriolis - v2;
    return j;
}

*/


int main(int argc, char* argv[]){
    struct reb_simulation* r = reb_create_simulation();
    // Setup constants
    //r->G            = 1;        // Gravitational constant
    r->G = 0.295912208285591100E-03; // Gravitational constant (AU, solar masses, days)
    r->dt           = -0.1;      // time step in days
    r->integrator        = REB_INTEGRATOR_IAS15;
    r->heartbeat        = heartbeat;
    r->display_data = NULL;
    r->collision = REB_COLLISION_DIRECT;
    r->collision_resolve = reb_collision_resolve_merge;
    r->gravity = REB_GRAVITY_NONE;
    r->usleep = 10000.;

    r->t = 2458849.5; // set simulation internal time to the time of test particle initial conditions.

    struct reb_particle tp = {0};

    // Initial conditions for Ceres from JPL Horizons
    // Barycentric equatorial frame
    
    /* Ceres
    tp.x = 1.003810191255037E+00;
    tp.y =-2.383247476572548E+00;
    tp.z =-1.329143955066118E+00;
    tp.vx = 9.193372298255753E-03;
    tp.vy = 3.368462462504294E-03;
    tp.vz = -2.856144947515055E-04;
    */

    // Holman
    /*
    tp.x =  3.338876057509365E+00;
    tp.y =  -9.176517956664152E-01;
    tp.z = -5.038590450387491E-01;
    tp.vx = 2.805663678557796E-03;
    tp.vy = 7.550408259144305E-03;
    tp.vz = 2.980028369986096E-03;
    */

    // 2020 CD3
    tp.x = -1.743654025843151E-01;
    tp.y = 8.883058030225525E-01;
    tp.z = 3.953036087753029E-01;
    tp.vx = -1.722157767734381E-02;
    tp.vy = -2.728117293034965E-03;
    tp.vz =-1.109706882773078E-03;

    
    // Shift to geocenter
    double xe, ye, ze, vxe, vye, vze, m;
    ephem(r->G, 3, r->t, &m, &xe, &ye, &ze, &vxe, &vye, &vze); // Get position and mass of the earth wrt barycenter
    printf("%le %le %le %le %le %le\n", xe, ye, ze, vxe, vye, vze);
    tp.x -= xe;
    tp.y -= ye;
    tp.z -= ze;
    tp.vx -= vxe;
    tp.vy -= vye;
    tp.vz -= vze;
    
    reb_add(r, tp);


    struct rebx_extras* rebx = rebx_attach(r);

    // Also add "ephemeris_forces" 
    struct rebx_force* ephem_forces = rebx_load_force(rebx, "ephemeris_forces");
    rebx_add_force(rebx, ephem_forces);

    // Have to set speed of light in right units (set by G & initial conditions).  Here we use default units of AU/(yr/2pi)
    rebx_set_param_int(rebx, &ephem_forces->ap, "N_ephem", 11);
    rebx_set_param_int(rebx, &ephem_forces->ap, "N_ast", 16);
    rebx_set_param_double(rebx, &ephem_forces->ap, "c", 173.144632674);

    tmax            = r->t - 1000.;

    FILE* g = fopen("states.txt","w");
    //fprintf(g,"%lf\t",r->t);
    fclose(g);
    //reb_output_ascii(r, "states.txt");
    
    reb_integrate(r, tmax);

}

void heartbeat(struct reb_simulation* r){
    if (reb_output_check(r, 0.1)){
        reb_output_timing(r, tmax);
        reb_integrator_synchronize(r);

	double xe, ye, ze, vxe, vye, vze, m;	
	ephem(r->G, 3, r->t, &m, &xe, &ye, &ze, &vxe, &vye, &vze); // Get position and mass of the earth wrt barycenter	
        FILE* g = fopen("states.txt","a");
        fprintf(g,"%lf\t",r->t);
	const int N = r->N;	
	for (int i=0;i<N;i++){
	  struct reb_particle p = r->particles[i];
	  fprintf(g,"%e\t%e\t%e\t%e\t%e\t%e\n",p.x,p.y,p.z,p.vx,p.vy,p.vz);
	  //fprintf(g,"%e\t%e\t%e\t%e\t%e\t%e\n",
	  //p.x-xe, p.y-ye, p.z-ze, p.vx-vxe, p.vy-vye, p.vz-vze);	  
	}
        fclose(g);
	//reb_output_ascii(r, "states.txt");
    }
}


