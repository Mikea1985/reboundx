/** * @file central_force.c
 * @brief   A general central force.
 * @author  Dan Tamayo <tamayo.daniel@gmail.com>
 * 
 * @section     LICENSE
 * Copyright (c) 2015 Dan Tamayo, Hanno Rein
 *
 * This file is part of reboundx.
 *
 * reboundx is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * reboundx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The section after the dollar signs gets built into the documentation by a script.  All lines must start with space * space like below.
 * Tables always must be preceded and followed by a blank line.  See http://docutils.sourceforge.net/docs/user/rst/quickstart.html for a primer on rst.
 * $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
 *
 * $Central Force$       // Effect category (must be the first non-blank line after dollar signs and between dollar signs to be detected by script).
 *
 * ======================= ===============================================
 * Authors                 D. Tamayo
 * Implementation Paper    *In progress*
 * Based on                None
 * C Example               :ref:`c_example_central_force`
 * Python Example          `CentralForce.ipynb <https://github.com/dtamayo/reboundx/blob/master/ipython_examples/CentralForce.ipynb>`_.
 * ======================= ===============================================
 * 
 * Adds a general central acceleration of the form a=Acentral*r^gammacentral, outward along the direction from a central particle to the body.
 * Effect is turned on by adding Acentral and gammacentral parameters to a particle, which will act as the central body for the effect,
 * and will act on all other particles.
 *
 * **Effect Parameters**
 * 
 * None
 *
 * **Particle Parameters**
 *
 * ============================ =========== ==================================================================
 * Field (C type)               Required    Description
 * ============================ =========== ==================================================================
 * Acentral (double)             Yes         Normalization for central acceleration.
 * gammacentral (double)         Yes         Power index for central acceleration.
 * ============================ =========== ==================================================================
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "rebound.h"
#include "reboundx.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// these are the body codes for the user to specify
enum {
        PLAN_BAR,                       // <0,0,0>
        PLAN_SOL,                       // Sun (in barycentric)
        PLAN_EAR,                       // Earth centre
        PLAN_EMB,                       // Earth-Moon barycentre
        PLAN_MER,                       // ... plus the rest
        PLAN_VEN,
        PLAN_MAR,
        PLAN_JUP,
        PLAN_SAT,
        PLAN_URA,
        PLAN_NEP,

        _NUM_TEST,
};

// these are array indices for the internal interface
enum {
        JPL_MER,                        // Mercury
        JPL_VEN,                        // Venus
        JPL_EMB,                        // Earth
        JPL_MAR,                        // Mars
        JPL_JUP,                        // Jupiter
        JPL_SAT,                        // Saturn
        JPL_URA,                        // Uranus
        JPL_NEP,                        // Neptune
        JPL_PLU,                        // Pluto
        JPL_LUN,                        // Moon (geocentric)
        JPL_SUN,                        // the Sun
        JPL_NUT,                        // nutations
        JPL_LIB,                        // lunar librations
        JPL_MAN,                        // lunar mantle
        JPL_TDB,                        // TT-TDB (< 2 ms)

        _NUM_JPL,
};

struct _jpl_s {
        double beg, end;                // begin and end times
        double inc;                     // time step size
        double cau;                     // definition of AU
        double cem;                     // Earth/Moon mass ratio
        int32_t num;                    // number of constants
        int32_t ver;                    // ephemeris version
        int32_t off[_NUM_JPL];          // indexing offset
        int32_t ncf[_NUM_JPL];          // number of chebyshev coefficients
        int32_t niv[_NUM_JPL];          // number of interpolation intervals
        int32_t ncm[_NUM_JPL];          // number of components / dimension
///
        size_t len, rec;                // file and record sizes
        void *map;                      // memory mapped location
};

// this stores the position+velocity
struct mpos_s {
        double u[3];                    // position vector [AU]
        double v[3];                    // velocity vector [AU/day]
        double jde;                     // TDT time [days]
};

struct _jpl_s * jpl_init(void);
int jpl_free(struct _jpl_s *jpl);
void jpl_work(double *P, int ncm, int ncf, int niv, double t0, double t1, double *u, double *v);
int jpl_calc(struct _jpl_s *jpl, struct mpos_s *now, double jde, int n, int m);

/////// private interface :


static inline void vecpos_off(double *u, const double *v, const double w)
        { u[0] += v[0] * w; u[1] += v[1] * w; u[2] += v[2] * w; }
static inline void vecpos_set(double *u, const double *v)
        { u[0] = v[0]; u[1] = v[1]; u[2] = v[2]; }
static inline void vecpos_nul(double *u)
        { u[0] = u[1] = u[2] = 0.0; }
static inline void vecpos_div(double *u, double v)
        { u[0] /= v; u[1] /= v; u[2] /= v; }

/*
 *  jpl_work
 *
 *  Interpolate the appropriate Chebyshev polynomial coefficients.
 *
 *      ncf - number of coefficients per component
 *      ncm - number of components (ie: 3 for most)
 *      niv - number of intervals / sets of coefficients
 *
 */

void jpl_work(double *P, int ncm, int ncf, int niv, double t0, double t1, double *u, double *v)
{
        double T[24], S[24];
        double t, c;
        int p, m, n, b;

        // adjust to correct interval
        t = t0 * (double)niv;
        t0 = 2.0 * fmod(t, 1.0) - 1.0;
        c = (double)(niv * 2) / t1 / 86400.0;
        b = (int)t;

        // set up Chebyshev polynomials and derivatives
        T[0] = 1.0; T[1] = t0;
        S[0] = 0.0; S[1] = 1.0;

        for (p = 2; p < ncf; p++) {
                T[p] = 2.0 * t0 * T[p-1] - T[p-2];
                S[p] = 2.0 * t0 * S[p-1] + 2.0 * T[p-1] - S[p-2];
        }

        // compute the position/velocity
        for (m = 0; m < ncm; m++) {
                u[m] = v[m] = 0.0;
                n = ncf * (m + b * ncm);

                for (p = 0; p < ncf; p++) {
                        u[m] += T[p] * P[n+p];
                        v[m] += S[p] * P[n+p] * c;
                }
        }
}
 
/*
 *  jpl_init
 *
 *  Initialise everything needed ... probaly not be compatible with a non-430 file.
 *
 */

struct _jpl_s * jpl_init(void)
{
        struct _jpl_s *jpl;
        struct stat sb;
        char buf[256];
        ssize_t ret;
        off_t off;
        int fd, p;

//      snprintf(buf, sizeof(buf), "/home/blah/wherever/linux_p1550p2650.430");
//      snprintf(buf, sizeof(buf), "linux_p1550p2650.430");
        snprintf(buf, sizeof(buf), "/Users/aryaakmal/Documents/REBOUND/rebound/reboundx/examples/ephem_forces/linux_p1550p2650.430");

        if ((fd = open(buf, O_RDONLY)) < 0)
                return NULL;

        jpl = malloc(sizeof(struct _jpl_s));
        memset(jpl, 0, sizeof(struct _jpl_s));

        if (fstat(fd, &sb) < 0)
                goto err;
        if (lseek(fd, 0x0A5C, SEEK_SET) < 0)
                goto err;

        // read header
        ret  = read(fd, &jpl->beg, sizeof(double));
        ret += read(fd, &jpl->end, sizeof(double));
        ret += read(fd, &jpl->inc, sizeof(double));
        ret += read(fd, &jpl->num, sizeof(int32_t));
        ret += read(fd, &jpl->cau, sizeof(double));
        ret += read(fd, &jpl->cem, sizeof(double));

        // number of coefficients is assumed
        for (p = 0; p < _NUM_JPL; p++)
                jpl->ncm[p] = 3;

        jpl->ncm[JPL_NUT] = 2;
        jpl->ncm[JPL_TDB] = 1;

        for (p = 0; p < 12; p++) {
                ret += read(fd, &jpl->off[p], sizeof(int32_t));
                ret += read(fd, &jpl->ncf[p], sizeof(int32_t));
                ret += read(fd, &jpl->niv[p], sizeof(int32_t));
        }

        ret += read(fd, &jpl->ver,     sizeof(int32_t));
        ret += read(fd, &jpl->off[12], sizeof(int32_t));
        ret += read(fd, &jpl->ncf[12], sizeof(int32_t));
        ret += read(fd, &jpl->niv[12], sizeof(int32_t));

        // skip the remaining constants
        off = 6 * (jpl->num - 400);

        if (lseek(fd, off, SEEK_CUR) < 0)
                goto err;

        // finishing reading
        for (p = 13; p < 15; p++) {
                ret += read(fd, &jpl->off[p], sizeof(int32_t));
                ret += read(fd, &jpl->ncf[p], sizeof(int32_t));
                ret += read(fd, &jpl->niv[p], sizeof(int32_t));
        }

        // adjust for correct indexing (ie: zero based)
        for (p = 0; p < _NUM_JPL; p++)
                jpl->off[p] -= 1;

        // save file size, and determine 'kernel size'
        jpl->len = sb.st_size;
        jpl->rec = sizeof(double) * 2;

        for (p = 0; p < _NUM_JPL; p++)
                jpl->rec += sizeof(double) * jpl->ncf[p] * jpl->niv[p] * jpl->ncm[p];

        // memory map the file, which makes us thread-safe with kernel caching
        jpl->map = mmap(NULL, jpl->len, PROT_READ, MAP_SHARED, fd, 0);

        if (jpl->map == NULL)
                goto err;

        // this file descriptor is no longer needed since we are memory mapped
        if (close(fd) < 0)
                { ; } // perror ...
        if (madvise(jpl->map, jpl->len, MADV_RANDOM) < 0)
                { ; } // perror ...

        return jpl;

err:    close(fd);
        free(jpl);

        return NULL;
}

/*
 *  jpl_free
 *
 */
int jpl_free(struct _jpl_s *jpl)
{
        if (jpl == NULL)
                return -1;

        if (munmap(jpl->map, jpl->len) < 0)
                { ; } // perror...

        memset(jpl, 0, sizeof(struct _jpl_s));
        free(jpl);
        return 0;
}
/*
 *  jpl_calc
 *
 *  Caculate the position+velocity in _equatorial_ coordinates.
 *
 */

static void _bar(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { vecpos_nul(pos->u); vecpos_nul(pos->v); }
static void _sun(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_SUN]], jpl->ncm[JPL_SUN], jpl->ncf[JPL_SUN], jpl->niv[JPL_SUN], t, jpl->inc, pos->u, pos->v); }
static void _emb(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_EMB]], jpl->ncm[JPL_EMB], jpl->ncf[JPL_EMB], jpl->niv[JPL_EMB], t, jpl->inc, pos->u, pos->v); }
static void _mer(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_MER]], jpl->ncm[JPL_MER], jpl->ncf[JPL_MER], jpl->niv[JPL_MER], t, jpl->inc, pos->u, pos->v); }
static void _ven(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_VEN]], jpl->ncm[JPL_VEN], jpl->ncf[JPL_VEN], jpl->niv[JPL_VEN], t, jpl->inc, pos->u, pos->v); }
static void _mar(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_MAR]], jpl->ncm[JPL_MAR], jpl->ncf[JPL_MAR], jpl->niv[JPL_MAR], t, jpl->inc, pos->u, pos->v); }
static void _jup(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_JUP]], jpl->ncm[JPL_JUP], jpl->ncf[JPL_JUP], jpl->niv[JPL_JUP], t, jpl->inc, pos->u, pos->v); }
static void _sat(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_SAT]], jpl->ncm[JPL_SAT], jpl->ncf[JPL_SAT], jpl->niv[JPL_SAT], t, jpl->inc, pos->u, pos->v); }
static void _ura(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_URA]], jpl->ncm[JPL_URA], jpl->ncf[JPL_URA], jpl->niv[JPL_URA], t, jpl->inc, pos->u, pos->v); }
static void _nep(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_NEP]], jpl->ncm[JPL_NEP], jpl->ncf[JPL_NEP], jpl->niv[JPL_NEP], t, jpl->inc, pos->u, pos->v); }

static void _ear(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
{
        struct mpos_s emb, lun;

        jpl_work(&z[jpl->off[JPL_EMB]], jpl->ncm[JPL_EMB], jpl->ncf[JPL_EMB], jpl->niv[JPL_EMB], t, jpl->inc, emb.u, emb.v);
        jpl_work(&z[jpl->off[JPL_LUN]], jpl->ncm[JPL_LUN], jpl->ncf[JPL_LUN], jpl->niv[JPL_LUN], t, jpl->inc, lun.u, lun.v);

        vecpos_set(pos->u, emb.u);
        vecpos_off(pos->u, lun.u, -1.0 / (1.0 + jpl->cem));

        vecpos_set(pos->v, emb.v);
        vecpos_off(pos->v, lun.v, -1.0 / (1.0 + jpl->cem));
}


// function pointers are used to avoid a pointless switch statement
static void (* _help[_NUM_TEST])(struct _jpl_s *, double *, double, struct mpos_s *)
        = { _bar, _sun, _ear, _emb, _mer, _ven, _mar, _jup, _sat, _ura, _nep};

int jpl_calc(struct _jpl_s *pl, struct mpos_s *now, double jde, int n, int m)
{
        struct mpos_s pos;
        struct mpos_s ref;
        double t, *z;
        u_int32_t blk;
        int p;

        if (pl == NULL || now == NULL)
                return -1;

        // check if covered by this file
        if (jde < pl->beg || jde > pl->end || pl->map == NULL)
                return -1;

        // compute record number and 'offset' into record
        blk = (u_int32_t)((jde - pl->beg) / pl->inc);
        t = fmod(jde - pl->beg, pl->inc) / pl->inc;
        z = pl->map + (blk + 2) * pl->rec;

        // the magick of function pointers
        _help[n](pl, z, t, &pos);
        _help[m](pl, z, t, &ref);

        for (p = 0; p < 3; p++) {
                now->u[p] = pos.u[p] - ref.u[p];
                now->v[p] = pos.v[p] - ref.v[p];
        }

        now->jde = jde;
        return 0;
}





static void ephem(const int i, const double t, double* const m, double* const x, double* const y, double* const z){
    const double n = 1.;
    const double mu = 1.e-3;
    const double m0 = 1.-mu;
    const double m1 = mu; 

    struct _jpl_s *pl;
    struct mpos_s now;
    double jde;

    if ((pl = jpl_init()) == NULL) {
            fprintf(stderr, "could not load DE430 file\n");
            exit(EXIT_FAILURE);
    }

    jde = t + 2450123.7;  // t=0 is Julian day 2450123.7 

    if (i==0){                           
        *m = m0;
//      const double mfac = -m1/(m0+m1);
//      *x = mfac*cos(n*t);
//      *y = mfac*sin(n*t);
//      *z = 0.;
        jpl_calc(pl, &now, jde, PLAN_SOL, PLAN_BAR); //sun in barycentric coords. 
        vecpos_div(now.u, pl->cau);
        *x = now.u[0];
        *y = now.u[1];
        *z = now.u[2];
    }

    if (i==1){
        *m = m1;
//      const double mfac = m0/(m0+m1);
//      *x = mfac*cos(n*t);
//      *y = mfac*sin(n*t);
//      *z = 0.;
        jpl_calc(pl, &now, jde, PLAN_JUP, PLAN_BAR); //jupiter in barycentric coords. 
        vecpos_div(now.u, pl->cau);
        *x = now.u[0];
        *y = now.u[1];
        *z = now.u[2];
    }

    if (i==2){
        *m = m1;                         //mass values need to be passed. Use m1 for all bodies for now.
//      const double mfac = m0/(m0+m1);
//      *x = mfac*cos(n*t);
//      *y = mfac*sin(n*t);
//      *z = 0.;
        jpl_calc(pl, &now, jde, PLAN_SAT, PLAN_BAR); //jupiter in barycentric coords. 
        vecpos_div(now.u, pl->cau);
        *x = now.u[0];
        *y = now.u[1];
        *z = now.u[2];
    }

    if (i==3){
        *m = m1;                         //masses values need to be passed. Use m1 for all bodies for now.
//      const double mfac = m0/(m0+m1);
//      *x = mfac*cos(n*t);
//      *y = mfac*sin(n*t);
//      *z = 0.;
        jpl_calc(pl, &now, jde, PLAN_URA, PLAN_BAR); //jupiter in barycentric coords. 
        vecpos_div(now.u, pl->cau);
        *x = now.u[0];
        *y = now.u[1];
        *z = now.u[2];
    }

    if (i==4){
        *m = m1;                         //masses values need to be passed. Use m1 for all bodies for now.
//      const double mfac = m0/(m0+m1);
//      *x = mfac*cos(n*t);
//      *y = mfac*sin(n*t);
//      *z = 0.;
        jpl_calc(pl, &now, jde, PLAN_NEP, PLAN_BAR); //jupiter in barycentric coords. 
        vecpos_div(now.u, pl->cau);
        *x = now.u[0];
        *y = now.u[1];
        *z = now.u[2];
    }

    jpl_free(pl);
}

void rebx_ephemeris_forces(struct reb_simulation* const sim, struct rebx_force* const force, struct reb_particle* const particles, const int N){
    const int* const N_ephem = rebx_get_param(sim->extras, force->ap, "N_ephem");
    if (N_ephem == NULL){
        fprintf(stderr, "REBOUNDx Error: Need to set N_ephem for ephemeris_forces\n");
        return;
    }

    const double G = sim->G;
    const double t = sim->t;
    double m, x, y, z;
    for (int i=0; i<*N_ephem; i++){
        ephem(i, t, &m, &x, &y, &z);
        for (int j=0; j<N; j++){
            const double dx = particles[j].x - x;
            const double dy = particles[j].y - y;
            const double dz = particles[j].z - z;
            const double _r = sqrt(dx*dx + dy*dy + dz*dz);
            const double prefac = G*m/(_r*_r*_r);
            //fprintf(stderr, "%e, %e, %e, %e\n", m, x, y, z);
            particles[j].ax -= prefac*dx;
            particles[j].ay -= prefac*dy;
            particles[j].az -= prefac*dz;
        }
    }
}
