/*
   lperfex: A workalike of the IRIX perfex(1) command for Linux systems
   running on Intel P6 core (PPro/PII/PIII/Celeron/Xeon) processors.

   Copyright (C) 1999,2000 Ohio Supercomputer Center.

   This code is licensed under version 2 or later of the GNU GPL; see
   /usr/src/linux/COPYING for details.

   AUTHOR: Troy Baer
           Science and Technology Support
	   Ohio Supercomputer Center
	   troy@osc.edu
	   http://www.osc.edu/~troy/

   This code relies on Erik Hendricks' perf patch and library v0.7
   for the Linux kernel.  The patch is available from:

   http://www.beowulf.org/software/perf-0.7.tar.gz

   To compile:

   gcc -o lperfex lperfex.c -I/usr/local/include -L/usr/local/lib -lperf -lm

   (Assuming your sysadmin has installed libperf.a and perf.h into
   /usr/local/lib and /usr/local/include, respectively.)


   VERSION HISTORY:
   v0.1 released 01 Oct 1999; first public release.
   v0.2 released 21 Dec 1999; bugfix release.
   v0.3 released 19 Jun 2000; added features.
   v0.4 not released; internal development version.
   v0.5 released 10 Aug 2000; bugfix release.
   v1.0 released 01 Sep 2000; final libperf release.
 
   Future versions of lperfex will use will use the PerfAPI low-level interface.


   TO DO:

   * Support of more of the IRIX perfex command line options,
     especially counter multiplexing and counting inside multithreaded
     code (assuming these are even possible).  This may require moving
     away from the Hendriks perf patch and libperf to some other
     kernel-level counter interface.

   * More and better report statistics.  Many of the "interesting"
     performance metrics are difficult to do without multiplexing
     because they require 3 or more counters.

   * Further integration with PerfAPI (http://icl.cs.utk.edu/projects/papi/).
     lperfex builds against the Linux perf patches and libperf distributed
     in the current PAPI tarballs, but future versions will use the PAPI low
     level API directly.

   gcc mini-rant:

   Why the heck does gcc give warnings about the "%lf" and "%le" formats for
   doubles?  It's legal (and, more importantly, *correct*) ANSI C, fer cryin'
   out loud.  If anybody knows the answer to this, please let me know.

*/

#include <errno.h>
#include <math.h>
#include <perf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* 
   P6 core countable events:

   0: Memory references
   1: L1 data cache lines loaded
   2: L1 data cache lines loaded and modified
   3: L1 data cache lines flushed
   4: Weighed number of cycles spent waiting while a L1 data cache miss
      is resolved
   5: Instruction fetches
   6: L1 instruction cache misses
   7: ITLB misses
   8: Cycles spent waiting for instruction fetches and ITLB misses
   9: Cycles spent waiting on the instruction decoder
  10: L2 cache instruction fetches
  11: L2 cache data loads
  12: L2 cache data stores
  13: L2 cache lines loaded
  14: L2 cache lines flushed
  15: L2 cache lines loaded and modified
  16: L2 cache lines modified and flushed
  17: L2 cache requests
  18: L2 cache address strobes
  19: Cycles spent waiting on the L2 data bus
  20: Cycles spent waiting on data transfer from L2 cache to processor
  21: Cycles spent while DRDY is asserted
  22: Cycles spent while LOCK is asserted
  23: Bus requests outstanding
  24: Burst read transactions
  25: Read-for-ownership transactions
  26: Write-back transactions
  27: Instruction fetch transactions
  28: Invalidate transactions
  29: Partial-write transactions
  30: Partial transactions
  31: I/O transactions
  32: Deferred transactions
  33: Burst transactions
  34: Total number of transactions
  35: Memory transactions
  36: Bus clock cycles spent while the processor is receiving data
  37: Bus clock cycles spent while the processor is driving the BNR pin
  38: Bus clock cycles spent while the processor is driving the HIT pin
  39: Bus clock cycles spent while the processor is driving the HITM pin
  40: Cycles spent while the bus is snoop-stalled
  41: Floating point operations retired (counter 0 only)
  42: Floating point operations executed (counter 0 only)
  43: Floating point exceptions handled by microcode (counter 1 only)
  44: Multiply operations (counter 1 only)
  45: Divide operations (counter 1 only)
  46: Cycles spent doing division (counter 0 only)
  47: Store buffer blocks
  48: Store buffer drain cycles
  49: Misaligned memory references
  50: Instructions retired
  51: uOps retired
  52: Instructions decoded
  53: Hardware interrupts received
  54: Cycles spent while interrupts are disabled
  55: Cycles spent while interrupts and disabled and pending
  56: Branch instructions retired
  57: Mispredicted branches retired
  58: Taken branches retired
  59: Taken mispredicted branches retired
  60: Branch instructions decoded
  61: Branches which miss the BTB
  62: Bogus branches
  63: BACLEAR assertions
  64: Cycles spent during resource related stalls
  65: Cycles spent during partial stalls
  66: Segment register loads
  67: Cycles during which the processor is not halted
*/
int event[68] = {
  PERF_DATA_MEM_REFS,  
  PERF_DCU_LINES_IN,
  PERF_DCU_M_LINES_IN,
  PERF_DCU_M_LINES_OUT,
  PERF_DCU_MISS_STANDING,
  PERF_IFU_IFETCH,
  PERF_IFU_IFETCH_MISS,
  PERF_ITLB_MISS,
  PERF_IFU_MEM_STALL,
  PERF_ILD_STALL,
  PERF_L2_IFETCH,
  PERF_L2_LD,
  PERF_L2_ST,
  PERF_L2_LINES_IN,
  PERF_L2_LINES_OUT,
  PERF_L2_LINES_INM,
  PERF_L2_LINES_OUTM,
  PERF_L2_RQSTS,
  PERF_L2_ADS,
  PERF_L2_DBUS_BUSY,
  PERF_L2_DBUS_BUSY_RD,
  PERF_BUS_DRDY_CLOCKS,
  PERF_BUS_LOCK_CLOCKS,
  PERF_BUS_REQ_OUTSTANDING,
  PERF_BUS_TRAN_BRD,
  PERF_BUS_TRAN_RFO,
  PERF_BUS_TRANS_WB,
  PERF_BUS_TRAN_IFETCH,
  PERF_BUS_TRAN_INVAL,
  PERF_BUS_TRAN_PWR,
  PERF_BUS_TRAN_P,
  PERF_BUS_TRANS_IO,
  PERF_BUS_TRAN_DEF,
  PERF_BUS_TRAN_BURST,
  PERF_BUS_TRAN_ANY,
  PERF_BUS_TRAN_MEM,
  PERF_BUS_DATA_RCV,
  PERF_BUS_BNR_DRV,
  PERF_BUS_HIT_DRV,
  PERF_BUS_HITM_DRV,
  PERF_BUS_SNOOP_STALL,
  PERF_FLOPS,
  PERF_FP_COMP_OPS_EXE,
  PERF_FP_ASSIST,
  PERF_MUL,
  PERF_DIV,
  PERF_CYCLES_DIV_BUSY,
  PERF_LD_BLOCK,
  PERF_SB_DRAINS,
  PERF_MISALIGN_MEM_REF,
  PERF_INST_RETIRED,
  PERF_UOPS_RETIRED,
  PERF_INST_DECODER,
  PERF_HW_INT_RX,
  PERF_CYCLES_INST_MASKED,
  PERF_CYCLES_INT_PENDING_AND_MASKED,
  PERF_BR_INST_RETIRED,
  PERF_BR_MISS_PRED_RETIRED,
  PERF_BR_TAKEN_RETIRED,
  PERF_BR_MISS_PRED_TAKEN_RET,
  PERF_BR_INST_DECODED,
  PERF_BR_BTB_MISSES,
  PERF_BR_BOGUS,
  PERF_BACLEARS,
  PERF_RESOURCE_STALLS,
  PERF_PARTIAL_RAT_STALLS,
  PERF_SEGMENT_REG_LOADS,
  PERF_CPU_CLK_UNHALTED
};
char label[68][81] = {
  "Memory references",
  "L1 data cache lines loaded",
  "L1 data cache lines loaded and modified",
  "L1 data cache lines flushed",
  "Weighed number of cycles spent waiting while a L1 data cache miss is resolved",
  "Instruction fetches",
  "L1 instruction cache misses",
  "ITLB misses",
  "Cycles spent waiting for instruction fetches and ITLB misses",
  "Cycles spent waiting on the instruction decoder",
  "L2 cache instruction fetches",
  "L2 cache data loads",
  "L2 cache data stores",
  "L2 cache lines loaded",
  "L2 cache lines flushed",
  "L2 cache lines loaded and modified",
  "L2 cache lines modified and flushed",
  "L2 cache requests",
  "L2 cache address strobes",
  "Cycles spent waiting on the L2 data bus",
  "Cycles spent waiting on data transfer from L2 cache to processor",
  "Cycles spent while DRDY is asserted",
  "Cycles spent while LOCK is asserted",
  "Bus requests outstanding",
  "Burst read transactions",
  "Read-for-ownership transactions",
  "Write-back transactions",
  "Instruction fetch transactions",
  "Invalidate transactions",
  "Partial-write transactions",
  "Partial transactions",
  "I/O transactions",
  "Deferred transactions",
  "Burst transactions",
  "Total number of transactions",
  "Memory transactions",
  "Bus clock cycles spent while the processor is receiving data",
  "Bus clock cycles spent while the processor is driving the BNR pin",
  "Bus clock cycles spent while the processor is driving the HIT pin",
  "Bus clock cycles spent while the processor is driving the HITM pin",
  "Cycles spent while the bus is snoop-stalled",
  "Floating point operations retired (counter 0 only)",
  "Floating point operations executed (counter 0 only)",
  "Floating point exceptions handled by microcode (counter 1 only)",
  "Multiply operations (counter 1 only)",
  "Divide operations (counter 1 only)",
  "Cycles spent doing division (counter 0 only)",
  "Store buffer blocks",
  "Store buffer drain cycles",
  "Misaligned memory references",
  "Instructions retired",
  "uOps retired",
  "Instructions decoded",
  "Hardware interrupts received",
  "Cycles spent while interrupts are disabled",
  "Cycles spent while interrupts and disabled and pending",
  "Branch instructions retired",
  "Mispredicted branches retired",
  "Taken branches retired",
  "Taken mispredicted branches retired",
  "Branch instructions decoded",
  "Branches which miss the BTB",
  "Bogus branches",
  "BACLEAR assertions",
  "Cycles spent during resource related stalls",
  "Cycles spent during partial stalls",
  "Segment register loads",
  "Cycles during which the processor is not halted"
};

#define TIMEVAL_TO_DOUBLE(x) ((double)((x).tv_sec)+0.000001*(double)((x).tv_usec))
/* 
   The following assumes a 550 MHz clock.
   Compile with -DCLOCKSPEED=xxx to override.
*/
#define CLOCKSPEED 550

int main(int argc, char *argv[])
{
  int event0=-1, event1=-1, mplex=0, mkrpt=0, i, status;
  FILE *extfile=stdout;
  double telapsed,twall,cycles;
  clock_t tstart,tend;
  struct rusage ru;
  struct tms timesbuf;
  pid_t child;
  unsigned long long counter[PERF_COUNTERS];
  extern char *optarg;
  extern int optind;
  extern int errno;
  char hostname[80];
  
  /* Parse command line options */
  gethostname(hostname,80);
  extfile=stdout;
  while ((i=getopt(argc,argv,"ae:kmpo:sxy-"))!=-1)
    {
      switch(i)
	{
	case 'a':
	  mplex=1;
	  
	  break;
	case 'e':
	  mplex=0;
	  if (event0==-1)
	    {
	      sscanf(optarg,"%d",&event0);
	    }
	  else
	    {
	      sscanf(optarg,"%d",&event1);
	    }
	  break;
	case 'k':
	  fprintf(stderr,"lperfex:  Kernel counting not supported\n");
	  break;
	case 'm':
	  fprintf(stderr,"lperfex:  Multithreaded counting not supported\n");
	  break;
	case 'o':
	  extfile=fopen(optarg,"w");
	  break;
	case 's':
	  fprintf(stderr,"lperfex:  Signalled counting no supported\n");
	  break;
	case 'x':
	  fprintf(stderr,"lperfex:  Exception counting no supported\n");
	  break;
	case 'y':
	  mkrpt=1;
	  break;
	case '-':
	  break;
	  break;
	case '?':
	case 'h':
	  printf("Usage:  lperfex [-e event0 [-e event1]] [-y] [-o file] [--] command [command args]\n");
	  printf("\nEvent numbers: \n");
	  for (i=0;i<68;i++)
	    printf("\t%2d:  %s\n",i,label[i]);
	  exit(-1);
	default:
	  fprintf(stderr,"lperfex:  Unrecognized option -%c\n",i);
	  break;
	}
    }

  /* Set the counters up and run the child program */
  perf_reset();
  tstart=times(&timesbuf);
  if ((child=fork())==0)
    {
      /* Child */
      perf_reset();
      if (mplex==0)
	{
	  if (event0==-1)
	    {
	      event0=42; /* Default to counting MFLOPS */
	    }
	  perf_set_config(0,event[event0]);
	  if (event1==-1)
	    {
	      event1=13; /* Default to count L2 cache line loads */
	    }
	  perf_set_config(1,event[event1]);
	}
      else
	{
	  fprintf(stderr,"lperfex:  Multiplexing of counters currently not supported\n");
	  exit(-2);
	}
      perf_start();
      if (execvp(argv[optind],&(argv[optind]))==-1)
	{
	  fprintf(stderr,"%s unable to exec -- ",argv[optind]);
	  switch(errno)
	    {
	    case EACCES:
	      fprintf(stderr,"access problem\n");
	      break;
	    case EPERM:
	      fprintf(stderr,"permissions problem\n");
	      break;
	    case E2BIG:
	      fprintf(stderr,"argument list is too long\n");
	      break;
	    case ENOEXEC:
	      fprintf(stderr,"file not executable\n");
	      break;
	    case EFAULT:
	      fprintf(stderr,"filename points outside address space\n");
	      break;
	    case ENAMETOOLONG:
	      fprintf(stderr,"filename is too long\n");
	      break;
	    case ENOENT:
	      fprintf(stderr,"file does not exist\n");
	      break;
	    case ENOMEM:
	      fprintf(stderr,"insufficient memory\n");
	      break;
	    case ENOTDIR:
	      fprintf(stderr,"part of path prefix is not a directory\n");
	      break;
	    case ELOOP:
	      fprintf(stderr,"too many symbolic links in path\n");
	      break;
	    case EIO:
	      fprintf(stderr,"I/O error\n");
	      break;
	    case ENFILE:
	      fprintf(stderr,"too many files open on system\n");
	      break;
	    case EMFILE:
	      fprintf(stderr,"too many files open by process\n");
	      break;
	    case EINVAL:
	      fprintf(stderr,"invalid executable\n");
	      break;
	    case EISDIR:
	      fprintf(stderr,"file is a directory\n");
	      break;
	    case ELIBBAD:
	      fprintf(stderr,"bad library\n");
	      break;
	    default:
	      fprintf(stderr,"unknown error\n");
	    }
	  exit(-3);
	}
    }
  else
    {
      /* Parent */
      perf_wait(child,&status,0,&ru,counter);
      perf_stop();      
      tend=times(&timesbuf);
      /* telapsed=TIMEVAL_TO_DOUBLE(ru.ru_utime)+TIMEVAL_TO_DOUBLE(ru.ru_stime);*/
      telapsed=(double)(timesbuf.tms_cutime+timesbuf.tms_cstime)/(double)CLK_TCK;
      twall=(double)(tend-tstart)/(double)CLK_TCK;
      cycles=floor((double)CLOCKSPEED*(double)1000000*telapsed);
      for (i=optind;i<argc;i++)
	{
	  fprintf(extfile,"%s",argv[i]);
	  if (i<(argc-1)) fprintf(extfile," ");
	}
      fprintf(extfile,":  %5.2lf seconds of CPU time and %5.2lf seconds of real time elapsed\n(%3.2lf%% CPU utilization) using %lf MB of memory on %s\n\n",telapsed,twall,telapsed/twall*100.,((double)ru.ru_maxrss)/1024.,hostname);
      fprintf(extfile,"Event #\t\t\tEvent\t\t\t\t\t\t\t\t\tEvents Counted\n");
      fprintf(extfile,"-------\t\t\t-----\t\t\t\t\t\t\t\t\t--------------\n");
      if (event0!=-1)
	fprintf(extfile,"   %2d  \t%-80s\t%14llu\n",event0,label[event0],counter[0]);
      if (event1!=-1)
	fprintf(extfile,"   %2d  \t%-80s\t%14llu\n",event1,label[event1],counter[1]);
      if (mkrpt)
	{
	  fprintf(extfile,"\nStatistics (averaged across all threads):\n");
	  fprintf(extfile,"---------------------------------------------\n");

	  if (event[event0]==PERF_INST_DECODER || event[event0]==PERF_INST_RETIRED)
	    fprintf(extfile,"MIPS\t\t\t%14lf\n",1.0e-6*((double)counter[0])/(double)twall);
	  else if (event[event1]==PERF_INST_DECODER || event[event1]==PERF_INST_RETIRED)
	    fprintf(extfile,"MIPS\t\t\t%14lf\n",1.0e-6*((double)counter[1])/(double)twall);

	  if (event[event0]==PERF_FLOPS || event[event0]==PERF_FP_COMP_OPS_EXE)
	    {
	      fprintf(extfile,"MFLOPS\t\t\t%14lf\n",1.0e-6*((double)counter[0])/(double)twall);
	      if (event[event1]==PERF_MUL)
		{
		  fprintf(extfile,"FP multiplications/total FP ops\t%14lf\n",((double)counter[1])/((double)counter[0]));
		}
	      else if (event[event1]==PERF_DIV)
		{
		  fprintf(extfile,"FP divisions/total FP ops\t\t%14lf\n",((double)counter[1])/((double)counter[0]));
		}
	      else if (event[event1]==PERF_INST_DECODER || event[event1]==PERF_INST_RETIRED)
		{		
		  fprintf(extfile,"Instructions/FP op\t%14lf\n",((double)counter[1])/((double)counter[0]));
		}
	      else if (event[event1]==PERF_CPU_CLK_UNHALTED)
		{
		  fprintf(extfile,"Unhalted cycles/FP op\t\t%14lf\n",((double)counter[1])/((double)counter[0]));
		}
	    }

	  if (event[event0]==PERF_CYCLES_DIV_BUSY && event[event1]==PERF_DIV)
	    fprintf(extfile,"Avg. cycles/divide op\t\t%14lf\n",((double)counter[0])/((double)counter[1]));

	  if (event[event0]==PERF_DCU_LINES_IN)
	    fprintf(extfile,"L2 cache -> L1 Dcache bandwidth\t%14lf MB/s\n",1.6E-5*((double)counter[0])/twall);
	  else if (event[event1]==PERF_DCU_LINES_IN)
	    fprintf(extfile,"L2 cache ->L1 Dcache bandwidth\t%14lf MB/s\n",1.6E-5*((double)counter[1])/twall);

	  if (event[event0]==PERF_DCU_M_LINES_OUT)
	    fprintf(extfile,"L1 Dcache -> L2 cache bandwidth\t%14lf MB/s\n",1.6E-5*((double)counter[0])/twall);
	  else if (event[event1]==PERF_DCU_M_LINES_OUT)
	    fprintf(extfile,"L1 Dcache -> L2 cache bandwidth\t%14lf MB/s\n",1.6E-5*((double)counter[1])/twall);

	  if ( (event[event0]==PERF_DCU_LINES_IN && event[event1]==PERF_DCU_M_LINES_OUT) || (event[event1]==PERF_DCU_LINES_IN && event[event0]==PERF_DCU_M_LINES_OUT) )
	    fprintf(extfile,"Total L2 <=> L1 bandwidth\t%14lf MB/s\n",1.6E-5*((double)(counter[0]+counter[1]))/twall);

	  if (event[event0]==PERF_L2_LINES_IN)
	    fprintf(extfile,"Main memory -> L2 cache bandwidth\t%14lf MB/s\n",3.2E-5*((double)counter[0])/twall);
	  else if (event[event1]==PERF_L2_LINES_IN)
	    fprintf(extfile,"Main memory -> L2 cache bandwidth\t%14lf MB/s\n",3.2E-5*((double)counter[1])/twall);

	  if (event[event0]==PERF_L2_LINES_OUT)
	    fprintf(extfile,"L2 cache -> main memory bandwidth\t%14lf MB/s\n",3.2E-5*((double)counter[0])/twall);
	  else if (event[event1]==PERF_L2_LINES_OUT)
	    fprintf(extfile,"L2 cache -> main memory bandwidth\t%14lf MB/s\n",3.2E-5*((double)counter[1])/twall);

	  if ( (event[event0]==PERF_L2_LINES_OUT && event[event1]==PERF_L2_LINES_IN) || (event[event1]==PERF_L2_LINES_OUT && event[event0]==PERF_L2_LINES_IN) )
	    fprintf(extfile,"Total memory <=> L2 bandwidth\t%14lf MB/s\n",3.2E-5*((double)(counter[0]+counter[1]))/twall);

	  if (event[event0]==PERF_DATA_MEM_REFS && event[event1]==PERF_DCU_LINES_IN)
	    fprintf(extfile,"L1 data cache hit rate\t%14lf\n",1.-((double)counter[1])/((double)counter[0]));
	  else if (event[event1]==PERF_DATA_MEM_REFS && event[event0]==PERF_DCU_LINES_IN)
	    fprintf(extfile,"L1 data cache hit rate\t%14lf\n",1.-((double)counter[0])/((double)counter[1]));

	  if (event[event0]==PERF_L2_ADS && event[event1]==PERF_L2_LINES_IN)
	    fprintf(extfile,"L2 cache hit rate\t\t%14lf\n",1.-((double)counter[1])/((double)counter[0]));
	  else if (event[event1]==PERF_L2_ADS && event[event0]==PERF_L2_LINES_IN)
	    fprintf(extfile,"L2 cache hit rate\t\t%14lf\n",1.-((double)counter[0])/((double)counter[1]));

	  if (event[event0]==PERF_DCU_LINES_IN && event[event1]==PERF_L2_LINES_IN)
	    fprintf(extfile,"L1 data cache miss/L2 cache miss\t%14lf\n",((double)counter[0])/((double)counter[1]));
	  else if (event[event1]==PERF_DCU_LINES_IN && event[event0]==PERF_L2_LINES_IN)
	    fprintf(extfile,"L1 data cache miss/L2 cache miss\t%14lf\n",((double)counter[1])/((double)counter[0]));

	  if (event[event0]==PERF_CPU_CLK_UNHALTED)
	    fprintf(extfile,"Fraction of cycles spent unhalted\t%14lf\n",((double)counter[0]/cycles));
	  else if (event[event1]==PERF_CPU_CLK_UNHALTED)
	    fprintf(extfile,"Fraction of cycles spent unhalted\t%14lf\n",((double)counter[1]/cycles));

	  if (event[event0]==PERF_DCU_MISS_STANDING)
	    fprintf(extfile,"Fraction of cycles spent waiting on L1 cache\t%14lf\n",((double)counter[0])/cycles);
	  else if (event[event1]==PERF_DCU_MISS_STANDING)
	    fprintf(extfile,"Fraction of cycles spent waiting on L1 cache\t%14lf\n",((double)counter[1])/cycles);

	  if (event[event0]==PERF_L2_DBUS_BUSY)
	    fprintf(extfile,"Fraction of cycles spent waiting on L2 data bus\t%14lf\n",((double)counter[0])/cycles);
	  else if (event[event1]==PERF_L2_DBUS_BUSY)
	    fprintf(extfile,"Fraction of cycles spent waiting on L2 data bus\t%14lf\n",((double)counter[1])/cycles);

          if (event[event0]==PERF_L2_DBUS_BUSY_RD)
	    fprintf(extfile,"Fraction of cycles spent waiting on L2 data transfers\t%14lf\n",((double)counter[0])/cycles);
	  else if (event[event1]==PERF_L2_DBUS_BUSY_RD)
	    fprintf(extfile,"Fraction of cycles spent waiting on L2 data transfers\t%14lf\n",((double)counter[1])/cycles);

          if (event[event0]==PERF_RESOURCE_STALLS)
	    fprintf(extfile,"Fraction of cycles spent on resource stalls\t%14lf\n",((double)counter[0])/cycles);
          else if (event[event1]==PERF_RESOURCE_STALLS)
	    fprintf(extfile,"Fraction of cycles spent on resource stalls\t%14lf\n",((double)counter[1])/cycles);

          if (event[event0]==PERF_PARTIAL_RAT_STALLS)
	    fprintf(extfile,"Fraction of cycles spent on partial stalls\t%14lf\n",((double)counter[0])/cycles);
          else if (event[event1]==PERF_PARTIAL_RAT_STALLS)
	    fprintf(extfile,"Fraction of cycles spent on partial stalls\t%14lf\n",((double)counter[1])/cycles);

	  if (event[event0]==PERF_BUS_SNOOP_STALL)
	    fprintf(extfile,"Fraction of cycles spent on bus snoop stalls\t%14lf\n",((double)counter[0])/cycles);
	  else if (event[event1]==PERF_BUS_SNOOP_STALL)
	    fprintf(extfile,"Fraction of cycles spent on bus snoop stalls\t%14lf\n",((double)counter[1])/cycles);
	}
    }
  return(0);
}
