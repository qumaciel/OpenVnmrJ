/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */

#include <sys/file.h>
#include <errno.h>
#include <strings.h>
#include <stdio.h>
#include "group.h"
#include "symtab.h"
#include "params.h"
#include "REV_NUMS.h"
#include "lc.h"
#include "asm.h"
#include "oopc.h"	/* Object Oriented Programing Package */
#include "ACQ_SUN.h"
#include "variables.h"
#include "apdelay.h"
#include "rfconst.h"

/* #define TESTING   /* use for stand alone dbx testing */

/*----------------------------------------------------------------------------
|
|	Pulse Sequence Generator  backround task envoked from parent vnmr go
|
|				Author Greg Brissey  5/13/86
|   Modified   Author     Purpose
|   --------   ------     -------
|   12/13/88   Greg B.    1. Added Code to setGflags() for new Objects, 
|			     Ext_Trig, ToLnAttn, DoLnAttn, HS_Rotor.
|			  2. Added New Parameters dpwr (replaces dhp), tpwrf, dpwrf
|			     to control the fine attenuators.
|   2/10/89   Greg B.     1. Changed QueueExp() to pass the additional information:
|			     start_elem and complete_elem to Acqproc 
|			  2. For ra the RESUME_ACQ_BIT is set in expflags.
|   4/21/89   Greg B.     1. Added New global interleave parameter (il)
|   7/13/89   Greg B.     1. Major changes to the operation of arrayed experiments
|			  2. Many functions moved to arrayfunc.c
|			  3. Hash tables used in getval(), getstr()
|			  4. acq parameters,lc, auto structs initialize in psg instead
|			     of cps.
|		          5. Updating of global acq parameter taken care of in 
|		 	     arrayfuncs.c
|			  6. new global made to reduce number of getval() or getstr()
|			     calls in arrayed experiments.
|			  7. RF setting are calc once instead of many times.
|   7/27/89   Greg B.     1. Added Frequency device parameters & functions.  GMB.
|			  2. Added 3D functions as standard instead of conditionally
|			     compiled.
|  10/25/89   Greg B.     1. Added Hung's DPS Code to PSG.  GMB.
+----------------------------------------------------------------------------*/
/* --- code type definitions, can be changed for different machines */
/*typedef char codechar;		/* 1 bytes */
/*typedef short codeint;		/* 2 bytes */
/*typedef long  codelong;		/* 4 bytes */
/*--------------------------------------------------------------------*/


#define CALLNAME 0
#define OK 0
#define FALSE 0
#define TRUE 1
#define ERROR 1
#define NOTFOUND -1
#define NOTREE -1
#define MAXSTR 256
#define MAXARYS 256
#define STDVAR 70
#define MAXVAR 10
#define MAXLOOPS 20
#define MAXVALS 20
#define MAXPARM 60
#define MAXPATHL 128

#define MAXTABLE	60	/* for table implementation (aptable.h) */
#define MAXSLICE      1024      /* maximum number of slices - SISCO */

#define MAX_RFCHAN_NUM 6

#define TODEV 1
#define DODEV 2
#define DO2DEV 3
#define DO3DEV 4

/* PSG Revision Number, test by acquisition to confirm compatiblity, 255 Max */
/* #define PSG_REV_NUM 6

/* copied from macros.h */

#define is_y(target)   ((target == 'y') || (target == 'Y'))
#define is_w(target)   ((target == 'w') || (target == 'W'))
#define is_p(target)   ((target == 'p') || (target == 'P'))
#define is_q(target)   ((target == 'q') || (target == 'Q'))
#define is_porq(target)((target == 'p') || (target == 'P') || (target == 'q') || (target == 'Q'))


#define anyrfwg ((is_y(rfwg[0])) || (is_y(rfwg[1])) || (is_y(rfwg[2])))
#define anygradwg ((is_w(gradtype[0])) || (is_w(gradtype[1])) || (is_w(gradtype[2])))
#define anypfga ((is_p(gradtype[0])) || (is_p(gradtype[1])) || (is_p(gradtype[2])))
#define anypfgw ((is_q(gradtype[0])) || (is_q(gradtype[1])) || (is_q(gradtype[2])))
#define anypfg	(anypfga || anypfgw)

#define anywg  (anyrfwg || anygradwg || anypfgw)

/* house keeping delay between CTs 13ms */
#define HKDELAY 	0.0130
/* Wideline delay for software STM funcstion  16ms/K data */
#define WLDELAY 	0.0160

#define PATERR	-1	/* now sisco usage, greg */

#ifndef VIS_ACQ
/* (2300-7fff)/2 (11903 dec), minus 1024(dec)/2 = Max size of Acodes */
/*#define VM02_SIZE 	11391*/  /* not quite so fudge factor to 11000 */
#define VM02_SIZE 	11000
#else
/* (2300-143ff)/2 (36992 dec), minus 1024(dec)/2 = Max size of Acodes */
#define VM02_SIZE 	36480
#endif

double getval();		/* create Pulse Sequence routine */


int bgflag = 0;
int debug = 0;

static int child;
static int pipe1[2];
static int pipe2[2];
static int ready = 1;

/**************************************
*  Structure for real-time AP tables  *
*  and global variables declarations  *
**************************************/

codeint  	t1, t2, t3, t4, t5, t6, 
                t7, t8, t9, t10, t11, t12, 
                t13, t14, t15, t16, t17, t18, 
                t19, t20, t21, t22, t23, t24,
                t25, t26, t27, t28, t29, t30,
                t31, t32, t33, t34, t35, t36,
                t37, t38, t39, t40, t41, t42,
                t43, t44, t45, t46, t47, t48,
                t49, t50, t51, t52, t53, t54,
                t55, t56, t57, t58, t59, t60;

struct _Tableinfo
{
   int          reset;
   int          table_number;
   int          *table_pointer;
   int          *hold_pointer;
   int          table_size;
   int          divn_factor;
   int          auto_inc;
   int          wrflag;
   codeint      indexptr;
   codeint      destptr;
};

typedef struct _Tableinfo       Tableinfo;

Tableinfo       *Table[MAXTABLE];


/**********************************************
*  End table structures and global variables  *
**********************************************/


autodata  autoparms;	/* storage for automation parameters */
Acqparams  *acqvars;	/* pointer to acquisition variable used by acq CPU */

char       **cnames;	/* pointer array to variable names */
codeint     *preCodes;	/* pointer to the start of the malloced space */
codeint     *Codes;	/* pointer to the start of the Acodes array */
long	     Codesize;	/* size of the malloc space for codes */
long	     CodeEnd;	/* End Address  of the malloc space for codes */
codeint     *Codeptr; 	/* pointer into the Acode array */
codeint     *codestadr;	/* acode start address in Codes */
int	     PSfile;
int          nnames;	/* number of variable names */
int          ntotal;	/* total number of variable names */
int          ncvals;	/* NUMBER OF VARIABLE  values */
codelong     codestart;	/* Beginning offset of a PS lc,auto struct & code */
codelong     startofAcode;	/* Beginning offset of actual Acodes */
double     **cvals;		/* pointer array to variable values */

autodata    *Aauto;     /* pointer to automation structure in Acode set */
Acqparams   *Alc;	/* pointer to low core structure in Acode set */
codeint     *Aacode; 	/* pointer into the Acode array, also Start Address */
codeint     *lc_stadr;  /* Low Core Start Address */

char rftype[MAXSTR];	/* type of rf system used for trans & decoupler */
char amptype[MAXSTR];	/* type of amplifiers used for trans & decoupler */
char rfwg[MAXSTR];	/* y/n for rf waveform generators */
char gradtype[MAXSTR];	/* char keys w - waveform generation s-sisco n-none */
char rfband[MAXSTR];	/* RF band of trans & dec  (high or low) */
double  cattn[MAX_RFCHAN_NUM+1]; /* indicates coarse attenuators for channels */
double  fattn[MAX_RFCHAN_NUM+1]; /* indicates fine attenuators for channels */

/* --- global flags --- */
int  acqiflag = 0;	/* if 'acqi' was an argument, then interactive output */
int  ok;		/* global error flag */
int  automated;		/* True if system is an automated one */
int  H1freq;		/* Proton Freq. of instrument 200,300,400,500 */
int  ptsval[MAX_RFCHAN_NUM+1];	/* PTS type for trans & decoupler */
int  rcvroff_flag = 0;	/* receiver flag; initialized to ON */
int  rcvr_hs_bit;	/* HS line that switches rcvr on/off, 0x8000 or 0x1 */
int  rfp270_hs_bit;
int  dc270_hs_bit;
int  dc2_270_hs_bit;
int  homospoil_bit;
int  spare12_hs_bits=0;
int  ap_ovrride = 0;	/* ap delay override flag; initialized to FALSE */
int  phtable_flag = 0;	/* global flag for phasetables */
int  newdec;		/* True if system uses the direct synthesis for dec */
int  newtrans; 		/* True if system uses the direct synthesis for trans */
int  newtransamp;       /* True if system uses class A amplifiers for trans */
int  newdecamp;         /* True if system uses class A amplifiers for dec */
int  vttype;		/* VT type 0=none,1=varian,2=oxford */
int  SkipHSlineTest = 0;/* Skip or not skip the HSline test agianst presHSline */
			/* need to skip this test during pulse sequence since the */
			/* usage of 'ifzero(v1)' constructs would cause the test */
			/* to fail.	*/

/* automation data */
autodata autostruct;  /* automation data structure */

/* acquisition */
char  il[MAXSTR];	/* interleaved acquisition parameter, 'y','n', or 'f#' */
double  sw; 		/* Sweep width */
double  nf;		/* For E.A.T., number of fids in Pulse sequence */
double  np; 		/* Number of data points to acquire */
double  nt; 		/* number of transients */
double  sfrq;   	/* Transmitter Frequency MHz */
double  dfrq; 		/* Decoupler Frequency MHz */
double  dfrq2; 		/* 2nd Decoupler Frequency MHz */
double  dfrq3; 		/* 3rd Decoupler Frequency MHz */
double  fb;		/* Filter Bandwidth */
double  bs;		/* Block Size */
double  tof;		/* Transmitter Offset */
double  dof;		/* Decoupler Offset */
double  dof2;		/* 2nd Decoupler Offset */
double  dof3;		/* 3rd Decoupler Offset */
double  gain; 		/* receiver gain value, or 'n' for autogain */
int     gainactive;	/* gain active parameter flag */
double  dlp; 		/* decoupler Low Power value */
double  dhp; 		/* decoupler High Power value */
double  tpwr; 		/* Transmitter pulse power */
double  tpwrf;		/* Transmitter fine linear attenuator for pulse power */
double  dpwr;		/* Decoupler pulse power */
double  dpwrf;		/* Decoupler fine linear atten for pulse power */
double  dpwrf2;		/* 2nd Decoupler fine linear atten for pulse power */
double  dpwrf3;		/* 3rd Decoupler fine linear atten for pulse power */
double  dpwr2;		/* 2nd Decoupler pulse power */
double  dpwr3;		/* 3rd Decoupler pulse power */
double  filter;		/* pulse Amp filter setting */
double	xmf;		/* transmitter decoupler modulation frequency */
double  dmf;		/* 1st decoupler modulation frequency */
double  dmf2;		/* 2nd decoupler modulation frequency */
double  dmf3;		/* 3rd decoupler modulation frequency */
double  fb;		/* filter bandwidth */
int     cpflag;  	/* phase cycling flag  1=none,  0=quad detection */
int     dhpflag;	/* decoupler High Power Flag */

    /* --- pulse widths --- */
double  pw; 		/* pulse width */
double  p1; 		/* A pulse width */
double  pw90;		/* 90 degree pulse width */
double  hst;    	/* time homospoil is active */

/* --- delays --- */
double  alfa; 		/* Time after rec is turned on that acqbegins */
double  beta; 		/* audio filter time constant */
double  d1; 		/* delay */
double  d2; 		/* A delay, used in 2D/3D/4D experiments */
double  inc2D;		/* t1 dwell time in a 2D/3D/4D experiment */
double  d3;		/* t2 delay in a 3D/4D experiment */
double  inc3D;		/* t2 dwell time in a 3D/4D experiment */
double  d4;		/* t3 delay in a 4D experiment */
double  inc4D;		/* t3 dwell time in a 4D experiment */
double  pad; 		/* Pre-acquisition delay */
int     padactive; 	/* Pre-acquisition delay active parameter flag */
double  rof1; 		/* Time receiver is turned off before pulse */
double  rof2;		/* Time after pulse before receiver turned on */

/* --- total time of experiment --- */
double  totaltime; 	/* total timer events for a fid */
double  exptime; 	/* total time for an exp duration estimate */
int   phase1;            /* 2D acquisition mode */
int   phase2;            /* 3D acquisition mode */
int   phase3;            /* 4D acquisition mode */

/* --- programmable decoupling sequences -- */
char  xseq[MAXSTR];
char  dseq[MAXSTR];
char  dseq2[MAXSTR];
char  dseq3[MAXSTR];
double xres;		/* digit resolutio prg dec */
double dres;		/* digit resolutio prg dec */
double dres2;		/* digit resolutio prg dec */ 
double dres3;		/* digit resolutio prg dec */ 


/* --- status control --- */
char  xm[MAXSTR];		/* transmitter status control */
char  xmm[MAXSTR]; 		/* transmitter modulation type control */
char  dm[MAXSTR];		/* decoupler status control */
char  dmm[MAXSTR]; 		/* decoupler modulation type control */
char  dm2[MAXSTR]; 		/* 2nd decoupler status control */
char  dm3[MAXSTR]; 		/* 3rd decoupler status control */
char  dmm2[MAXSTR]; 		/* 2nd decoupler modulation type control */
char  dmm3[MAXSTR]; 		/* 3rd decoupler modulation type control */
char  homo[MAXSTR]; 		/* first  decoupler homo mode control */
char  homo2[MAXSTR]; 		/* second decoupler homo mode control */
char  homo3[MAXSTR]; 		/* third  decoupler homo mode control */
char  hs[MAXSTR]; 		/* homospoil status control */
int   xmsize;			/* number of characters in tm */
int   xmmsize;			/* number of characters in tmm */
int   dmsize;			/* number of characters in dm */
int   dmmsize;			/* number of characters in dmm */
int   dm2size;			/* number of characters in dm2 */
int   dm3size;			/* number of characters in dm3 */
int   dmm2size;			/* number of characters in dmm2 */
int   dmm3size;			/* number of characters in dmm3 */
int   homosize;			/* number of characters in homo */
int   homo2size;		/* number of characters in homo2 */
int   homo3size;		/* number of characters in homo3 */
int   hssize; 			/* number of characters in hs */

int curfifocount;		/* current number of word in fifo */
int setupflag;			/* alias used to invoke PSG ,go=0,su=1,etc*/
int dps_flag;			/* dps flag */
int ra_flag;			/* ra flag */
unsigned long start_elem;       /* elem (FID) for acquisition to start on (RA)*/
unsigned long completed_elem;   /* total number of completed elements (FIDs)  (RA) */
int statusindx;			/* current status index */
int HSlines;			/* High Speed output board lines */
int presHSlines;		/* last output of High Speed output board lines */

/* --- Explicit acquisition parameters --- */
int hwlooping;			/* hardware looping inprogress flag */
int hwloopelements;		/* PSG elements in hardware loop */
int starthwfifocnt;		/* fifo count at start of hwloop */
int acqtriggers;			/*# of data acquires */
int noiseacquire;		/* noise acquiring flag */

/*- These values are used so that they are changed only when their values do */
int oldlkmode;			/* previous value of lockmode */
int oldspin;			/* previous value of spin */
int oldwhenshim;		/* previous value of shimming mask */
double oldvttemp;		/* previous value of vt tempature */
double oldpad;			/* previous value of pad */

/*- These values are used so that they are included in the Acode only when */
/*  the next value out of the arrayed values has been obtained */
int oldpadindex;		/* previous value of pad value index */
int newpadindex;		/* new value of pad value index */

/* --- Pulse Seq. globals --- */
double xmtrstep;		/* phase step size trans */
double decstep;			/* phase step size dec */
codeint idc = PSG_ACQ_REV;	/* PSG software rev number,(initacqparms)*/
codeint npr_ptr;		/* offset into code to np */
codeint ct;			/* offset into code to ct */
codeint oph;			/* offset into code to oph */
codeint ssval;			/* offset into code to variable ss */
codeint ssctr;			/* offset into code to variable ssct */
codeint bsval;			/* offset into code to variable bs */
/*codeint bsval;			/* offset into code to bsct */
codeint bsctr;			/* offset into code to bsct */
codeint fidctr;			/* offset into code to bsct */
codeint HSlines_ptr;		/* offset into code to squi */
codeint nsp_ptr;		/* offset into code to nsp */
codeint id2;			/* offset into code to variable id2 */
codeint id3;			/* offset into code to variable id3 */
codeint id4;			/* offset into code to variable id4 */
codeint zero;			/* offset into code to variable zero */
codeint one;			/* offset into code to variable one */
codeint two;			/* offset into code to variable two */
codeint three;			/* offset into code to variable three */
codeint v1;			/* offset into code to v1 */
codeint v2;			/* offset into code to v2 */
codeint v3;			/* offset into code to v3 */
codeint v4;			/* offset into code to v4 */
codeint v5;			/* offset into code to v5 */
codeint v6;			/* offset into code to v6 */
codeint v7;			/* offset into code to v7 */
codeint v8;			/* offset into code to v8 */
codeint v9;			/* offset into code to v9 */
codeint v10;			/* offset into code to v10 */
codeint v11;			/* offset into code to v11 */
codeint v12;			/* offset into code to v12 */
codeint v13;			/* offset into code to v13 */
codeint v14;			/* offset into code to v14 */
codeint tablert;		/* offset into code to tablert */
codeint tpwrrt;			/* offset to special tpwr */
codeint dhprt;			/* offset to special dhprt */
codeint tphsrt;			/* offset to special tphsrt */
codeint dphsrt;			/* offset to special dphsrt */

codeint ntrt;			/* offset into code to nt - mrh 21oct92 */
codeint dlvlrt;			/* offset to special dlvlrt */
/*codeint tattnrt;		/* offset to special tattnrt */
/*codeint dattnrt;		/* offset to special dattnrt */
/*codeint sattnrt;		/* offset to special sattnrt */

codeint hwloop_ptr;		/* offset to hardware loop Acode */
codeint multhwlp_ptr;		/* offset to multiple hardware loop flag */
codeint nsc_ptr;		/* offset to NSC Acode */

unsigned long 	ix;			/* FID currently in Acode generation */
int 	nth2D;			/* 2D Element currently in Acode generation (VIS usage)*/
int     arrayelements;		/* number of array elements */
int     fifolpsize;		/* fifo loop size (63, 512, 1k, 2k, 4k) */
int     ap_interface;		/* ap bus interface type 1=500style, 2=amt style */ 
int	acqbitmask;		/* global settings for acquisition  */
int   rotorSync;		/* rotor sync interface 1=present or 0=not present */ 

/* ---  interlock parameters, etc. --- */
char 	in[MAXSTR];
char 	tin[MAXSTR];
char 	alock[MAXSTR];
char 	wshim[MAXSTR];
int  	spin;
int  	loc;
int  	spinactive;
double  vttemp; 	/* VT temperature setting */
double  vtwait; 	/* VT temperature timeout setting */
double  vtc; 		/* VT temperature cooling gas setting */
int  	tempactive;
int  	lockmode;
int  	whenshim;
int  	shimatanyfid;


/************************************************************************/
/*      SIS Globals                                                     */
/************************************************************************/

/*------------------------------------------------------------------
    RF and Gradient pattern structures
------------------------------------------------------------------*/
typedef struct _RFpattern {
    double  phase;
    double  amp;
    double  time;
} RFpattern;
typedef struct _Gpattern {
    double  amp;
    double  time;
} Gpattern;


/*------------------------------------------------------------------
    RF PULSES
------------------------------------------------------------------*/
double  p2;                  /* pulse length */
double  p3;                  /* pulse length */
double  p4;                  /* pulse length */
double  p5;                  /* pulse length */
double  pi;                  /* inversion pulse length */
double  psat;                /* saturation pulse length */
double  pmt;                 /* magnetization transfer pulse length */
double  pwx;                 /* X-nucleus pulse length */
double  pwx2;                /* X-nucleus pulse length */
double  psl;                 /* spin-lock pulse length */

char  pwpat[MAXSTR];         /* pattern for pw,tpwr */
char  p1pat[MAXSTR];         /* pattern for p1,tpwr1 */
char  p2pat[MAXSTR];         /* pattern for p2,tpwr2 */
char  p3pat[MAXSTR];         /* pattern for p3,tpwr3 */
char  p4pat[MAXSTR];         /* pattern for p4,tpwr4 */
char  p5pat[MAXSTR];         /* pattern for p5,tpwr5 */
char  pipat[MAXSTR];         /* pattern for pi,tpwri */
char  satpat[MAXSTR];        /* pattern for psat,satpat */
char  mtpat[MAXSTR];         /* magnetization transfer RF pattern */
char  pslpat[MAXSTR];        /* pattern for spin-lock */

double  tpwr1;               /* Transmitter pulse power */
double  tpwr2;               /* Transmitter pulse power */
double  tpwr3;               /* Transmitter pulse power */
double  tpwr4;               /* Transmitter pulse power */
double  tpwr5;               /* Transmitter pulse power */
double  tpwri;               /* inversion pulse power */
double  satpwr;              /* saturation pulse power */
double  mtpwr;               /* magnetization transfer pulse power */
double  pwxlvl;              /* pwx power level */
double  pwxlvl2;             /* pwx2 power level */
double  tpwrsl;              /* spin-lock power level */

/*------------------------------------------------------------------
    RF DECOUPLER PULSES
------------------------------------------------------------------*/
char  decpat[MAXSTR];        /* pattern for decoupler pulse */
char  decpat1[MAXSTR];       /* pattern for decoupler pulse */
char  decpat2[MAXSTR];       /* pattern for decoupler pulse */
char  decpat3[MAXSTR];       /* pattern for decoupler pulse */
char  decpat4[MAXSTR];       /* pattern for decoupler pulse */
char  decpat5[MAXSTR];       /* pattern for decoupler pulse */

double  dpwr1;               /* Decoupler pulse power */
double  dpwr4;               /* Decoupler pulse power */
double  dpwr5;               /* Decoupler pulse power */

/*------------------------------------------------------------------
    GRADIENTS
------------------------------------------------------------------*/
double  gro,gro2,gro3;       /* read out gradient strength */
double  gpe,gpe2,gpe3;       /* phase encode for 2D, 3D & 4D */
double  gss,gss2,gss3;       /* slice-select gradients */
double  gror;                /* read out refocus */
double  gssr;                /* slice select refocus */
double  grof;                /* read out refocus fraction */
double  gssf;                /* slice refocus fraction */
double  g0,g1,g2,g3,g4;      /* numbered levels */
double  g5,g6,g7,g8,g9;      /* numbered levels */
double  gx,gy,gz;            /* X, Y, and Z levels */
double  gvox1,gvox2,gvox3;   /* voxel selection */
double  gdiff;               /* diffusion encode */
double  gflow;               /* flow encode */
double  gspoil,gspoil2;      /* spoiler gradient levels */
double  gcrush,gcrush2;      /* crusher gradient levels */
double  gtrim,gtrim2;        /* trim gradient levels */
double  gramp,gramp2;        /* ramp gradient levels */
double  gpemult;             /* shaped phase-encode multiplier */
double  gradstepsz;         /* positive steps in the gradient dac */
double  gradunit;            /* dimensional conversion factor */
double  gmax;                /* maximum gradient value (G/cm) */
double  gxscale;             /* X scaling factor for gmax */
double  gyscale;             /* Y scaling factor for gmax */
double  gzscale;             /* Z scaling factor for gmax */

char  gpatup[MAXSTR];        /* gradient ramp-up pattern */
char  gpatdown[MAXSTR];      /* gradient ramp-down pattern */
char  gropat[MAXSTR];        /* readout gradient pattern */
char  gpepat[MAXSTR];        /* phase-encode gradient pattern */
char  gsspat[MAXSTR];        /* slice gradient pattern */
char  gpat[MAXSTR];         /* general gradient pattern */
char  gpat1[MAXSTR];         /* general gradient pattern */
char  gpat2[MAXSTR];         /* general gradient pattern */
char  gpat3[MAXSTR];         /* general gradient pattern */
char  gpat4[MAXSTR];         /* general gradient pattern */
char  gpat5[MAXSTR];         /* general gradient pattern */

/*------------------------------------------------------------------
    DELAYS
------------------------------------------------------------------*/
double  tr;                  /* repetition time per scan */
double  te;                  /* primary echo time */
double  ti;                  /* inversion time */
double  tm;                  /* mid delay for STE */
double  at;                  /* acquisition time */
double  tpe,tpe2,tpe3;       /* phase encode durations for 2D-4D */
double  tcrush;              /* crusher gradient duration */
double  tdiff;               /* diffusion encode duration */
double  tdelta;              /* diffusion encode duration */
double  tDELTA;              /* diffusion gradient separation */
double  tflow;               /* flow encode duration */
double  tspoil;              /* spoiler duration */
double  hold;                /* physiological trigger hold off */
double  trise;               /* gradient coil rise time: sec */
double  satdly;              /* saturation time */
double  tau;                 /* general use delay */
double  runtime;             /* user variable for total exp time */

/*------------------------------------------------------------------
    FREQUENCIES
------------------------------------------------------------------*/
double  resto;               /* reference frequency offset */
double  wsfrq;               /* water suppression offset */
double  chessfrq;            /* chemical shift selection offset */
double  satfrq;              /* saturation offset */
double  mtfrq;               /* magnetization transfer offset */

/*------------------------------------------------------------------
    PHYSICAL SIZES AND POSITIONS
      Dimensions and positions for slices, voxels and fov
------------------------------------------------------------------*/
double  pro;                 /* fov position in read out */
double  ppe,ppe2,ppe3;       /* fov position in phase encode */
double  pos1,pos2,pos3;      /* voxel position */
double  pss[MAXSLICE];       /* slice position array */

double  lro;                 /* read out fov */
double  lpe,lpe2,lpe3;       /* phase encode fov */
double  lss;                 /* dimension of multislice range */

double  vox1,vox2,vox3;      /* voxel size */
double  thk;                 /* slice or slab thickness */

double  fovunit;             /* dimensional conversion factor */
double  thkunit;             /* dimensional conversion factor */

/*------------------------------------------------------------------
    BANDWIDTHS
------------------------------------------------------------------*/
double  sw1,sw2,sw3;         /* phase encode bandwidths */

/*------------------------------------------------------------------
    ORIENTATION PARAMETERS
------------------------------------------------------------------*/
char  orient[MAXSTR];        /* slice orientation */
char  vorient[MAXSTR];       /* voxel orientation */
char  dorient[MAXSTR];       /* diffusion gradient orientation */
char  sorient[MAXSTR];       /* saturation band orientation */
char  orient2[MAXSTR];       /* spare orientation */

double  psi,phi,theta;       /* slice Euler angles */
double  vpsi,vphi,vtheta;    /* voxel Euler angles */
double  dpsi,dphi,dtheta;    /* diffusion gradient Euler angles */
double  spsi,sphi,stheta;    /* saturation band Euler angles */
double  psi2,phi2,theta2;    /* spare Euler angles */

/*------------------------------------------------------------------
    COUNTS AND FLAGS
------------------------------------------------------------------*/
double  nD;                  /* experiment dimensionality */
double  ns;                  /* number of slices */
double  ne;                  /* number of echoes */
double  ni;                  /* number of standard increments */
double  nv,nv2,nv3;          /* number of phase encode views */
double  ssc;                 /* compressed ss transients */
double  ticks;               /* external trigger counter */

char  ir[MAXSTR];            /* inversion recovery flag */
char  ws[MAXSTR];            /* water suppression flag */
char  mt[MAXSTR];            /* magnetization transfer flag */
char  pilot[MAXSTR];         /* auto gradient balance flag */
char  seqcon[MAXSTR];        /* acquisition loop control flag */
char  petable[MAXSTR];       /* name for phase-encode table */
char  acqtype[MAXSTR];       /* e.g. "full" or "half" echo */
char  exptype[MAXSTR];       /* e.g. "se" or "fid" in CSI */
char  apptype[MAXSTR];       /* keyword for param init, e.g., "imaging"*/
char  seqfil[MAXSTR];        /* pulse sequence name */
char  rfspoil[MAXSTR];       /* rf spoiling flag */
char  satmode[MAXSTR];       /* presaturation mode */
char  verbose[MAXSTR];       /* verbose mode for sequences and psg */

/*------------------------------------------------------------------
    Miscellaneous
------------------------------------------------------------------*/
double  rfphase;             /* rf phase shift  */
double  B0;                  /* static magnetic field level */
char  preamp[MAXSTR];       /* PIC high/low gain setting */

/*------------------------------------------------------------------
    Old SISCO Imaging Variables
------------------------------------------------------------------*/
double  slcto;               /* slice selection offset */
double  delto;               /* slice spacing frequency */
double  tox;                 /* Transmitter Offset */
double  toy;                 /* Transmitter Offset */
double  toz;                 /* Transmitter Offset */
double  griserate;           /* Gradient riserate  */

/************************************************************************/
/*      End SIS Globals                                                 */
/************************************************************************/

/* --- Pulse Seq. globals --- */

/*  Objects   */
Object Ext_Trig  = NULL;	/* External Trigger Device */
Object RT_Delay  = NULL;	/* Real-Time Delay Event Device */
Object ToAttn    = NULL;	/* AMT Interface Obs attenuator */
Object DoAttn    = NULL;	/* AMT Interface Dec attenuator */
Object Do2Attn   = NULL;	/* AMT Interface Dec2 attenuator */
Object Do3Attn   = NULL;	/* AMT Interface Dec3 attenuator */
Object ToLnAttn  = NULL;	/* Solids Linear attenuator, Observe Channel */
Object DoLnAttn  = NULL;	/* Solids Linear attenuator, Dec Channel */
Object Do2LnAttn = NULL;	/* Linear attenuator, Dec2 Channel */
Object Do3LnAttn = NULL;	/* Linear attenuator, Dec3 Channel */
Object RF_Rout   = NULL;	/* RF Routing Relay Bank */
Object RF_Opts   = NULL;	/* RF Amp & etc. Options Bank */
Object RF_Opts2  = NULL;	/* RF Amp & etc. Options Bank */
Object RF_MLrout = NULL;	/* RF routing in Magnet Leg */
Object RF_TR_PA  = NULL;	/* RF T/r and PreAmp enables */
Object RF_Amp1_2 = NULL;	/* Amp 1/2 cw vs. pulse */
Object RF_Amp3_4 = NULL;	/* Amp 3/4 cw vs. pulse */
Object RF_Amp_Sol= NULL;	/* Amp solids cw vs. pulse */
Object RF_mixer  = NULL;	/* XMTRs hi/lo mixer select */
Object RF_hilo   = NULL;	/* attns hi/lo-band relays router */
Object RF_Mod    = NULL;	/* xmtrs modulation mode APbyte */
Object HS_select = NULL;	/* XMTRs HS lines select */
Object RCVR_homo = NULL;	/* RCVR homo decoupler bit */
Object X_gmode   = NULL;	/* XMTRs gate mode selection */
Object HS_Rotor  = NULL;	/* High Speed Rotor Device */
Object RFChan1   = NULL;	/* RF Channel Device */
Object RFChan2   = NULL;	/* RF Channel Device */
Object RFChan3   = NULL;	/* RF Channel Device */
Object RFChan4   = NULL;	/* RF Channel Device */

Object RF_Channel[MAX_RFCHAN_NUM+1];	/* index array of RF channel Objects */

/*  RF Channels */
int OBSch=1;			/* The Acting Observe Channel */
int DECch=2;  			/* The Acting Decoupler Channel */
int DEC2ch=3;  			/* The Acting 2nd Decoupler Channel */
int DEC3ch=4;  			/* The Acting 3rd Decoupler Channel */
int NUMch=2;			/* Number of channels configured */

/*------------------------------------------------------------------------
|
|	This module is a child task.  It is called from vnmr and is passed
|	pipe file descriptors. Option are passed in the parameter go_Options.
|       This task first transfers variables passed through the pipe to this
|	task's own variable trees.  Then this task can do what it wants.
|
|	The go_Option parameter passed from parent can include
|         debug which turns on debugging.
|         next  which for automation, puts the acquisition message at the
|               top of the queue
|         acqi  which indicates that the interactive acquisition program
|               wants an acode file.  No message is sent to Acqproc.
|	  ra    which means the VNMR command is RA, not GO
+-------------------------------------------------------------------------*/

/* Used by locksys.c  routines from vnmr */
char systemdir[MAXPATHL];       /* vnmr system directory */
char userdir[MAXPATHL];         /* vnmr user system directory */
char curexp[MAXPATHL];       /* current experiment path */

FILE *dpsdata;			/* display pulse sequence data file */

main(argc,argv) 	int argc; char *argv[];
{   
    char    array[MAXSTR];
    char    filename[MAXPATHL];		/* file name for Codes */
    char   *gnames[50];

    double arraydim;	/* number of fids (ie. set of acodes) */

    int     ngnames;
    int     i,index;
    int     narrays;
    int     bytes;
    int     nextflag = 0;
    int	    res;
    int     Rev_Num;
    int     P_rec_stat;
    char   *tmpptr;
  
    acqiflag = ra_flag = dps_flag = 0;
    ok = TRUE;

    if (argc < 4)  /* not enought args to start, exit */
    {	fprintf(stderr,
	"This is a background task! Only execute from within 'vnmr'!\n");
	exit(1);
    }
    Rev_Num = atoi(argv[1]); /* GO -> PSG Revision Number */
    pipe1[0] = atoi(argv[2]); /* convert file descriptors for pipe 1*/
    pipe1[1] = atoi(argv[3]);
    pipe2[0] = atoi(argv[4]); /* convert file descriptors for pipe 2*/
    pipe2[1] = atoi(argv[5]);
    setupflag = atoi(argv[6]);  /* alias flag */

    close(pipe1[1]); /* close write end of pipe 1*/
    close(pipe2[0]); /* close read end of pipe 2*/
    P_rec_stat = P_receive(pipe1);  /* Receive variables from pipe and load trees */    
    close(pipe1[0]);

    /* ------- Check For GO - PSG Revision Clash ------- */
    if (Rev_Num != GO_PSG_REV )
    {	
	char msge[100];
	sprintf(msge,"GO(%d) and PSG(%d) Revision Clash, PSG Aborted.\n",
	  Rev_Num,GO_PSG_REV);
	text_error(msge);
	abort(1);
    }
    if (P_rec_stat == -1 )
    {	
	text_error("P_receive had a fatal error.\n");
	abort(1);
    }

    /*-----------------------------------------------------------------
    |  begin of PSG task
    +-----------------------------------------------------------------*/

    if (strcmp("dps", argv[0]) == 0)
         dps_flag = 1;
    else
	abort(0);

    getparm("goid","string",CURRENT,filename,MAXPATHL);

    A_getnames(GLOBAL,gnames,&ngnames,50);

    /* --- setup the arrays of variable names and values --- */

    /* --- get number of variables in CURRENT tree */
    A_getnames(CURRENT,0L,&ntotal,1024); /* get number of variables in tree */
    cnames = (char **) malloc(ntotal * (sizeof(char *))); /* allocate memory */
    cvals = (double **) malloc(ntotal * (sizeof(double *)));
    if ( cvals == 0L  || cnames == 0L )
    {
	text_error("insuffient memory for variable pointer allocation!!");
	reset(); 
	abort(0);
    }
    /* --- load pointer arrays with pointers to names and values --- */
    A_getnames(CURRENT,cnames,&nnames,ntotal);
    A_getvalues(CURRENT,cvals,&ncvals,ntotal);

    init_hash(ntotal);
    load_hash(cnames,ntotal);

    /* --- malloc space for Acodes --- */
    if (getparm("arraydim","real",CURRENT,&arraydim,1))
	abort(1);

    if (acqiflag || (setupflag > 0))	  	/* if no a GO then force to 1D */
        arraydim = 1.0;		/* any setup is 1D, nomatter what. */

    Codesize =  (long)  (VM02_SIZE * sizeof(codeint));     /* code + struct */
    preCodes = (codeint *) malloc( Codesize );
    if ( preCodes == 0L )
    {
        char msge[128];
	sprintf(msge,"Insuffient memory for Acode allocation of %ld Kbytes.",
		Codesize/1000L);
	text_error(msge);
	reset(); 
	abort(0);
    }
    Codes = preCodes + 4;

    CodeEnd = ((long) Codes) + Codesize;

    /* start of first Acode set (words)*/
    /* --- skip past offset array --- */
    /* Set Acode pointer to beginning of Codes */
    Codeptr = codestadr = Codes;

    /* Set up Acode pointers */
    Alc = (Acqparams *) Codes;	/* start of low core */
    lc_stadr = Codes;
    Aauto = (autodata *) (Alc + 1) ;/* start of auto struc */
    Aacode = (codeint *) (Aauto + 1);
    Codeptr = Aacode;

    fidctr = (codeint) (((codeint *) &(Alc->acqelemid)) - lc_stadr);/*offsetelemid*/
    fidctr += 1;/* since real time offset are used as integers & fidctr is long */
		/* we must shift the offset by 1 word */
    npr_ptr = (codeint) (((codeint *) &(Alc->acqnp)) - lc_stadr);/*offsetto np*/

#ifdef SIS
    /* Offset to nt used by vscan psg element */
    ntrt = (codeint) (((codeint *) &(Alc->acqnt)) - lc_stadr);/* offset to nt */
    ntrt += 1;	/* since real time offset are used as integers & nt is long */
		/* we must shift the offset by 1 word */
#endif SIS

    ct = (codeint) (((codeint *) &(Alc->acqct)) - lc_stadr);/* offset to ct */
    ct += 1;	/* since real time offset are used as integers & ct is long */
		/* we must shift the offset by 1 word */
    oph = (codeint) (((codeint *) &(Alc->acqoph)) - lc_stadr);/*offset to oph */
    bsval = (codeint) (((codeint *) &(Alc->acqbs)) - lc_stadr);/*offset bs*/
    bsctr = (codeint) (((codeint *) &(Alc->acqbsct)) - lc_stadr);/*offsetobsct*/
    ssval = (codeint) (((codeint *) &(Alc->acqss)) - lc_stadr);/*offset to ss*/
    ssctr = (codeint) (((codeint *) &(Alc->acqssct)) - lc_stadr);/* ssct*/
    HSlines_ptr = (codeint) (((codeint *) &(Alc->acqsqui)) - lc_stadr);
    HSlines_ptr += 1;	/* since real time offset are used as integers &  */
			/* squi is long we must shift the offset by 1 word */

    /* offset to nsp loc 44-52*/
    nsp_ptr = (codeint) (((codeint *) &(Alc->acqocsr)) - lc_stadr);/*offsetto nsp*/
   /* user variables*/
    id2 = (codeint) (((codeint *) &(Alc->acqid2)) - lc_stadr);/*offset to id2 */
    id3 = id2 + 1;
    id4 = id2 + 2;
    zero = (codeint) (((codeint *) &(Alc->acqzero)) - lc_stadr);/*offset to zero */
    one = zero + 1;				/* offset to one */
    two = zero + 2;				/* offset to two */
    three = zero + 3;				/* offset to three */
    tablert = (codeint) (((codeint *) &(Alc->acqtablert)) - lc_stadr);
    		/* offset to real-time table variable */
    v1 = (codeint) (((codeint *) &(Alc->acqv1)) - lc_stadr);/* offset to v1 */
    v2 = v1 + 1; 	v3 = v1 + 2; 		v4 = v1 + 3;
    v5 = v1 + 4; 	v6 = v1 + 5; 		v7 = v1 + 6;
    v8 = v1 + 7;	v9 = v1 + 8; 		v10 = v1 + 9;
    v11 = v1 + 10; 	v12 = v1 + 11; 		v13 = v1 + 12;
    v14 = v1 + 13;

    /* --- set parameter to unused value, so new & old will not be 
       --- the same 1st time --- */
    oldlkmode = -1;			/* previous value of lockmode */
    oldspin = -1;			/* previous value of spin */
    oldwhenshim = -1;			/* previous value of shimming mask */
    oldvttemp = 29999.0;		/* previous value of vt tempature */
    oldpad = 29999.0;			/* previous value of pad */
    oldpadindex = -1;			/* previous value index of PAD */
    newpadindex = 0;			/* new value index of PAD */
    exptime  = 0.0; 	   /* total timer events for a exp duration estimate */
    start_elem = 1;             /* elem (FID) for acquisition to start on (RA)*/
    completed_elem = 0;

    if (setGflags()) abort(0);

    /* --- set up global parameter --- */
    initparms();

    /* ---- A single or arrayed experiment ???? --- */
    ix = nth2D = 0;		/* initialize ix */
    {
       char dpsdatafile[256];
       long  x_ptr;

        x_ptr = Codeptr - Codes;

       totaltime  = 0.0; /* total timer events for a fid */
       ix = nth2D = 1;		/* generating Acodes for FID 1 */
       strcpy(dpsdatafile,curexp);
       strcat(dpsdatafile,"/dpsdata");
       if ((dpsdata = fopen(dpsdatafile, "w")) == NULL)
       { 
           fprintf(stderr, "can not create file for dps, abort\n");
           exit(0);
       }   
       creatDPS();
       close_error(0);
       fclose(dpsdata);
       exit(0);
    }
}

/*-----------------------------------------------------------------
|       sign_add()/2
|        uses sign of first argument to decide to add or subtract
|                       second argument to first
|       returns new value (double)
+------------------------------------------------------------------*/
double sign_add(arg1,arg2)
double arg1;
double arg2;
{
    if (arg1 >= 0.0)
        return(arg1 + arg2);
    else
        return(arg1 - arg2);
}

/*--------------------------------------------------------------------------
|   This closes the pipe that go is waiting on when it is in automation mode.
|   This procedure is called from close_error.  Close_error is called either
|   when PSG completes successfully or when PSG calls abort.
+--------------------------------------------------------------------------*/
closepipe2(int success)
{
    char autopar[12];


    if (acqiflag)
       write(pipe2[1],&ready,sizeof(int)); /* tell go, PSG is done */
    else if (!dps_flag && (getparm("auto","string",GLOBAL,autopar,12) == 0))
        if ((autopar[0] == 'y') || (autopar[0] == 'Y'))
            write(pipe2[1],&ready,sizeof(int)); /* tell go, PSG is done */
    close(pipe2[1]); /* close write end of pipe 2*/
}
/*-----------------------------------------------------------------------
+------------------------------------------------------------------------*/
reset()
{
    int i;

    if (cnames) free(cnames);
    if (cvals) free(cvals);
    if (Codes) free(Codes);
}
/*------------------------------------------------------------------------------
|
|	setGflag()
|
|	This function sets the global flags and variables that do not change
|	during the pulse sequence 
|       the 2D experiment 
|   Modified   Author     Purpose
|   --------   ------     -------
|   12/13/88   Greg B.    1. Added Code for new Objects, 
|			     Ext_Trig, ToLnAttn, DoLnAttn, HS_Rotor.
|			  2. Attn Object has changed to use the OFFSET rather
|			     than the MAX to determine the directional offset 
|			     (i.e. works forward or backwards)
+----------------------------------------------------------------------------*/
setGflags()
{
    char  mess[MAXSTR];
    double tmpval;
    int 	i;

    if ( P_getreal(GLOBAL,"numrfch",&tmpval,1) >= 0 )
    {
        NUMch = (int) (tmpval + 0.0005);
	if (( NUMch < 1) || (NUMch > MAX_RFCHAN_NUM))
	{
            sprintf(mess,
	     "Number of RF Channels specified '%d' is too large.. PSG Aborted.\n",
	      NUMch);
            text_error(mess);
	    abort(1);
	}
    }
    else
       NUMch = 2;
    /* --- determine type of ap_interface board that is present ---	*/
    /* type 1 is 500 style attenuators,etc			   	*/
    /* type 2 is ap interface introduced with the AMT amplifiers	*/
    /* type 3 is 'type 2' with the additional fine linear attenuators	*/
    /*        for the solids application. 				*/
    /* type 4 introduces the rf introduced in 1991/1993			*/
    /*------------------------------------------------------------------*/
	ap_interface = 1;	

    /*--- obtain coarse & fine attenuator RF channel configuration ---*/
    cattn[0] = fattn[0] = 0.0;  /* dont use 0 index */
    for(i=1; i<=MAX_RFCHAN_NUM; i++)
    {
      /*--- obtain coarse & fine attenuator RF channel configuration ---*/
      if ( P_getreal(GLOBAL,"cattn",&cattn[i],i) < 0 )
      {
          cattn[i] = 0.0;                /* if not found assume 0 */
      }
      if ( P_getreal(GLOBAL,"fattn",&fattn[i],i) < 0 )
      {
          fattn[i] = 0.0;                /* if not found assume 0 */
      }

      /*--- obtain PTS values for channel configuration ---*/
      if ( P_getreal(GLOBAL,"ptsval",&tmpval,i) >= 0 )
        ptsval[i-1] = (int) (tmpval + .0005);
      else
	ptsval[i-1] = 0;		/* no pts for decoupler */
    }

	fifolpsize = 63;

	rotorSync = 0;


    /* --- set rf and amplifier types for decoupler and transmitter --- */
    if (getparm("rftype","string",GLOBAL,rftype,15))
    	return(ERROR);
    
    if (getparm("amptype","string",GLOBAL,amptype,15))
    	return(ERROR);
    if (getparm("rfband","string",CURRENT,rfband,15))
    	return(ERROR);

    /* --- discover waveformers and gradients --- */

    rfwg[0]='\0';
    if (P_getstring(GLOBAL, "rfwg", rfwg, 1, MAXSTR-4) < 0)
       strcpy(rfwg,"nnnn");
    else
       strcat(rfwg,"nnnn");
    gradtype[0]='\0';
    if (P_getstring(GLOBAL, "gradtype", gradtype, 1, MAXSTR-4) < 0)
       strcpy(gradtype,"nnnn");
    else
       strcat(gradtype,"nnnn");

    /* --- end discover waveformers and gradients --- */

    if (rftype[0] == 'c' || rftype[0] == 'C' ||
        rftype[0] == 'd'  || rftype[0] == 'D')
    {
	newtrans = TRUE;
    }
    else
    {
	newtrans = FALSE;
    }

    if (rftype[1] == 'c'  || rftype[1] == 'C' ||
        rftype[1] == 'd'  || rftype[1] == 'D')
    {
	newdec = TRUE;
    }
    else
    {
	newdec = FALSE;
    }

    if (amptype[0] == 'c'  || amptype[0] == 'C')
    {
	newtransamp = FALSE;
    }
    else
    {
	newtransamp = TRUE;
    }

    if (amptype[1] == 'c'  || amptype[1] == 'C')
    {
	newdecamp = FALSE;
    }
    else
    {
	newdecamp = TRUE;
    }
    automated = TRUE;

    /* ---- set basic instrument proton frequency --- */
    if (getparm("h1freq","real",GLOBAL,&tmpval,1))
    	return(ERROR);
    H1freq = (int) tmpval;

/*****************************************
*  Load userdir, systemdir, and curexp.  *
*****************************************/

    if (P_getstring(GLOBAL, "userdir", userdir, 1, MAXPATHL-1) < 0)
       text_error("PSG unable to locate current user directory\n");
    if (P_getstring(GLOBAL, "systemdir", systemdir, 1, MAXPATHL-1) < 0)
       text_error("PSG unable to locate current system directory\n");
    if (P_getstring(GLOBAL, "curexp", curexp, 1, MAXPATHL-1) < 0)
       text_error("PSG unable to locate current experiment directory\n");

    vttype = (int) tmpval;

    return(OK);
}



/*-----------------------------------------------------------------
|       creatDPS()/0
|       creates the display pulse sequence data
+------------------------------------------------------------------*/
creatDPS()
{
    char tmpphsname[MAXSTR];    /* temporary phase-table file name */
    int i;
    double  tp1, dp1;

    hwlooping = 0;
    acqtriggers = 0;
    hwloopelements = 0;
    starthwfifocnt = 0;
    if (newtransamp)
        fprintf(dpsdata, " QT %f\n", getval("tpwr"));
    else
        fprintf(dpsdata, " QT 63.0\n");
    if (newdecamp)
        fprintf(dpsdata, " QD %f\n", getval("dpwr"));
    else
    {
        if (dhpflag)
                fprintf(dpsdata, " QD %f\n", getval("dhp"));
        else
                fprintf(dpsdata, " QD %f\n", getval("dlp"));
    }
    if (NUMch > 2)
    {
      if (cattn[DO2DEV] != 0.0)
        fprintf(dpsdata, " Q3 %f\n", getval("dpwr2"));
      else
        fprintf(dpsdata, " Q3 0.0\n");
    }
    if (NUMch > 3)
    {
      if (cattn[DO3DEV] != 0.0)
        fprintf(dpsdata, " Q4 %f\n", getval("dpwr3"));
      else
        fprintf(dpsdata, " Q4 0.0\n");
    }
    x_pulsesequence();  /* generate  Pulse Sequence */
    return;
}



/*-----------------------------------------------------------
|
|   text_error()
|   Writes messages to a file named "psg.error" in the
|       vnmruser (env variable) directory.
|
+-----------------------------------------------------------*/
static int   erroropen = 0;
static FILE *errorfile;
static char  errorpath[MAXPATHL];

text_error(error_mess)
char *error_mess;
{

  fprintf(stdout,"%s\n",error_mess);
  if (!erroropen)
  {
    if (P_getstring(GLOBAL, "userdir", errorpath, 1, MAXPATHL) >= 0)
    {
      strcat(errorpath,"/psg.error");
      if (errorfile=fopen(errorpath,"w+"))
        erroropen = 1;
    }
  }
  if (erroropen)
    fprintf(errorfile,error_mess);
}

/*-----------------------------------------------------------
|
|   close_error(int)
|   Closes the error message file named "psg.error" in the
|       vnmruser (env varaible) directory.
|
+-----------------------------------------------------------*/
close_error(int fail)
{
  if (erroropen)
  {
    fclose(errorfile);
    erroropen = 0;
    chmod(errorpath,0660);
  }
  closepipe2(fail);
}

getparm(varname,vartype,tree,varaddr,size)
char *varname;
char *vartype;
int   tree;
char *varaddr; /* For Reals, pointer is recased as a double; ie.(double *) */
int      size;
{
    int ret;
    char mess[MAXSTR];

    if ( (strcmp(vartype,"REAL") == 0) || (strcmp(vartype,"real") == 0) )
    {
        if ((ret = P_getreal(tree,varname,(double *)varaddr,1)) < 0)
        {    sprintf(mess,"Cannot get parameter: %s\n",varname);
             text_error(mess);
             return(1);
        }
    }
    else
    {
        if ( (strcmp(vartype,"STRING") == 0) ||
             (strcmp(vartype,"string") == 0) )
        {
            if ((ret = P_getstring(tree,varname,varaddr,1,size)) < 0)
            {   sprintf(mess,"Cannot get parameter: %s\n",varname);
                text_error(mess);
                return(1);
            }
        }
        else
        {   sprintf(mess,"Variable '%s' is neither a 'real' or 'string'.\n",
                        vartype);
            text_error(mess);
            return(1);
        }
      }
    return(0);
}


/*-----------------------------------------------------------------
|       getval()/1
|       returns value of variable
+------------------------------------------------------------------*/
double getval(variable)
char *variable;
{
    int index;

    /* index = findsname(variable,cnames,nnames); */
    index = find(variable);   /* hash table find */
    if (index == NOTFOUND)
    {
        fprintf(stdout,"'%s': not found, value assigned to zero.\n",variable);
        return(0.0);
    }
    return( *( (double *) cvals[index]) );
}
/*-----------------------------------------------------------------
|       getstr()/1
|       returns string value of variable
+------------------------------------------------------------------*/
getstr(variable,buf)
char *variable;
char buf[];
{
    int index;

    /* index = findsname(variable,cnames,nnames); */
    index = find(variable);   /* hash table find */
    if (index != NOTFOUND)
    {
        char *content;

        content = ((char *) cvals[index]);
        strncpy(buf,content,MAXSTR-1);
        buf[MAXSTR-1] = 0;
    }
    else
    {
        fprintf(stdout,"'%s': not found, value assigned to null.\n",variable);
        buf[0] = 0;
    }
}


Werrprintf(format,args) char *format; char *args;
{
   _doprnt(format,&args,stderr);
   fprintf(stderr,"\n");
}

Winfoprintf(format,args)
{}

Wscrprintf(format,args)
{}
Wprintfpos(i1,i2,i3,s1) int i1,i2,i3; char *s1;
{
   fprintf(stderr,"Wprintfpos fake %s\n",s1);
}
WerrprintfWithPos(format,args) char *format; char *args;
{
}


/*-----------------------------------------------------------------
|       initparms()/
|       initializes the main varaibles used
|   Modified   Author     Purpose
|   --------   ------     -------
|   12/13/88   Greg B.    1. Added Code to initialize fine attenuator
|                            parameters tpwrf,dpwrf.
|                         2. initialize dpwr, now a standard parameter
|                            replacing the coarse attenuator function
|                            of dhp.
|    4/21/89   Greg B.    1. Added Code to initialize interleave parameter (il)
+------------------------------------------------------------------*/
#define POLE4 2.0
/* six   pole Bessel                 */
#define POLE6 2.33
/* eight pole Elliptical             */
#define POLE8 1.29
codeint lkflt_fast;     /* value to set lock loop filter to when fast */
codeint lkflt_slow;     /* value to set lock loop filter to when slow */
static ra_initcnt = 0;

initparms()
{
    double getval();
    double getvalnwarn();
    double tmpval;
    char   tmpstr[20];
    char   audfilter[12];
    int    tmp, getchan;
    int    dspflag = FALSE;
    char   dsp[10];
    int    factor=0;
    vInfo  info;
    double fbmax;
    double maxlbsw;

    if (!P_getVarInfo(CURRENT, "oversamp", &info))
      if (info.active)
        if (!P_getreal(CURRENT, "oversamp", &tmpval, 1))
          if (tmpval > 1)
          {
            factor = (int)(tmpval+0.5);
            if (factor > 1)
              dspflag = TRUE;
          }

    if (dspflag)
    {
       if ( P_getreal(CURRENT, "oscoef", &tmpval, 1) < 0 )
       {
          sw = getval("sw");
          np = getval("np");
       }
       else
       {
          sw = getval("sw");
          np = getval("np");
          sw *= factor;
          np *= factor;
       }
    }
    else
    {
       sw = getval("sw");
       np = getval("np");
    }

    if ( P_getreal(CURRENT,"nf",&nf,1) < 0 )
    {
        nf = 0.0;                /* if not found assume 0 */
    }
    if (nf < 2.0) 
	nf = 1.0;

/*****************************
*  Set the observe channel.  *
*****************************/

    if (ap_interface == 4)
    {
       if ( (P_getstring(CURRENT, "rfchannel", tmpstr, 1, 12) == 0) &&
            ((tmpstr[0] == '1') || (tmpstr[0] == '2')) )
       {
          if ( (NUMch == 2) && (tmpstr[0] == '2') )
          {
             OBSch = DODEV;
             DECch = TODEV;
          }
       }
       xmf = getvalnwarn("xmf");	/* observe modulation freq */
       if (xmf == 0.0) xmf=1000.0;
/*       getstrnwarn("xm",xm); */
/*       if (xm[0]==0) strcpy(xm,"n"); */
       strcpy(xm,"n");
       xmsize = strlen(xm);
/*       getstrnwarn("xmm",xmm);
/*       if (xmm[0]==0) strcpy(xmm,"c"); */
       strcpy(xmm,"c");
       xmmsize = strlen(xmm);
       getstrnwarn("xseq",xseq);
       xres = getvalnwarn("xres");	/* prg decoupler digital resolution */
       if (xres < 1.0)
          xres = 1.0;
    }
    if ( P_getreal(GLOBAL, "locktc", &tmpval, 1) < 0 )
       lkflt_fast = (codeint) 1;
    else
       lkflt_fast = (codeint) (tmpval + 0.5);
    if ( P_getreal(GLOBAL, "lockacqtc", &tmpval, 1) < 0 )
       lkflt_slow = (codeint) 4;
    else
       lkflt_slow = (codeint) (tmpval + 0.5);
    if (ap_interface == 4)
    {  if ( (lkflt_fast < 1) || (lkflt_fast > 4) ) lkflt_fast=(codeint)1;
       if ( (lkflt_slow < 1) || (lkflt_slow > 4) ) lkflt_slow=(codeint)4;
    }
    else
    {  if (lkflt_fast != 2) lkflt_fast=(codeint)1;
       if (lkflt_fast == 2) lkflt_fast=(codeint)4;
       if (lkflt_slow != 1) lkflt_slow=(codeint)4;
    }

    /* Determine fb limit */
    if (P_getreal(GLOBAL, "maxsw_loband", &maxlbsw, 1) < 0){
	maxlbsw = 100000.0;
    }
    fbmax = maxlbsw > 100000.1 ? FBMAX * 2 : FBMAX;

    nt = getval("nt");
    sfrq = getval("sfrq");
    beta = POLE4;
    if (P_getstring(GLOBAL,"audiofilter",audfilter,1,12) == 0)
      if (audfilter[0] == 'e')
      {  if (sw >= maxlbsw+0.1) 
	   beta = POLE6;
         else
	   beta = POLE8;
      }
    fb = getval("fb"); 			/* filter bandwidth */
    if (dspflag)
    {
      fb = fb*factor;
      if (fb > fbmax)
	fb = fbmax;
    }
    filter = getval("filter");		/* pulse Amp filter setting */
    tof = getval("tof");
    bs = getval("bs");
    if (!var_active("bs",CURRENT))
	bs = 0.0;
    pw = getval("pw");
    pw90 = getval("pw90");
    p1 = getval("p1");

    /* --- delays --- */
    d1 = getval("d1"); 		/* delay */
    d2 = getval("d2"); 		/* a delay: used in 2D experiments */
    d3 = getvalnwarn("d3");	/* a delay: used in 3D experiments */
    d4 = getvalnwarn("d4");	/* a delay: used in 4D experiments */
    phase1 = (int) sign_add(getvalnwarn("phase"),0.005);
    phase2 = (int) sign_add(getvalnwarn("phase2"),0.005);
    phase3 = (int) sign_add(getvalnwarn("phase3"),0.005);
    rof1 = getval("rof1"); 	/* Time receiver is turned off before pulse */
    rof2 = getval("rof2");	/* Time after pulse before receiver turned on */
    alfa = getval("alfa"); 	/* Time after rec is turned on that acqbegins */
    pad = getval("pad"); 	/* Pre-acquisition delay */
    padactive = var_active("pad",CURRENT);
    hst = getval("hst"); 	/* HomoSpoil delay */

#ifndef VIS_ACQ
    tpwr = ( (cattn[TODEV] != 0.0) ? getval("tpwr") : 0.0 );
    if ( P_getreal(CURRENT,"tpwrm",&tpwrf,1) < 0 )
       if ( P_getreal(CURRENT,"tpwrf",&tpwrf,1) < 0 )
          tpwrf = 4095.0;
#else
    tpwr = getval("tpwr");	/* SISCO RF Attenuation Values */
#endif

    getstr("il",il);		/* interleave parameter */
    getstr("rfband",rfband);	/* RF band, high or low */

    getstr("hs",hs);
    hssize = strlen(hs);
    /* setlockmode();		/* set up lockmode variable,homo bits */
    if (bgflag)
    {
      fprintf(stderr,"sw = %lf, sfrq = %10.8lf\n",sw,sfrq);
      fprintf(stderr,"hs='%s',%d\n",hs,hssize);
    }
    gain = getval("gain");
    if (rftype[0] == 'd' && H1freq > 450)
       if (sfrq < 310)
          if (gain < 18)
          {  gain=18;
             text_error("gain reset to 18");
          }
    gainactive = var_active("gain",CURRENT); /* non arrayable */
    getstr("in",in); /* non arrayable */
    getstr("tin",tin); /* non arrayable */
    spin = (int) sign_add(getval("spin"),0.005);
    spinactive = var_active("spin",CURRENT); /* non arrayable */
    vttemp = getval("temp");	/* get vt temperature */
    tempactive = var_active("temp",CURRENT); /* non arrayable */
    vtwait = getval("vtwait");	/* get vt timeout setting */
    vtc = getval("vtc");	/* get vt timeout setting */
    if (getparm("loc","real",CURRENT,&tmpval,1))
	abort(1); 
    if (!var_active("loc",CURRENT))
    {
        tmpval = 0.0;
        if (setparm("loc","real",CURRENT,&tmpval,1))
          abort(1);
    }
    loc = (int) sign_add(tmpval,0.005);
    spinactive = var_active("spin",CURRENT); /* non arrayable */
    getstr("alock",alock);
    getstr("wshim",wshim);
/*
    getlockmode(alock,&lockmode);		
    whenshim = setshimflag(wshim,&shimatanyfid); 
*/

    if ( ( tmp=P_getstring(CURRENT, "dn", tmpstr, 1, 9)) >= 0)
       getchan = TRUE;
    else
       getchan = FALSE;
    /* if "dn" does not exist, don't bother with the rest of channel 2 */
    getchan = (NUMch > 1) && getchan && (tmpstr[0]!='\000');
    if (getchan)		/* variables associated with 2nd channel */
    {
       dfrq = getval("dfrq");
       dmf  = getval("dmf");		/* 1st decoupler modulation freq */
       dof  = getval("dof");
       dres = getvalnwarn("dres");	/* prg decoupler digital resolution */
       if (dres < 1.0) dres = 1.0;
       getstrnwarn("dseq",dseq);
       if (cattn[DODEV] != 0.0)
       {
          dpwr = getval("dpwr");
          dhp     = 0.0;
          dhpflag = FALSE;
          dlp     = 0.0;
       }
       else
       {
          dhp = getval("dhp");
          dlp = getval("dlp");
          dhpflag = var_active("dhp", CURRENT);
          dpwr = dhp;
       }
       if ( P_getreal(CURRENT,"dpwrm",&dpwrf,1) < 0 )
          if ( P_getreal(CURRENT,"dpwrf",&dpwrf,1) < 0 )
             dpwrf = 4095.0;
       getstr("dm",dm);
       dmsize = strlen(dm);
       getstr("dmm",dmm);
       dmmsize = strlen(dmm);
       getstr("homo",homo);
       homosize = strlen(homo);
    }
    else
    {
       dfrq    = 1.0;
       dmf     = 1000;
       dof     = 0.0;
       dres    = 1.0;
       dseq[0] = '\000';
       dhp     = 0.0;
       dhpflag = FALSE;
       dlp     = 0.0;
       dpwr    = 0.0;
       dpwrf   = 0.0;
       strcpy(dm,"n");
       dmsize  = 1;
       strcpy(dmm,"c");
       dmmsize = 1;
       strcpy(homo,"n");
       homosize= 1;
    }
    if (bgflag)
    {
       if (!getchan)
          fprintf(stderr,"next line are default values for chan 2\n");
       fprintf(stderr,"dm='%s',%d, dmm='%s',%d\n",dm,dmsize,dmm,dmmsize);
       fprintf(stderr,"homo='%s',%d\n",homo,homosize);
    }

    if ( (tmp=P_getstring(CURRENT, "dn2", tmpstr, 1, 9)) >= 0)
       getchan = TRUE;
    else
       getchan = FALSE;
    /* if "dn2" does not exist, don't bother with the rest of channel 3 */
    getchan = (NUMch > 2) && getchan && (tmpstr[0]!='\000');
    if (getchan)			/* variables associated with 3rd channel */
    {
      dfrq2 = getval("dfrq2");
      dmf2  = getval("dmf2");		/* 2nd decoupler modulation freq */
      dof2  = getval("dof2");
      dres2 = getvalnwarn("dres2");	/* prg decoupler digital resolution */
      if (dres2 < 1.0) dres2 = 1.0;
      getstrnwarn("dseq2",dseq2);
      dpwr2 = ( (cattn[DO2DEV] != 0.0) ? getval("dpwr2") : 0.0 );
      if ( P_getreal(CURRENT,"dpwrm2",&dpwrf2,1) < 0 )
         if ( P_getreal(CURRENT,"dpwrf2",&dpwrf2,1) < 0 )
            dpwrf2 = 4095.0;
      getstr("dm2",dm2);
      dm2size = strlen(dm2);
      getstr("dmm2",dmm2);
      dmm2size = strlen(dmm2);
      getstr("homo2",homo2);
      homo2size = strlen(homo2);
    }
    else
    {
       dfrq2    = 1.0;
       dmf2     = 1000;
       dof2     = 0.0;
       dres2    = 1.0;
       dseq2[0] = '\000';
       dpwr2    = 0.0;
       dpwrf2   = 0.0;
       strcpy(dm2,"n");
       dm2size  = 1;
       strcpy(dmm2,"c");
       dmm2size = 1;
       strcpy(homo2,"n");
       homo2size= 1;
    }
    if (bgflag)
    {
       if (!getchan)
          fprintf(stderr,"next two lines are default values for chan 3\n");
       fprintf(stderr,"dfrq2 = %10.8lf, dof2 = %10.8lf, dpwr2 = %lf\n",
	   dfrq2,dof2,dpwr2);
       fprintf(stderr,"dmf2 = %10.8lf, dm2='%s',%d, dmm2='%s',%d\n",
	   dmf2,dm2,dm2size,dmm2,dmm2size);
       fprintf(stderr,"homo2='%s',%d\n",homo2,homo2size);
    }

    if ( (tmp=P_getstring(CURRENT, "dn3", tmpstr, 1, 9)) >= 0)
       getchan = TRUE;
    else
       getchan = FALSE;
    /* if "dn3" does not exist, don't bother with the rest of channel 3 */
    getchan = (NUMch > 3) && getchan && (tmpstr[0]!='\000');
    if (getchan)			/* variables associated with 3rd channel */
    {
      dfrq3 = getval("dfrq3");
      dmf3  = getval("dmf3");		/* 3nd decoupler modulation freq */
      dof3  = getval("dof3");
      dres3 = getvalnwarn("dres3");	/* prg decoupler digital resolution */
      if (dres3 < 1.0) dres3 = 1.0;
      getstrnwarn("dseq3",dseq3);
      dpwr3 = ( (cattn[DO3DEV] != 0.0) ? getval("dpwr3") : 0.0 );
      if ( P_getreal(CURRENT,"dpwrm3",&dpwrf3,1) < 0 )
         if ( P_getreal(CURRENT,"dpwrf3",&dpwrf3,1) < 0 )
            dpwrf3 = 4095.0;
      getstr("dm3",dm3);
      dm3size = strlen(dm3);
      getstr("dmm3",dmm3);
      dmm3size = strlen(dmm3);
      getstr("homo3",homo3);
      homo3size = strlen(homo3);
    }
    else
    {
       dfrq3    = 1.0;
       dmf3     = 1000;
       dof3     = 0.0;
       dres3    = 1.0;
       dseq3[0] = '\000';
       dpwr3    = 0.0;
       dpwrf3   = 0.0;
       strcpy(dm3,"n");
       dm3size  = 1;
       strcpy(dmm3,"c");
       dmm3size = 1;
       strcpy(homo3,"n");
       homo3size= 1;
    }
    if (bgflag)
    {
       if (!getchan)
          fprintf(stderr,"next two lines are default values for chan 3\n");
       fprintf(stderr,"dfrq3 = %10.8lf, dof3 = %10.8lf, dpwr3 = %lf\n",
	   dfrq3,dof3,dpwr3);
       fprintf(stderr,"dmf3 = %10.8lf, dm3='%s',%d, dmm3='%s',%d\n",
	   dmf3,dm3,dm3size,dmm3,dmm3size);
       fprintf(stderr,"homo3='%s',%d\n",homo3,homo3size);
    }
}

double getvalnwarn(variable)
char *variable;
{
    int index;

    /* index = findsname(variable,cnames,nnames); */
    index = find(variable);   /* hash table find */
    if (index == NOTFOUND)
    {
        return(0.0);
    }
    return( *( (double *) cvals[index]) );
}


var_active(varname,tree)
char    *varname;
int     tree;
{
    char        strval[20];
    int         ret;
    vInfo       varinfo;                /* variable information structure */

    if ( ret = P_getVarInfo(tree,varname,&varinfo) )
    {   fprintf(stderr,"Cannot find the variable: %s",varname);
        return(-1);
    }
    if (varinfo.basicType != T_STRING)
    {
        if (varinfo.active == ACT_ON)
            return(1);
        else
            return(0);
    }
    else  /* --- variable is string check is value for 'undef' --- */
    {
        P_getstring(tree,varname,strval,1,10);
        /*getparm(varname,"string",tree,&strval[0],10);*/
        if ((strncmp(strval,"unused",6) == NULL) ||
            (strncmp(strval,"Unused",6) == NULL) )
            return(0);
        else
            return(1);
    }
}

getstrnwarn(variable,buf)
char *variable;
char buf[];
{
    int index;

    /* index = findsname(variable,cnames,nnames); */
    index = find(variable);   /* hash table find */
    if (index != NOTFOUND)
    {
        char *content;

        content = ((char *) cvals[index]);
        strncpy(buf,content,MAXSTR-1);
        buf[MAXSTR-1] = 0;
    }
    else
    {
        buf[0] = 0;
    }
}

SetRFChanAttr()
{ }

G_Simpulse()
{ }

rlpwrf()
{ }

hardware_get()
{ }

