#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

// general debugging of values
//#define DEBUG
// debug the simulator, using toy inputs
//#define DEBUGSIMUL
// debug the bootstrap procedure
//#define DEBUGBOOTSTRAP
// debug the linear regression
//#define DEBUGLINEARREG
// perform the simulation
#define SIMUL

// page states
#define ACTIVE 1
#define INACTIVE 2
#define NONDISC  4

// macros for faster computation
#define CHECKSTATE(st, val) (st & val)
#define SETSTATE(st, val)   (st | val)
#define RESETSTATE(st, val) (st ^ val)

// scheduling policies
#define DYN 1
#define FIFO 2
#define LIFO 3

// for debugging bootstrap
#define VECSIZE 20
#define BOOTSIZE 1000

// should be changed according to input files
#ifdef DEBUGSIMUL
#define MAXTIME 5
#define MAXPAGE 5
#define NUMDISC 3
#else
#define MAXTIME  131
#define MAXPAGE  4289710
#define NUMDISC  4289710
#define NUMDINFO 1010216
#endif

#define MAXHIST 1000
#define NUMSEED 490615
#define MAXSCHED MAXPAGE

// FSM
#define OK 1
#define NOTOK 0

// CODES
#define HTTPOK 200
#define CODEFIXED -2

typedef struct col{
  int code;
  int size;
  float change;
} col_type;

#define NUMCOL MAXTIME
#define NUMLOG MAXPAGE

typedef struct log{
  int url;
  col_type c[NUMCOL];
} log_type;

log_type *l;
//log_type l[NUMLOG] ={
//#include "in.c"
//#include "intest.c"
//};

// this vector just identifies the pages that are
// seeds in our crawling

int *seed;
//int seed[NUMSEED] = {
//#include "seedtest.c"
//#include "seed.c"
//};

// we should eventually replace the nextcollection variable
// with a smarter data structure that makes easy to detect
// just the pages to be scheduled next time and also grab
// the pages that come next in time and need to be rescheduled

typedef struct page{
  int pageid;          // page identifier
  int samples;         // current number of samples
  float vvec[MAXTIME]; // vector containing the collected values
  float tvec[MAXTIME]; // vector containing the timestamps
  int state;           // page state
  int expanded;        // have we enabled its descendance?
  float mean;          // estimated mean
  float ub;            // upperbound for mean estimation
  float lb;            // lowerbound for mean estimation
  int nextcollection;  // next collection time
  int interval;        // current collection interval
  int postponed;       // #delays since last collection
  float b1;            // latest b1 of linear regression
  float b0;            // latest b0 of linear regression
  float D;             // latest summation of the squared errors
  float r2;            // latest determination coefficient
  float realmean;      // mean considering collected points so far
  int szreal;          // number of collected points so far
} page_t;

page_t *pagevec;

// buffer of discovered pages is a single vector
// of integers. for each page we have a list of
// discovered pages.


typedef struct discinfo {
  int pageid;  // page id associated with this info
  int start;   // starting position in the buffer
  int end;     // ending position in the buffer, end=start=-1, empty
  int cursor;  // last page returned
} discinfo_t;

discinfo_t *dinfo;
//discinfo_t dinfo[MAXPAGE] = {
//#include "dinfotest.c"
//#include "dinfo.c"
//};

int *discpages;
//int discpages[NUMDISC] = {
////#include "disctest.c"
////#include "disc.c"
//};

void read_input(char *report, char *dinfo_file, char *disc, char *seeds){
  int i, j;
  FILE *f;
  col_type *temp;
  discinfo_t *d_t;

  f = fopen(report, "r");

  for(i = 0; i < MAXPAGE; i++){
    for(j = 0; j < MAXTIME; j++){
      temp = &l[i].c[j];
      fscanf(f, "%d %d %f", &temp->code, &temp->size, &temp->change);
      temp->change = temp->change*100;
    }
  }
  fclose(f);

  f = fopen(disc, "r");

  for(i = 0; i < NUMDISC; i++){
    fscanf(f, "%d", &discpages[i]);
  }

  fclose(f);

  f = fopen(seeds, "r");

  for(i = 0; i < NUMSEED; i++){
    fscanf(f, "%d", &seed[i]);
  }

  fclose(f);

  f = fopen(dinfo_file, "r");

  for(i = 0; i < NUMDINFO; i++) {
    d_t = &dinfo[i];
    fscanf(f, "%d %d %d %d", &d_t->pageid, &d_t->start, &d_t->end,
        &d_t->cursor);
  }

  fclose(f);
}

int locatediscinfo(int pageid){
  int i;
  // should exchange for a binary search
  for (i=0; i<MAXPAGE; i++){
    if (pageid == dinfo[i].pageid) return i;
  }
  return -1;
}

int getfirst(int pageid){

  if (pageid==-1) return -1;
  int pos = locatediscinfo(pageid);

  if (dinfo[pos].end!=-1){
    dinfo[pos].cursor = dinfo[pos].start;
#ifdef DEBUGSIMUL
    fprintf(stderr,"pageid %d pos %d end %d cursor %d\n",
        pageid, pos, dinfo[pos].end, dinfo[pos].cursor);
#endif
    return discpages[dinfo[pos].cursor];
  }
  return -1;  // no pages to return
}

int getnext(int pageid){

  if (pageid==-1) return -1;
  int pos = locatediscinfo(pageid);

#ifdef DEBUGSIMUL
  getchar();
  fprintf(stderr,"pageid %d pos %d end %d cursor %d\n",
      pageid, pos, dinfo[pos].end, dinfo[pos].cursor);

#endif

  if ((dinfo[pos].end!=-1) &&
      (dinfo[pos].cursor<dinfo[pos].end)){
    dinfo[pos].cursor++;
    if (dinfo[pos].cursor<=dinfo[pos].end)
      return discpages[dinfo[pos].cursor];
  }
  return -1;  // no pages to return
}

// data structure and function for scheduling

typedef struct sched{
  int pageid;         // page identifier
  float schedval;     // sched val
  int nextcollection; // current schedule time
  int postponed;      // number of postponed executions
} sched_t;

static int cmpsched(const void * p1, const void *p2){
  sched_t *f1 = (sched_t *)(p1);
  sched_t *f2 = (sched_t *)(p2);
  if ((*f1).schedval<(*f2).schedval) return -1;
  else if ((*f1).schedval>(*f2).schedval) return 1;
  else return 0;
}

// utilities

void print_log(FILE * out){
  int i,j;
  for (i=0;i<NUMLOG;i++){
    fprintf(out,"%d ",l[i].url);
    for (j=0; j<NUMCOL; j++){
      fprintf(out,"%d %d %.2f ",
          l[i].c[j].code, l[i].c[j].size, l[i].c[j].change);
    }
    fprintf(out,"\n");
  }
}

int chkconsistency(){
  int i,j,numerror=0;
  for (i=0;i<NUMLOG;i++){
    for (j=0; j<NUMCOL; j++){
      // 200 subsumes a size>0
      if (l[i].c[j].code == 200){
        if ((l[i].c[j].size <1) || (l[i].c[j].change<0)){
          l[i].c[j].code = CODEFIXED;
          numerror ++;
        }
      }
    }
  }
  return numerror;
}

void usage(){
  fprintf(stderr,"DynWebStats simulator\n");
  fprintf(stderr,"\t-s [d|f|l]        (scheduling policy)\n");
  fprintf(stderr,"\t   d              (dynamic scheduling)\n");
  fprintf(stderr,"\t   f              (fifo scheduling)\n");
  fprintf(stderr,"\t   l              (lifo scheduling)\n");
  fprintf(stderr,"\t-c <capacity>     (page download capacity)\n");
  fprintf(stderr,"\t-f <factor>       (dynamic scheduling factor)\n");
  fprintf(stderr,"\t-t <threshold>    (page similarity threshold)\n");
  fprintf(stderr,"\t-o <filename>     (output file)\n");
}

// global variables that control simulation execution
int schedpolicy=-1,capacity=-1, schedulingfactor=-1;
float similaritythr=-1.0;
char * outputfilename = (char*)0;

void parse_args(argc,argv)
  int argc;
  char ** argv;
{
  extern char * optarg;
  int c,option;
  while ((c = getopt(argc,argv,"s:c:f:t:o:h")) != EOF)
    switch(c) {
      case 's': // scheduling policy
        option = optarg[0];
        switch(option){
          case 'd': schedpolicy=DYN; break;
          case 'f': schedpolicy=FIFO; break;
          case 'l': schedpolicy=LIFO; break;
          default: usage(); exit(1);  break;
        }
        break;
      case 'c': // download capacity
        capacity = atoi(optarg);
        break;
      case 'f': // scheduling factor
        schedulingfactor = atoi(optarg);
        break;
      case 't': // page similarity threshold
        similaritythr = atof(optarg);
        break;
      case 'o': // page similarity threshold
        outputfilename = optarg;
        break;
      case 'h':
      default:
        usage();
        exit(1);
        break;
    }
  if ((schedpolicy==-1) || (capacity==-1) ||
      (schedulingfactor==-1) || (similaritythr<0.0) ||
      (outputfilename == (char*)0)){
    usage();
    exit(1);
  }
}

int stat(){
  int i,j,numerror=0;
  int histchange[1001];
  for (i=0;i<1001; i++) histchange[i] = 0;
  for (i=0;i<NUMLOG;i++){
    for (j=0; j<NUMCOL; j++){
      if (l[i].c[j].code == 200){
        histchange[(int)(l[i].c[j].change*1000)] ++;
      } else {
        numerror ++;
      }
    }
  }
#ifdef DEBUGSIMUL
  for (i=0; i<1001; i++){
    if(histchange[i]) fprintf(stderr,"h[%d] %d\n",i,histchange[i]);
  }
  fprintf(stderr,"numerror %d\n",numerror);
#endif
  return numerror;
}

void initpage(){
  int i;

  for (i=0; i<MAXPAGE; i++){
    pagevec[i].pageid = i;  // Double check! It assumes contiguous keys
    pagevec[i].samples = 0;
    pagevec[i].state = NONDISC;
    pagevec[i].expanded = 0;
    pagevec[i].mean = pagevec[i].ub = pagevec[i].lb = 0.0;
    pagevec[i].nextcollection = 0;
    pagevec[i].interval = 1;
    pagevec[i].szreal = 0;
  }
}

void initseed(){
  int i;

  for (i=0; i<NUMSEED; i++)
    pagevec[seed[i]].state = INACTIVE;
}

// bootstrap functions

int bootstrap(int n, int numboot, float * invec, float ** outmat){
  // generate numboot bootstraps from invec to outvec
  int i, j, pos;
  for (i=0; i<numboot; i++){
    for (j=0; j<n; j++){
      pos = random()%n;
      outmat[i][j] = invec[pos];
    }
  }
#ifdef DEBUG
  for (i=0; i<numboot; i++){
    for (j=0; j<n; j++){
      printf(" %f", outmat[i][j]);
    }
    printf("\n");
  }
#endif
  return numboot;
}

int bootmean(int n, int numboot, float ** inmat, float * outvec){
  // we calculate the mean of each bootstrap and store it in outvec
  int i, j;
  float sum;

  for (i=0; i<numboot; i++){
    sum = 0.0;
    for (j=0; j<n; j++){
      sum += inmat[i][j];
    }
    outvec[i] = sum/n;
#ifdef DEBUG
    printf("bootmean %d = %f\n",i,outvec[i]);
#endif
  }
  return numboot;
}

static int cmpfloat(const void * p1, const void *p2){
  float *f1 = (float*)(p1);
  float *f2 = (float*)(p2);
  if ((*f1)<(*f2)) return -1;
  else if ((*f1)>(*f2)) return 1;
  else return 0;
}


int continuousci(float p, int numboot, float * invec, float * lbound, float * ubound, float * mean){
  // given a statistic in invec and p confidence interval, we calculate the
  // expected value and its confidence interval
  int i, pos;
  float * aux = (float *) malloc(numboot*sizeof(float));
  float sum;

  // copy the vector
  for (i=0; i<numboot; i++){
    aux[i] = invec[i];
#ifdef DEBUG
    printf(" %f",aux[i]);
#endif
  }
#ifdef DEBUG
  printf("\n");
#endif

  // sort the vector
  qsort(aux, numboot, sizeof(float), cmpfloat);

#ifdef DEBUG
  for (i=0; i<numboot; i++){
    printf(" %f",aux[i]);
  }
  printf("\n");
#endif

  // determine how many positions we will discard at each end
  pos = (int) ((numboot*(1.0-p))/2+0.5);
#ifdef DEBUG
  printf("pos = %d\n",pos);
#endif

  // calculate the mean of the trimmed vector
  sum = 0;
  for (i=pos; i<numboot-pos; i++)
    sum += aux[i];

#ifdef DEBUG
  printf("sum %f\n",sum);
#endif
  *mean = sum/(numboot-2*pos);
  *ubound = aux[numboot-pos-1];
  *lbound = aux[pos];
  free(aux);
  return numboot;
}

// calculate the linear regression
// b1 = (sumxi*sumyi-n*sumxiyi)/((sumxi)^2-n*sumxi2)
// b0 = (sumyi-b1*sumxi)/n
// D = sum_1^n (yi-ui)^2
// r2 = 1-D/(sumyi2- (sumyi^2)/n
// we will contrast the estimated mean with the estimated
// regression and check the deviation.


float calclinear(float x, float b0, float b1){
  return b1*x+b0;
}

int linearreg(int size, float * xvec, float * yvec,
    float * b1, float * b0, float * D, float * r2){
  double sumxi=0.0, sumyi=0.0, sumxiyi=0.0, sumxi2=0.0, sumyi2=0.0;
  double u;
  int i;

  // no points!
  if (size == 0){
    (*b1) = (*b0) = (*D) = 0.0;
    (*r2) = 1.0;
    return size;
  }
  // just one point
  if (size == 1){
    (*b0) = yvec[0];
    (*b1) = 0.0;
    (*D) = 0.0;
    (*r2) = 1.0;
    return size;
  }

  for (i=0; i<size; i++){
    sumxi += xvec[i];
    sumyi += yvec[i];
    sumxiyi += xvec[i]*yvec[i];
    sumxi2 += xvec[i]*xvec[i];
    sumyi2 += yvec[i]*yvec[i];
  }
  (*b1) = (sumxi*sumyi-size*sumxiyi)/(sumxi*sumxi-size*sumxi2);
  (*b0) = (sumyi-(*b1)*sumxi)/size;
#ifdef DEBUGLINEARREG
  printf("sumxi %f sumyi %f sumxiyi %f sumxi2 %f sumyi2 %f b1 %f b0 %f\n",
      sumxi, sumyi, sumxiyi, sumxi2, sumyi2, (*b1), (*b0));
#endif
  (*D) = 0.0;
  for (i=0; i<size; i++){
    u = calclinear(xvec[i], (*b0), (*b1));
#ifdef DEBUGLINEARREG
    printf("i %d xi %f yi %f u %f\n",i,xvec[i], yvec[i], u);
#endif
    (*D) += (yvec[i]-u)*(yvec[i]-u);
  }
  (*r2) = 1 - (*D)/(sumyi2-sumyi*sumyi/size);
#ifdef DEBUGLINEARREG
  printf("D %f r2 %f\n",(*D),(*r2));
#endif
  return size;
}


int main (int argc, char ** argv){

  //  print_log(stdout);
  l = malloc(sizeof(log_type) * NUMLOG);
  pagevec   = malloc(sizeof(page_t) * MAXPAGE);
  dinfo     = malloc(sizeof(discinfo_t) * MAXPAGE);
  discpages = malloc(sizeof(int) * NUMDISC);
  seed      = malloc(sizeof(int) * NUMSEED);

#ifdef DEBUGLINEARREG


  float xvec[5] = {0.3, 2.7, 4.5, 5.9, 7.8};
  float yvec[5] = {1.8, 1.9, 3.1, 3.9, 3.3};
  float b1, b0, D, r2;

  linearreg(0, xvec, yvec, &b1, &b0, &D, &r2);

  printf("b0 %f b1 %f x %f y %f\n", b0, b1, 3.5, calclinear(3.5,b0,b1));

  linearreg(1, xvec, yvec, &b1, &b0, &D, &r2);

  printf("b0 %f b1 %f x %f y %f\n", b0, b1, 3.5, calclinear(3.5,b0,b1));

  linearreg(2, xvec, yvec, &b1, &b0, &D, &r2);

  printf("b0 %f b1 %f x %f y %f\n", b0, b1, 3.5, calclinear(3.5,b0,b1));

  linearreg(5, xvec, yvec, &b1, &b0, &D, &r2);

  printf("b0 %f b1 %f x %f y %f\n", b0, b1, 3.5, calclinear(3.5,b0,b1));

#endif

#ifdef DEBUGBOOTSTRAP
  int i,j;
  float in[VECSIZE], mvec[BOOTSIZE];
  float **bootout;
  float lb, ub, mean;

  for(i=0; i<VECSIZE; i++) in[i] = (float)drand48();

  bootout = (float **)malloc(BOOTSIZE*sizeof(float*));
  for(i=0; i<BOOTSIZE; i++) bootout[i] = (float *)malloc(VECSIZE*sizeof(float));

  bootstrap(VECSIZE, BOOTSIZE, in, bootout);

  bootmean(VECSIZE, BOOTSIZE, bootout, mvec);

  continuousci(0.95, BOOTSIZE, mvec, &lb, &ub, &mean);
  printf("mean %f ub %f lb %f\n",mean, lb, ub);
#endif


#ifdef SIMUL


  int t, p, i;
  float ** bootout;
  float mvec[BOOTSIZE];
  float changed;
  int size, collect, nextstate;
  int numexp,numcolsched, numcoltrial, numcolerror, numactive,numinactive;
  float summean, sumub, sumlb, sumerr, sumlin, auxsum;
  int avgcoltrial, avgcolerror, avgactive, auxcount,tp, auxp;
  int hist[MAXHIST];
  int numsched, rescheduling;
  sched_t *schedvec;

  schedvec = malloc(sizeof(sched_t) * MAXSCHED);

  parse_args(argc,argv);
  read_input("./final_report", "./dinfo.c", "./disc.c", "./seed.c");

  auxp = chkconsistency();
#ifdef DEBUGSIMUL
  fprintf(stderr,"chkconsistency %d errors\n",auxp);
#endif
  stat();

  // simulator

  // we allocate bootout just once with MAXTIME as the number of
  // samples for efficiency. each page may have a different number of
  // samples.

  bootout = (float **)malloc(BOOTSIZE*sizeof(float*));
  for(i=0; i<BOOTSIZE; i++) bootout[i] = (float *)malloc(MAXTIME*sizeof(float));

  // initialize the structure of pages
  initpage();
  initseed();
  // initialize stats variables
  avgcoltrial = avgcolerror = avgactive = 0;

  // for each timestamp
  for (t=0; t<MAXTIME; t++){
    numexp = numcolsched = numcoltrial = numcolerror = numactive = 0;
    numinactive = 0;
    summean = sumub = sumlb = sumlin = sumerr = 0.0;

    // build the scheduling vector
    // we first check which pages should be collected at t

    numsched = 0;
    for (p=0; p<MAXPAGE; p++) {
      if (pagevec[p].state != NONDISC){
        if (t==pagevec[p].nextcollection){
          schedvec[numsched].pageid = pagevec[p].pageid;
          schedvec[numsched].postponed = pagevec[p].postponed;
          schedvec[numsched].nextcollection = t;
          numsched++;
        }
      }
    }
    numcolsched = numsched;

    // we then check whether the pages to be collected fit
    // in the capacity
    if (numsched <= capacity){
      // we can collect all pages and there is capacity left,
      // we should check whether
      // there are postponed pages that may be collected,
      // we should add then to the vector
      rescheduling = 0;
      for (p=0; p<MAXPAGE; p++) {
        if (pagevec[p].state != NONDISC){
          if (pagevec[p].postponed){
            schedvec[numsched].pageid = pagevec[p].pageid;
            schedvec[numsched].postponed = pagevec[p].postponed;
            schedvec[numsched].nextcollection = pagevec[p].nextcollection;
            numsched++;
            rescheduling++;
          }
        }
      }
      // now that we considered all postponed pages, check again
      // whethere there are any to be rescheduled
      if (rescheduling>0) {
        // check whether we added more than necessary and adjust
        if (numsched > capacity){
          // sort the vector by timestamp and postpone
          // and discard the last pages. again, adjust the nextcollection
          // field and zero out the postponed
          // the sorting key is the nextcolletion*MAXTIME+postponed
          // so that the we prioritize first by nextcollection and
          // then by postpone in each timestamp
          for (p=0; p<numsched; p++){
            schedvec[p].schedval = schedvec[p].nextcollection*MAXTIME+
              schedvec[p].postponed;
          }

          // sort schedvec
          qsort(schedvec, numsched, sizeof(sched_t), cmpsched);

          // trim schedvec to just capacity
          numsched = capacity;
        }
        // reschedule the antecipated pages
        for (p=0; p<numsched; p++){
          if (schedvec[p].postponed){
            pagevec[schedvec[p].pageid].nextcollection = t;
            pagevec[schedvec[p].pageid].postponed = 0;
          }
        }
      }
      // we are now ready to collect
    } else {
      float u,outdatecost,variancecost;
      // we should postpone some pages, according to the
      // scheduling policy. they will differ just regarding
      // schedval. once set, we perform the same trimming
      //
      for (p=0; p<numsched; p++){
        switch (schedpolicy){
          case DYN:
            // we should prioritize the pages that are causing more
            // error in the estimation. error in this case is the
            // harmonic mean between the relative bootstrap range
            // (ub-mean)/mean and the relative regression deviation
            // at t, that is, we calculate the value u at t, and
            // calculate the ratio |u-mean|/mean.

            // first estimate the current value according to linear
            // regression
            u = calclinear(t,pagevec[p].b0,pagevec[p].b1);
            outdatecost = fabs(u-pagevec[p].mean)/pagevec[p].mean;
            variancecost = fabs(pagevec[p].ub-pagevec[p].mean)/
              pagevec[p].mean;
            schedvec[p].schedval = (2*outdatecost*variancecost)/
              (outdatecost+variancecost);
            break;
          case FIFO:
            // we consider just the last time it was collected
            // the earlier the page was collected, the more priority
            // it has, that is, just the last collected datum
            schedvec[p].schedval = pagevec[p].tvec[pagevec[p].samples-1];
            break;
          case LIFO:
            // we consider just the last time it was collected
            // the later the page was collected, the more priority
            // it has, that is, just the inverse of the last collection
            // timestamp
            schedvec[p].schedval = 1/pagevec[p].tvec[pagevec[p].samples-1];
            break;
          default:
            fprintf(stderr,"Invalid schedpolicy\n");
            usage();
            exit(1);
        }
      }

      // sort schedvec
      qsort(schedvec, numsched, sizeof(sched_t), cmpsched);

      // reschedule the postponed pages
      for (p=capacity; p<numsched; p++){
        auxp = schedvec[p].pageid;
        pagevec[auxp].postponed++;
        pagevec[auxp].nextcollection +=
          (pagevec[auxp].interval>1?pagevec[auxp].interval/2:1);
      }
      // trim schedvec to just capacity
      numsched = capacity;

    }


    // for each page
    for (p=0; p<MAXPAGE; p++) if (pagevec[p].state != NONDISC){

      // for each instant in time, we go over each page and
      // check which ones should be collected and which ones
      // should not. for the collected pages, we update our
      // vector.


      // should we collect the page?
      if (t==pagevec[p].nextcollection){
        // grab the result from the proper collection
        collect = (l[p].c[t].code == HTTPOK);
        changed = l[p].c[t].change;
        size = l[p].c[t].size;
#ifdef DEBUGSIMUL
        fprintf(stderr,"th %.2f t %d p %d coll %d chg %.2f sz %d\n",
            similaritythr,t,p,collect, changed,size);
#endif

        // If collect is OK, then we should check whether
        // pages should be discovered
#ifdef DEBUGSIMUL
        fprintf(stderr,"t %d p %d collect %d expanded %d\n",
            t,p, collect, pagevec[p].expanded);
#endif
        if ((collect == OK) && (pagevec[p].expanded==0)){
          int di,ppage;
          for (di = dinfo[p].start; di<=dinfo[p].end; di++) {
            ppage = discpages[di];
            if (pagevec[ppage].state == NONDISC){
              pagevec[ppage].state = INACTIVE;
              // schedule policy
              // initially just a trivial one
              pagevec[ppage].nextcollection = t+1;
              numexp++;
            }
#ifdef DEBUGSIMUL
            fprintf(stderr,"p %d di %d ppage %d \n",p,di,ppage);
#endif
          }
          pagevec[p].expanded = 1;
        }

        // record in the page history
        // evolve the FSM
        if ((pagevec[p].state == INACTIVE) && (collect == NOTOK)){
          pagevec[p].interval *= schedulingfactor;
          nextstate = INACTIVE;
        }
        if ((pagevec[p].state == INACTIVE) && (collect == OK)){
          if (pagevec[p].interval > 1)
            pagevec[p].interval /= schedulingfactor;
          nextstate = ACTIVE;
        }
        if ((pagevec[p].state == ACTIVE) && (collect == NOTOK)){
          if (pagevec[p].interval > 1)
            pagevec[p].interval /= schedulingfactor;
          nextstate = INACTIVE;
        }
        if ((pagevec[p].state == ACTIVE) && (collect == OK)){
          // if changed is large enough, antecipate the collection
          if (changed > similaritythr) {
            if (pagevec[p].interval > 1){
              pagevec[p].interval /= schedulingfactor;
            }
          } else {
            // otherwise, postpone it
            pagevec[p].interval *= schedulingfactor;
          }
          nextstate = ACTIVE;
        }
        // calculate the next collection timestamp and update state
        pagevec[p].nextcollection+=pagevec[p].interval;
        pagevec[p].state = nextstate;
        if (collect == OK){
          pagevec[p].vvec[pagevec[p].samples] = size;
          pagevec[p].tvec[pagevec[p].samples] = t;
          pagevec[p].samples ++;
        } else {
          // count the number of collection errors
          numcolerror ++;
        }
        // count the number of collection trials
        numcoltrial++;
#ifdef DEBUGSIMUL
        fprintf(stderr,
            "th %.2f t %d p %d int %d nxtc %d nxts %d nce %d nct %d\n",
            similaritythr,t,p,pagevec[p].interval,
            pagevec[p].nextcollection,
            pagevec[p].state,numcolerror,numcoltrial);
#endif
      }

      // we check whether the page is active for estimating a statistic
      if(pagevec[p].state == ACTIVE){
        // we then bootstrap the page for the measure and determine,
        // for each page, the expected average measure and its bounds.

        bootstrap(pagevec[p].samples, BOOTSIZE, pagevec[p].vvec, bootout);

        bootmean(pagevec[p].samples, BOOTSIZE, bootout, mvec);

        continuousci(0.95, BOOTSIZE, mvec, &(pagevec[p].lb),
            &(pagevec[p].ub), &(pagevec[p].mean));
#ifdef DEBUG
        printf("mean %f ub %f lb %f\n",
            pagevec[p].mean, pagevec[p].lb, pagevec[p].ub);
#endif
        // calculate the number of active pages
        numactive ++;
        // calculate mean, ub, lb
        summean+= pagevec[p].mean;
        sumub += pagevec[p].ub;
        sumlb += pagevec[p].lb;

        // update the linear regression data
        linearreg(pagevec[p].samples,pagevec[p].tvec, pagevec[p].vvec,
            &(pagevec[p].b1), &(pagevec[p].b0), &(pagevec[p].D),
            &(pagevec[p].r2));

        // calculate the deviation
#ifdef DEBUGSIMUL
        fprintf(stderr,"sumlin: t %d p %d b1 = %f b0 %f reg %f mean %f\n",
            t, p, pagevec[p].b1, pagevec[p].b0,
            calclinear(t, pagevec[p].b0, pagevec[p].b1),
            pagevec[p].mean);
#endif
        sumlin +=
          fabs(calclinear(t, pagevec[p].b0, pagevec[p].b1)-pagevec[p].mean)/
          pagevec[p].mean;

        // update the actual mean
        auxsum = 0.0;
        auxcount = 0;
        for (tp=0; tp<=t;tp++){
          collect = (l[p].c[tp].code == HTTPOK);
          size = l[p].c[tp].size;
          if (collect == 1){
            auxsum += size;
            auxcount ++;
          }
        }
        pagevec[p].szreal = auxcount;
        pagevec[p].realmean = auxsum/auxcount;
        // calculate the error wrt realmean

        sumerr += fabs(pagevec[p].realmean - pagevec[p].mean)/
          pagevec[p].mean;
#ifdef DEBUGSIMUL
        fprintf(stderr,"sumerr: t %d p %d realmean %10.2f mean %10.2f err %8.2f sumerr %8.2f\n",
            t, p, pagevec[p].realmean, pagevec[p].mean,
            fabs(pagevec[p].realmean - pagevec[p].mean)/pagevec[p].mean, sumerr);
#endif
      } else if(pagevec[p].state == INACTIVE){
        numinactive++;
      }

    }

    // we also calculate the overall summation of the average and of
    // the bounds, generating the range of summation of the set of pages.
    // calculate and print the overall summation and bounds.
    fprintf(stdout,
        "th %.2f t %d sched %d exp %d coll %d ce %d active %d inactive %d %.2f %.2f - %.2f lin %.2f error %.2f\n",
        similaritythr,t,numcolsched, numexp, numcoltrial,
        numcolerror,numactive, numinactive,
        summean/numactive,sumlb/numactive,sumub/numactive,
        sumlin/numactive,sumerr/numactive);
    avgcoltrial += numcoltrial;
    avgcolerror += numcolerror;
    avgactive += numactive;
  }
#ifdef DEBUGSIMUL
  fprintf(stdout,"th %f ct %.2f ce %.2f act %.2f\n",
      similaritythr, avgcoltrial*1.0/MAXTIME,
      avgcolerror*1.0/MAXTIME, avgactive*1.0/MAXTIME);
#endif
  // analyze the intervals for each page.
  for (i=0; i<MAXHIST; i++) hist[i] = 0;
  for (p=0; p<MAXPAGE; p++) hist[pagevec[p].interval]++;
#ifdef DEBUGSIMUL
  for (i=0; i<MAXHIST; i++)
    if (hist[i]) fprintf(stdout,"th %f histint %d %d\n",
        similaritythr,i, hist[i]);
#endif
  // analyze the number of samples
  for (i=0; i<MAXHIST; i++) hist[i] = 0;
  for (p=0; p<MAXPAGE; p++) hist[pagevec[p].samples]++;
#ifdef DEBUGSIMUL
  for (i=0; i<MAXHIST; i++)
    if (hist[i]) fprintf(stdout,"th %f histsample %d %d\n",
        similaritythr,i, hist[i]);
#endif
#endif

  return 0;
}
