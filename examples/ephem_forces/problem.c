#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "rebound.h"
#include "reboundx.h"

typedef struct {
  double t, x, y, z, vx, vy, vz, ax, ay, az;
} tstate;

typedef struct {
    double* t;
    double* state;
    int n_out;
    int n_particles;
} timestate;

void read_inputs(char *filename, double* tstart, double* tstep, double* trange,
		 int *geocentric,
		 double **instate,
		 int *n_particles);

int main(int argc, char* argv[]){

    timestate ts;

    double *instate;    
    double* outstate;
    double* outtime;
    
    int n_out;

    // Read ICs & integration params from file
    double tstart, tstep, trange;
    int geocentric;
    int n_particles;

    if(argc >=2){
	read_inputs(argv[1], &tstart, &tstep, &trange, &geocentric, &instate, &n_particles);
    }else{
	read_inputs("initial_conditions.txt", &tstart, &tstep, &trange, &geocentric, &instate, &n_particles);
    }

    int integration_function(double tstart, double tstep, double trange,
			     int geocentric,
			     int n_particles,
			     double* instate,
			     timestate *ts);
    
    integration_function(tstart, tstep, trange,
			 geocentric,
			 n_particles,
			 instate,
			 &ts);

    n_out = ts.n_out;
    outtime = ts.t;
    outstate = ts.state;

    // clearing out the file
    FILE* g = fopen("out_states.txt","w");

    for(int i=0; i<n_out; i++){
	for(int j=0; j<n_particles; j++){
	    fprintf(g,"%lf ", outtime[i]);
	    fprintf(g,"%d ", j);
	    int offset = (i*n_particles+j)*6;
	    for(int k=0; k<6; k++){
		fprintf(g,"%16.8e ", outstate[offset+k]);
	    }
	    fprintf(g,"\n");
	}
    }
    fclose(g);    

}

void read_inputs(char *filename, double* tstart, double* tstep, double* trange,
		 int* geocentric, 
		 double **instate,
		 int* n_particles){

     char label[100]; /* hardwired for length */
     FILE* fp;

     int np = 0;

     int n_allocated = 1;
     double* state = malloc(n_allocated*6*sizeof(double)); // Clean this up.
     
     if((fp = fopen(filename, "r")) != NULL){

      while(fscanf(fp, "%s", label) != EOF){
        if(!strcmp(label, "tstart")){
	  fscanf(fp, "%lf", tstart);     
        } else if(!strcmp(label, "tstep")){
	  fscanf(fp, "%lf", tstep);
        } else if(!strcmp(label, "trange")){
	  fscanf(fp, "%lf", trange);
        } else if(!strcmp(label, "geocentric")){
         fscanf(fp, "%d", geocentric);
        } else if(!strcmp(label, "state")){
         fscanf(fp, "%lf%lf%lf", &state[6*np+0], &state[6*np+1], &state[6*np+2]);
         fscanf(fp, "%lf%lf%lf", &state[6*np+3], &state[6*np+4], &state[6*np+5]);	 
         np++;

	 // Resize the array, if needed.
	 if(np==n_allocated){
	     n_allocated *= 2;
	     state = realloc(state, n_allocated*6*sizeof(double));
	 }
	 
        } else {
         printf("No label: %s\n", label);
         exit(EXIT_FAILURE);
        }
      }

      //deallocate unused space
      state = realloc(state, np*6*sizeof(double));

      *n_particles = np;
      *instate = state;      
      
      fclose(fp);

     }else{
       exit(EXIT_FAILURE);
     }

}

