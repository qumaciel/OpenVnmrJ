/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */
/* 
 */
/*
modification history
--------------------
8-5-04,gmb  created 
*/

/*
DESCRIPTION

   This Task Handlers the Problems or Exceptions that happen to the
   system. For Example Fifo Errors (FORP,FOO,etc).

   The logic for handling these execption is encapsulated in this task
   thus there is one central place where the logic is placed.

*/

#define _POSIX_SOURCE /* defined when source is to be POSIX-compliant */
#include <string.h>
#include <vxWorks.h>
#include <stdioLib.h>
#include <rngLib.h>
#include <semLib.h>
#include <msgQLib.h>
#include <wdLib.h>
#include "hostAcqStructs.h"
#include "expDoneCodes.h"
#include "errorcodes.h"
#include "logMsgLib.h"
#include "taskPriority.h"
#include "sysflags.h"
#include "instrWvDefines.h"
#include "Cntlr_Comm.h"
#include "Monitor_Cmd.h"
#include "upLink.h"
#include "fifoFuncs.h"
#include "cntlrStates.h"

#define HOSTNAME_SIZE 80
extern char hostName[HOSTNAME_SIZE];

extern SEM_ID  pSemSAStop; /* Binary  Semaphore used to Stop upLinker for SA */

/* extern MSG_Q_ID pUpLinkMsgQ;	/* MsgQ used between UpLinker and STM Object */
extern MSG_Q_ID pMsgesToPHandlr;/* MsgQ for Msges to Problem Handler */
extern MSG_Q_ID pDataTagMsgQ;   /* MsgQ used for Sending msg to recvproc upLink task */


Monitor_Cmd ExceptionMsg =  { 0, 0, 0, 0, 0, 0, 0, 0, 0 } ; 

extern RING_ID  pSyncActionArgs;  /* Buffer for 'Sync Action' (e.g. SETVT) function args */

/* Fixed & Dynamic Named Buffers */

extern int     SA_Criteria;/* SA, EXP_FID_CMPLT, BS_CMPLT, IL_CMPLT */
extern unsigned long SA_Mod; /* modulo for SA, ie which fid to stop at 'il'*/

static void resetSystem();
static void reset2SafeState();
static void resetNameBufs();
static void AbortExp(Cntlr_Comm *msge);

static int pHandlerTid;
static int pHandlerPriority;

WDOG_ID pHandlerWdTimeout;
int pHandlerTimeout;


startPhandler(int priority, int taskoptions, int stacksize)
{
   int pHandler();

   if (taskNameToId("tPHandlr") == ERROR)
   {
     pHandlerPriority = priority;
     pHandlerTid = taskSpawn("tPHandlr",priority,taskoptions,stacksize,pHandler,
		   pMsgesToPHandlr,2, 3,4,5,6,7,8,9,10);
   
     pHandlerWdTimeout = wdCreate();
     pHandlerTimeout = 0;
   }

}

killPhandler()
{
   int tid;
   if ((tid = taskNameToId("tEHandlr")) != ERROR)
	taskDelete(tid);
}

pHandlerWdISR()
{
   pHandlerTimeout = 1;
   wdCancel(pHandlerWdTimeout);
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_WDISR,NULL,NULL);
#endif
}

/* 
 * This routine is called from the context of the NDDS task n_rtu7400
 * 
 *   Author: Greg Brissey    8/04/2004
 */
static void ExceptionPub_CallBack(Cntlr_Comm *recvIssue)
{

    /* in the future we might do a few test and call differnet routines but for
       now we just relay the info on to a seperate Task to handle exceptions
       so NDDS can get on with it's business of receiving issue
    */
    /* if this was a exception message initiated from this controller then a msge was
     * already directly sent to the phandler, thus we can safely ignore this message
     */
     if (strcmp(recvIssue->cntlrId,hostName) != 0)
     {
       /* send error to exception handler task, it knows what to do */
       msgQSend(pMsgesToPHandlr, (char*) recvIssue,
                sizeof(Cntlr_Comm), NO_WAIT, MSG_PRI_NORMAL);
     }
     else
     {
          DPRINT1(-1,"NDDS callback for Local Exception from '%s', ignored.\n",recvIssue->cntlrId);
     }

}

/*
 * The controller sends it's exception directly to it's phandler via this call
 * the NDDS callback Exception subscription handler will ignore the Exception coming
 * from itself
 */
sendExceptionDirectly(Cntlr_Comm *recvIssue)
{
    /* send error to exception handler task, it knows what to do */
    msgQSend(pMsgesToPHandlr, (char*) recvIssue,
                sizeof(Cntlr_Comm), NO_WAIT, MSG_PRI_NORMAL);
}

initExceptionHandler()
{
   /* initialize msgQ used by Master or Controller comm channel */
   pMsgesToPHandlr = msgQCreate(20, sizeof(Cntlr_Comm), MSG_Q_PRIORITY);

   startPhandler(PHANDLER_PRIORITY, STD_TASKOPTIONS, STD_STACKSIZE);

    /* bring NDDS exception Pub/Sub for controller */
    initialExceptionComm((void *)ExceptionPub_CallBack);
}

/*************************************************************
*
*  exceptionHandler - Wait for Messages that indicate some problem
*    with the system. Then perform appropriate recovery.
*
*					Author Greg Brissey 12-7-94
*/
pHandler(MSG_Q_ID msges)
{
   Cntlr_Comm msge;
   int *val;
   int bytes;
   void recovery(Cntlr_Comm *);
   void extrecovery(Cntlr_Comm *);

   DPRINT(1,"pHandler :Server LOOP Ready & Waiting.\n");
   FOREVER
   {
     markReady(PHANDLER_FLAGBIT);
     memset( &msge, 0, sizeof( Cntlr_Comm ) );
     bytes = msgQReceive(pMsgesToPHandlr, (char*) &msge, 
			  sizeof( Cntlr_Comm ), WAIT_FOREVER);
     markBusy(PHANDLER_FLAGBIT);
     DPRINT5(-1,"'%s': pHandler: recv: %d bytes, From Cntlr: '%s', Exception Type: %d, Event: %d \n",
			hostName,bytes, msge.cntlrId, msge.cmd, msge.arg1);

     if (strcmp(msge.cntlrId,hostName) == 0)
     {
        recovery( &msge );
     }
     else
     {
        extrecovery( &msge );
	DPRINT(-1,"From another Controller, No action yet.\n");
     }
     /* prevent false starts from Sync glitches, controller reboots, or FPGA reloads */
     cntrlFifoClearStartMode();
   } 
}

enableInterrupts()
{
}

/*********************************************************************
*
* recovery, based on the cmd code decides what recovery action 
*		to perform.
*
*					Author Greg Brissey 12-7-94
*/
void recovery(Cntlr_Comm *msge)
{
   char *token;
   int len;
   int cmd;
   int i,nMsg2Read;
   int bytes;
   PUBLSH_MSG pubMsg;

   switch( msge->cmd /* exceptionType */ )
   {

      case WARNING_MSG:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_WARNMSG,NULL,NULL);
#endif
  	        DPRINT2(0,"WARNING: doneCode: %d, errorCode: %d\n",
			msge->cmd,msge->errorcode);
		break;

      case HARD_ERROR:

#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_HARDERROR,NULL,NULL);
#endif
  	        DPRINT2(0,"HARD_ERROR: doneCode: %d, errorCode: %d\n", msge->cmd, msge->errorcode);

		AbortExp(msge);

  	        DPRINT(0,"Done");
		break;
		
      case EXP_HALTED:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_EXPHALTED,NULL,NULL);
#endif

        /* This would tenitively be where the DDR has decided that */
          break;

      case STOP_CMPLT:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_STOPCMPLT,NULL,NULL);
#endif
        /* This would tenitively be where the DDR has decided that */
        /* the stop criteria has been meet  */
        break;

      case ALLOC_ERROR:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_ALLOCERROR,NULL,NULL);
#endif
          DPRINT(-1,"Memory allocation Failure\n");
		break;
      default:
		/* Who Cares */
    		errLogRet(LOGIT,debugInfo,
       		"phandler: Invalid Exception Type: %d, Event: %d", 
			msge->cmd, msge->errorcode);
		break;
    }

}


/*********************************************************************
*
* external exception recovery, based on the cmd code decides what recovery action 
*		to perform.
*
*					Author Greg Brissey 8-5-2004
*/
void extrecovery(Cntlr_Comm *msge)
{
   char *token;
   int len;
   int cmd;
   int i,nMsg2Read;
   int bytes;
   PUBLSH_MSG pubMsg;
   extern int systemConRestart();
   static void resetNameBufs();

   switch( msge->cmd /* exceptionType */ )
   {
      case PANIC:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_PANIC,NULL,NULL);
#endif
    		errLogRet(LOGIT,debugInfo,
       		"phandler: Panic Error: %d", msge->errorcode);
		break;

      case WARNING_MSG:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_WARNMSG,NULL,NULL);
#endif
  	        DPRINT2(0,"WARNING: doneCode: %d, errorCode: %d\n",
			msge->cmd,msge->errorcode);

		break;

   
      case SOFT_ERROR:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_SOFTERROR,NULL,NULL);
#endif

  	        DPRINT2(0,"SOFT_ERROR: doneCode: %d, errorCode: %d\n",
			msge->cmd,msge->errorcode);
		break;

      case EXP_ABORTED:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_EXPABORTED,NULL,NULL);
#endif
  	        DPRINT(0,"Exp. Aborted");
                /* drops down into the HARD_ERROR case */

      case HARD_ERROR:

#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_HARDERROR,NULL,NULL);
#endif
  	        DPRINT2(0,"HARD_ERROR: doneCode: %d, errorCode: %d\n", msge->cmd, msge->errorcode);

		AbortExp(msge);

  	        DPRINT(0,"Done");
		break;
		

      case INTERACTIVE_ABORT:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_HARDERROR,NULL,NULL);
#endif
		break;


      case EXP_HALTED:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_EXPHALTED,NULL,NULL);
#endif
		break;


      case STOP_CMPLT:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_STOPCMPLT,NULL,NULL);
#endif
		break;


      case ALLOC_ERROR:
#ifdef INSTRUMENT
        wvEvent(EVENT_PHDLR_ALLOCERROR,NULL,NULL);
#endif
		break;
      
      case WATCHDOG:
		break;

      default:
    		errLogRet(LOGIT,debugInfo,
       		"phandler: Invalid Exception Type: %d, Event: %d", 
			msge->cmd, msge->errorcode);
		break;
   }
/*  INOVA
   while ( (bytes = msgQReceive(pMsgesToPHandlr, (char*) &msge, sizeof( Cntlr_Comm ), NO_WAIT)) != ERROR )
   {
     DPRINT1(0,"Read %d bytes from Phandler msgQ, (Bogus Errors at this point)\n",bytes);
   }
*/
}

static
void AbortExp(Cntlr_Comm *msge)
{
   PUBLSH_MSG pubMsg;

   /* donecode = EXP_ABORTED or  HARD_ERROR plus errorCode(FOO,etc..) */
   /* need to get this msge up to expproc as soon as possible since the
      receipt of this msge cause expproc to send the SIGUSR2 (abort cmd)
      to Sendproc, without this msge all the acodes will be sent down.
   */
   /* EXP_ABORT or HARD_ERROR */

   /* ------------------------------------------------- */

   /* Controller output from FIFO to Software registers */
   setFifoOutputSelect(SELECT_SW_CONTROLLED_OUTPUT);

   /* force values that must serialized out to whomever */
   setAbortSerializedStates();

   /* clear Experiment downld codes,patterns,tables, etc.
    * and restart parser
    *
    */
    DPRINT(-1,"restart AParser\n");
    AParserAA();

    /* resetExpBufsAndParser(); replaced with AParserAA above */

    DPRINT(-1,"Waiting for Parser to be ready\n");
    wait4ParserReady();

   /* Disable DMA, and clear any Queued DMA transfers */
   abortFifoBufTransfer();

   /* reset FIFO */
   cntrlFifoReset();

    freeAllDwnldBufsSynced();   /* free download buffer after dwnld completion msg recv'd */

   /* ------------------------------------------------- */
   send2Master(CNTLR_CMD_STATE_UPDATE,CNTLR_EXCPT_CMPLT,0,0,NULL);
   DPRINT(-1,"sent Master Exception Completed State\n");

   return;
}


static
void resetSystem()
{
  /* abort Parser */
   /* clear PARALLEL_CHANS */
   /* abort Updater task */
   /* abort shandler task */
   return;
}

/*
   WARNING: Not to be used by any other tasks than phandler
     1. Stop Parser
     2. Stop DownLinker
     3. Stuff Fifo with safe states and run
     4. Give-n-Take Serial Port devices Mutex to allow serial cmd to finish
     5. Free Name buffers
     6. Raise Priority of DownLink to above phandler and resume task if suspend & wait, lower priority
     7. Raise Priority of Parser to above phandler and resume task if suspend & wait, lower priority
     Last three steps allows for an orderly and efficient clean up of resources (name buffers,etc.)
*/

/* reprogram HSlines, ap registers to safe state */

static void
reset2SafeState( int why )  /* argument is the type of exception */
{
   /* parallel channel sort to stop and return to APint() */
   /* like stopAPint() but also cause the Parser to suspend itself */
   /* like stop downLinker and set to dump further download to bit bucket */
   /* stop the stuffer in it's tracks */
   /* reset Data Object not to use parallel channels */

   /* Keep the MTS gradient amp disabled whenever possible! */

   /* this now clear the Fifo Buffer & restart the Stuffer */
   /* set initial value of parallel channel free buffer pointer to null */
  /* use Post sequence to fill fifo with safe states */
   return;
}

/*----------------------------------------------------------------------*/
/* resetNameBufs							*/
/*	Removes named buffers.  Lowers priority in case downlinker is	*/
/*	pending on new buffers (e.g. interleaving). Waits then removes	*/
/*	some more.							*/
/*----------------------------------------------------------------------*/
static
void resetNameBufs()   /* free all named buffers */
{
}

/* programs safe HS line, turns off WFG, etc, set lock filters,etc.. */
/* WARNING this function will changed the calling task priority if it is
   lower than the Stuffer, to greater than the stuffer while putting fifo words
   into the fifo buffer 
*/
void set2ExpEndState()  /* reprogram HSlines, ap registers to safe state, Normal end */
{
   /* Keep the MTS gradient amp disabled whenever possible! */
   /* Lower priority to allow stuffer in, if needed  */
   /* if priority <= to that of the stuffer then lower the priority of this task */
   /* thus allowing the stuffer to stuff the fifo */
   /* activate backgound shimming if enabled */
   return;
}
