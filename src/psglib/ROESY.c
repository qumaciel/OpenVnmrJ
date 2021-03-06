// Copyright (C) 2015  University of Oregon
// You may distribute under the terms of either the GNU General Public
// License or the Apache License, as specified in the LICENSE file.
// For more information, see the LICENSE file.

/* ROESY - cross-relaxation experiment in rotating frame

	Features included:
		States-TPPI in F1
		z-filter with flipback option
		
	Paramters:
		sspul :		selects magnetization randomization option
		mixR	:	ROESY spinlock mixRing time
		slpwrR	:	spin-lock power level
		slpwR	:	90 deg pulse width for spinlock
		slpatR	:	Spinlock pattern [cw and troesy]
		zfilt	:	selects z-filter with flipback option
				[requires gradient and PFGflg=y]

************************************************************************
****NOTE:  v20,v21,v22,v23 and v24 are used by Hardware Loop and reserved ***
************************************************************************

KrishK	-	Last revision	: June 1997
KrishK	-	Revised		: July 2004
KrishK  -       Includes slp saturation option : July 2005
KrishK - includes purge option : Aug. 2006
****v17,v18,v19 are reserved for PURGE ***

*/



#include <standard.h>
#include <chempack.h>

extern int dps_flag;

static int ph1[4] = {1,3,2,0},
	   ph2[8] = {3,3,0,0,1,1,2,2},  /*troesy spinlock */
           ph3[8] = {1,3,2,0,3,1,0,2},
           ph8[4] = {3,3,0,0},
           ph6[8] = {1,1,2,2,3,3,0,0};

pulsesequence()
{
   double          slpwrR = getval("slpwrR"),
                   slpwR = getval("slpwR"),
                   mixR = getval("mixR"),
                   gzlvlz = getval("gzlvlz"),
                   gtz = getval("gtz"),
		   zfphinc = getval("zfphinc");
   char		   slpatR[MAXSTR];
   int             phase1 = (int)(getval("phase")+0.5),
                   prgcycle = (int)(getval("prgcycle")+0.5);

/* LOAD AND INITIALIZE PARAMETERS */
   getstr("slpatR",slpatR);

   if (strcmp(slpatR,"cw") &&
        strcmp(slpatR,"troesy") &&
        strcmp(slpatR,"dante"))
        abort_message("SpinLock pattern %s not supported!.\n", slpatR);

  assign(ct,v17);
  assign(zero,v18);
  assign(zero,v19);

  if (getflag("prgflg") && (satmode[0] == 'y') && (prgcycle > 1.5))
    {
        hlv(ct,v17);
        mod2(ct,v18); dbl(v18,v18);
        if (prgcycle > 2.5)
           {
                hlv(v17,v17);
                hlv(ct,v19); mod2(v19,v19); dbl(v19,v19);
           }
     }

   settable(t1,4,ph1);	getelem(t1,v17,v1);
   settable(t2,8,ph2);	getelem(t2,v17,v20);
   settable(t3,8,ph3);	
   settable(t8,4,ph8);	getelem(t8,v17,v8);
   settable(t6,8,ph6);	getelem(t6,v17,v6);
  
   assign(v1,oph); 
   if (getflag("zfilt"))
	getelem(t3,v17,oph);

   assign(v20,v9);
   if (!strcmp(slpatR,"troesy"))
	assign(v20,v21);
   else
	add(v20,one,v21);

   add(oph,v18,oph);
   add(oph,v19,oph);

  if (getflag("prgflg") && (satmode[0] == 'y'))
	assign(v1,v6);
   if (phase1 == 2)
      {incr(v1); incr(v6);}			/* hypercomplex method */

/*
   mod2(id2,v13);
   dbl(v13,v13);
*/
  initval(2.0*(double)(((int)(d2*getval("sw1")+0.5)%2)),v13);

       add(v1,v13,v1);
       add(v6,v13,v6);
       add(oph,v13,oph);

/* The following is for flipback pulse */
   zfphinc=zfphinc+180;
   if (zfphinc < 0) zfphinc=zfphinc+360;
   initval(zfphinc,v10);

/* BEGIN ACTUAL PULSE SEQUENCE */
   status(A);
      obspower(tpwr);

   delay(5.0e-5);
   if (getflag("sspul"))
        steadystate();

   if (satmode[0] == 'y')
     {
        if ((d1-satdly) > 0.02)
                delay(d1-satdly);
        else
                delay(0.02);
        if (getflag("slpsat"))
           {
                shaped_satpulse("relaxD",satdly,v6);
                if (getflag("prgflg"))
                   shaped_purge(v1,v6,v18,v19);
           }
        else
           {
                satpulse(satdly,v6,rof1,rof1);
                if (getflag("prgflg"))
                   purge(v1,v6,v18,v19);
           }
     }
   else
        delay(d1);

   if (getflag("wet"))
     wet4(zero,one);
   decpower(dpwr);

   status(B);
      rgpulse(pw, v1, rof1, rof1);
      if (d2 > 0.0)
       delay(d2 - POWER_DELAY - (2*pw/PI) - rof1);
      else
       delay(d2);
      
      obspower(slpwrR);

      if (mixR > 0.0)
      {
        if (dps_flag)
          	rgpulse(mixR,v21,0.0,0.0);
        else
          SpinLock(slpatR,mixR,slpwR,v21);
      }

       if ((getflag("zfilt")) && (getflag("PFGflg")))
        {
           obspower(tpwr);
           rgpulse(pw,v9,1.0e-6,rof1);
           zgradpulse(gzlvlz,gtz);
           delay(gtz/3);
           if (getflag("flipback"))
                FBpulse(v8,v10);
           rgpulse(pw,v8,rof1,rof2);
        }
       else
/* Add 2*pw/PI to final rof2, so ddrpm can be set to
   a value of 'p' for both zfilt options */

           delay(rof2+(2*pw/PI));

   status(C);
}
