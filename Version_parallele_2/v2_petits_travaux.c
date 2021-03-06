/* ------------------------------
   $Id: mandel-seq.c,v 1.2 2008/03/04 09:52:55 marquet Exp $
   ------------------------------------------------------------
   
   Affichage de l'ensemble de Mandelbrot.
   Version sequentielle.
   
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>

#include "mpi.h"
#include "string.h"

/* Valeur par defaut des parametres */
#define N_ITER  255             /* nombre d'iterations */

#define X_MIN   -1.78           /* ensemble de Mandelbrot */
#define X_MAX   0.78
#define Y_MIN   -0.961
#define Y_MAX   0.961

#define X_SIZE  2048            /* dimension image */
#define Y_SIZE  1536
#define FILENAME "mandel.ppm"   /* image resultat */
#define NAME "mandel"
#define EXTENSION ".ppm"

#define SLICENUMBERFACTOR 20
#define FINISHMESSAGE -1

typedef struct {
  short * finish;
  int   * actual_slice;
} proc_state_t;


typedef struct {  
  int x_size, y_size;           /* dimensions */
  char *pixels;         /* matrice linearisee de pixels */
} picture_t;

int self;                       /* mon rang parmi les processus */
int procs;                      /* nombre de processus */

#define ROOT 0

static void 
usage() 
{
  fprintf(stderr, "usage : ./mandel [options]\n\n");
  fprintf(stderr, "Options \t Signification \t\t Val. defaut\n\n");
  fprintf(stderr, "-n \t\t Nbre iter. \t\t %d\n", N_ITER);
  fprintf(stderr, "-b \t\t Bornes \t\t %f %f %f %f\n",
	  X_MIN, X_MAX, Y_MIN, Y_MAX);
  fprintf(stderr, "-d \t\t Dimensions \t\t %d %d\n", X_SIZE, Y_SIZE);
  fprintf(stderr, "-f \t\t Fichier \t\t %s\n", FILENAME);

  exit(EXIT_FAILURE);
}

static void 
parse_argv (int argc, char *argv[], 
            int *n_iter, 
            double *x_min, double *x_max, double *y_min, double *y_max, 
            int *x_size, int *y_size, 
            char **path) 
{
  const char *opt = "b:d:n:f:";
  int c;
    
  /* Valeurs par defaut */
  *n_iter = N_ITER;
  *x_min  = X_MIN;
  *x_max  = X_MAX;
  *y_min  = Y_MIN;
  *y_max  = Y_MAX;
  *x_size = X_SIZE;
  *y_size = Y_SIZE;
  *path   = FILENAME;

  /* Analyse arguments */
  while ((c = getopt(argc, argv, opt)) != EOF) {    
    switch (c) {      
    case 'b':           /* domaine */
      sscanf(optarg, "%lf", x_min);
      sscanf(argv[optind++], "%lf", x_max);
      sscanf(argv[optind++], "%lf", y_min);
      sscanf(argv[optind++], "%lf", y_max);
      break;
    case 'd':           /* largeur hauteur */
      sscanf(optarg, "%d", x_size);
      sscanf(argv[optind++], "%d", y_size);
      break;
    case 'n':           /* nombre d'iterations */
      *n_iter = atoi(optarg);
      break;
    case 'f':           /* fichier de sortie */
      *path = optarg;
      break;
    default:
      usage();
    }
  }  
}

static void 
init_picture (picture_t *pict, int x_size, int y_size)
{  
  pict->y_size = y_size;
  pict->x_size = x_size;
  pict->pixels = malloc(y_size * x_size); /* allocation espace memoire */
} 
static void 
init_proc_state (proc_state_t * tab, int nbProc)
{  
  tab->finish = malloc(nbProc* sizeof(short) );
  tab->actual_slice = malloc(nbProc * sizeof(int)); /* allocation espace memoire */
  memset(tab->finish, 0, sizeof(short) * nbProc);
} 

/* Enregistrement de l'image au format ASCII .ppm */
static void 
save_picture (const picture_t *pict, const char *pathname)
{  
  unsigned i;
  FILE *f = fopen(pathname, "w");  

  fprintf(f, "P6\n%d %d\n255\n", pict->x_size, pict->y_size); 
  for (i = 0 ; i < pict->x_size * pict->y_size; i++) {
    char c = pict->pixels[i];
    fprintf(f, "%c%c%c", c, c, c); /* monochrome blanc */
  }
    
  fclose (f);
}

static void
compute (picture_t *pict,
         int nb_iter,
         double x_min, double x_max, double y_min, double y_max)
{  
  int pos = 0;
  int iy, ix, i;
  double pasx = (x_max - x_min) / pict->x_size, /* discretisation */
    pasy = (y_max - y_min) / pict->y_size; 

  /* Calcul en chaque point de l'image */
  for (iy = 0 ; iy < pict->y_size ; iy++) {
    for (ix = 0 ; ix < pict->x_size; ix++) {
      double a = x_min + ix * pasx,
	b = y_max - iy * pasy,
	x = 0, y = 0;      
      for (i = 0 ; i < nb_iter ; i++) {
	double tmp = x;
	x = x * x - y * y + a;
	y = 2 * tmp * y + b;
	if (x * x + y * y > 4) /* divergence ! */
	  break; 
      }
      
      pict->pixels[pos++] = (double) i / nb_iter * 255;    
    }
  }
}

int isOver(int nbProc, short* tab) {
  int tmp = 0;
  for(tmp = 0; tmp < nbProc; tmp++){

    if(tab [tmp] == 0){
      return 0;}
  }
  return 1;
}
/* Au dé but du while l'imaage n'est pas foutu dans la structure pict. */
void masterProcess(int x_size, int y_size, char* pathname) {
  int next_slice = 0;
  int sliceNumber = (procs - 1) * SLICENUMBERFACTOR ;
  picture_t pict;
  proc_state_t state;
        
  init_picture (& pict, x_size, y_size / ((procs-1) * SLICENUMBERFACTOR) * ((procs-1) * SLICENUMBERFACTOR));
  init_proc_state(&state, procs - 1);
  for(next_slice = 0; next_slice < procs - 1; next_slice++)
    {
      MPI_Send(&next_slice, 1, MPI_INT, next_slice + 1,  0 , MPI_COMM_WORLD); 
      state.actual_slice[next_slice] = next_slice;
      state.finish [next_slice] = 0;

    }
  int variable = FINISHMESSAGE;
  MPI_Status status;
  int slave_size = y_size / ((procs-1) * SLICENUMBERFACTOR) * x_size;
  char* buffer = malloc(slave_size * sizeof(char));
 
        
  do {
    MPI_Recv(buffer, slave_size, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
    memcpy(&(pict.pixels[state.actual_slice[status.MPI_SOURCE -1]*slave_size]), buffer,sizeof(char) * slave_size);
    if(next_slice > sliceNumber - 1)
      {
	MPI_Send(&variable, 1, MPI_INT, status.MPI_SOURCE,  0 , MPI_COMM_WORLD);
	state.finish [status.MPI_SOURCE - 1] = 1;
      }
    else {
      
      MPI_Send(&next_slice, 1, MPI_INT, status.MPI_SOURCE,  0 , MPI_COMM_WORLD); 
      state.actual_slice[status.MPI_SOURCE-1] = next_slice;
      state.finish [status.MPI_SOURCE - 1] = 0;
      next_slice++; 
            
    }
        
         

  }
  while(next_slice < sliceNumber-1 || !isOver(procs - 1, state.finish));

  save_picture (& pict, pathname);

}

void slaveProcess(double x_min, double x_max, double y_min, double y_max, int x_size, int y_size, int n_iter){
  
  double x_min_rank, x_max_rank;
  double y_min_rank, y_max_rank;
  int x_size_rank, y_size_rank, pos_slice = 42;
  picture_t pict;
  MPI_Status status;
  int nbSlices = (procs-1) * SLICENUMBERFACTOR;
  x_size_rank = x_size ;
  y_size_rank = y_size / nbSlices ;
  x_min_rank = x_min;
  x_max_rank = x_max;
  init_picture (& pict, x_size_rank, y_size_rank);
        
  while(666){
    MPI_Recv(&pos_slice, 1, MPI_INT, ROOT, 0, MPI_COMM_WORLD,&status);
    if(pos_slice == FINISHMESSAGE)
      return;
    y_min_rank = y_min + ((y_max - y_min) / nbSlices  * (pos_slice + 1));
    y_max_rank = y_min_rank - ((y_max - y_min) / nbSlices);
    compute (& pict, n_iter, x_min_rank, x_max_rank, y_min_rank, y_max_rank);
    MPI_Send(pict.pixels, x_size_rank*y_size_rank, MPI_CHAR, ROOT, 0, MPI_COMM_WORLD);
  }
        
    
        
}

int main (int argc, char *argv[]) {
        
  int n_iter,                     /* degre de nettete  */  
    x_size, y_size;               /* & dimensions de l'image */  
  double x_min, x_max, y_min, y_max; /* bornes de la representation */
  char *pathname;         /* fichier destination */

  clock_t starting_time,
    ending_time;


  MPI_Init (&argc, &argv);
  MPI_Comm_size (MPI_COMM_WORLD, &procs);
  MPI_Comm_rank (MPI_COMM_WORLD, &self);
  parse_argv(argc, argv, 
	     &n_iter, 
	     &x_min, &x_max, &y_min, &y_max, 
	     &x_size, &y_size, &pathname);

  if(self == ROOT) {
    starting_time = clock();
    masterProcess(x_size, y_size, pathname);
    ending_time = clock();  
    float diff = (((float)ending_time - (float)starting_time) / 1000000.0F ) * 1000;   
    printf("Temps d'execution : %f\n",diff); 
  }
  else {
    slaveProcess(x_min, x_max, y_min, y_max, x_size, y_size, n_iter);
  }
  MPI_Finalize ();

  exit(EXIT_SUCCESS);
}
