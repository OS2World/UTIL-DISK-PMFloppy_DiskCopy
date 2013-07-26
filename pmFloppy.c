/*
           pmfloppy.c
           
           main program

           PM based disk utility.  Use separate threads for disk actions to
           prevent slowing up PM too much.  Use huge memory allocation for
           single pass reads.

           Copyright G. Bryant, 1990

   
  Change Log
   8-Jun-90   Correct array bounds violation in FreeThread
   9-Jun-90   Correct the EXPORTS statement in pmfloppy.def
	5/95 - ROB Extensive changes to seperate the utility functions (i.e.
		reading and writing) from the tasking functions in readsource() and
		writetarget().  These changes allow essentially the same code to 
		be executed as a subroutine and as a task.

*/

#define INCL_PM
#define INCL_BASE
#define INCL_DOSERRORS
#define INCL_DOSPROCESS
#define INCL_DOSDEVIOCTL
#define INCL_DOSSESMGR
#define INCL_GPILCIDS
#define INCL_WINSWITCHLIST
#include <os2.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include "pmfloppy.h"
#include "DskIm.h"
#include "funcdefs.h"

/* prototypes for current file*/
MRESULT  EXPENTRY ClientWndProc(HWND, ULONG, MPARAM, MPARAM);
MRESULT  Panic(PCH, USHORT);
MRESULT  DisplayStatus(HWND, HPS, USHORT);
MRESULT  DisplayDone(HWND , HPS, USHORT);
VOID     FreeThread(USHORT);
static void copyCheckThread (void *param);
static void duplicateThread (void *param);
static FNTHREAD loadImageThread ;
static void readThread (void *param);
static FNTHREAD saveImageThread;
static void writeThread (void *param);
static void startThread (void func (void * param));
static void fileToFloppyThread (void *param);
static void floppyToFileThread (void *param);
VOID     DisplayImageStat(HWND, HPS);
VOID     PutBox(PSZ, PSZ);

/* GLOBAL VARIABLES */

/* PM vbls*/
HAB    hab;
HMQ    hmq;
HWND   hWndFrame ;
static CHAR szClientClass[]="PMFloppy";
ULONG  ctldata = FCF_STANDARD ;
HWND   hWndClient;
HWND   hwndDeskTop;
QMSG   qmsg;
LONG   CharHeight;
LONG   CharWidth;

/* User response vbls*/
DskImage ImageBuffers[NUM_IMAGES];
USHORT   BufferNum;                  /* only use in foreground thread*/
USHORT   CompNum;                    /* only use in foreground thread*/
char	imageFileName[260];

/* Thread variables*/
ThreadContext tcThBufs[NUM_THREADS];


int main(void)
{
SWCNTRL swctl;
PID pid;

  hab = WinInitialize(0);
  hmq = WinCreateMsgQueue(hab, 0);
  hwndDeskTop = WinQueryDesktopWindow(hab, (HDC)NULL);

  if (!WinRegisterClass(hab, szClientClass, ClientWndProc, CS_SIZEREDRAW, 0))
    return(0);

  hWndFrame = WinCreateStdWindow(HWND_DESKTOP,
                                 WS_VISIBLE,
                                 &ctldata,
                                 szClientClass,
                                 "Floppy Disk Utility",
                                 0L,
                                 0,
                                 IDR_PMFLOPPY,
                                 &hWndClient);

  if (hWndFrame != 0)
  {

    WinQueryWindowProcess(hWndFrame, &pid, NULL);

    swctl.hwnd = hWndFrame;
    swctl.hwndIcon = 0;
    swctl.hprog = 0;
    swctl.idProcess = pid;
    swctl.idSession = 0;
    swctl.uchVisibility = SWL_VISIBLE;
    swctl.fbJump = SWL_JUMPABLE;
    swctl.szSwtitle[0] = '\0';


    while ( WinGetMsg(hab, &qmsg, (HWND)NULL, 0, 0) )
	    WinDispatchMsg(hab, &qmsg);

    WinDestroyWindow(hWndFrame);
  }

  WinDestroyMsgQueue(hmq);
  WinTerminate(hab);

  DosExit(EXIT_PROCESS, 0);
} /* main */


/* ClientWndProc - main window processing*/
/**/
/*  note: UM messages are User-defined (in pmfloppy.h).*/
/*        all UM messages use mp2 to indicate the drive the message concerns,*/
/*        and mp1 is data specific to that message.  Then UM_xxxMSG is a */
/*        generic msg.  It is currently only used for DONE messages.  If I*/
/*        need more, set up some magic numbers and pass them through mp1. */
/**/
MRESULT EXPENTRY ClientWndProc(HWND hwnd
			      , ULONG id
			      , MPARAM mp1
			      , MPARAM mp2) {

HPS         hPS;
HWND        hMenu;
CHAR        szTxtBuf[MAXMSGLEN];
int      	result;
FONTMETRICS fm;
USHORT      curBuff;
USHORT      gotSource;
BOOL        DriveActive;
USHORT      curTh;
RECTL       rctPaint;

  switch (id) {
    case WM_CREATE:
      for (curBuff=0;curBuff < NUM_IMAGES;curBuff++)
        ImageBuffers[curBuff].BufferName[0] = '\0';
      for (curTh=0;curTh < NUM_THREADS;curTh++) {
        tcThBufs[curTh].ThID = 0;
        tcThBufs[curTh].ImageNumber = NUM_IMAGES+1;
        tcThBufs[curTh].CompNumber = NUM_IMAGES+1;
      }
      hPS = WinGetPS(hwnd);
      GpiQueryFontMetrics(hPS, (long)sizeof(fm), &fm);
      CharHeight = fm.lMaxBaselineExt;
      CharWidth = fm.lMaxCharInc;
      WinReleasePS(hPS);
      break;

    case WM_PAINT:
      hPS = WinGetPS(hwnd);
      GpiErase(hPS);
      WinQueryUpdateRect(hwnd,&rctPaint);
      DisplayImageStat(hwnd, hPS);
      WinValidateRect(hwnd, &rctPaint, FALSE);
      WinReleasePS(hPS);
      break;

    case WM_COMMAND:
      switch (COMMANDMSG(&id)->cmd) {
        case IDM_READ:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,ReadDlgProc,0,READ_DLG,NULL))
			startThread (readThread);
          break;

        case IDM_WRITE:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,WriteDlgProc,0,WRITE_DLG,NULL))
			startThread (writeThread);
          break;
		case IDM_DUPLICATE:
          if (!WinDlgBox(HWND_DESKTOP,hWndFrame,duplicateDlgProc,0,
				IDD_DUPLICATE,NULL)) 
		  	break;
		  startThread (duplicateThread);
		  break;
		case IDM_COPYCHECK:
          if (!WinDlgBox(HWND_DESKTOP,hWndFrame,copyCheckDlgProc,0,
			IDD_DUPLICATE, NULL)) 
		  	break;
		startThread (copyCheckThread);
		  break;

        case IDM_ABOUT:
          WinDlgBox(HWND_DESKTOP,hWndFrame,AboutDlgProc,0,ABOUT_DLG,NULL);
          break;

        case IDM_DELETE:
          WinDlgBox(HWND_DESKTOP,hWndFrame,DeleteDlgProc,0,DELETE_DLG,NULL);
          hPS = WinGetPS(hwnd);
          GpiErase(hPS);
          DisplayImageStat(hwnd, hPS);
          WinReleasePS(hPS);
          break;

        case IDM_LOAD:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,LoadDlgProc,0,LOAD_DLG,NULL)) {
            /* send off thread to load*/
            for (curTh=0;
                 (curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
                 curTh++) ;

            if (curTh < NUM_THREADS) {
              tcThBufs[curTh].ImageNumber = BufferNum;

              if (DosCreateThread( &(tcThBufs[curTh].ThID),
								loadImageThread, curTh, 0, STACK_SIZE))
               {
					freeImageBuffer(BufferNum);
                PutBox("Image Load","DosCreateThread failed.");
	  	  	      return FALSE;
              } /* if can't create the thread. */
            }
            else {
				freeImageBuffer(BufferNum);
              PutBox("Image Load","Out of program thread resources.");
	  	  	    return FALSE;
            } /* if can't create the thread. */
          }
          break;

        case IDM_SAVE:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,SaveDlgProc,0,SAVE_DLG,NULL)) {
            /* send off thread to save*/
            for (curTh=0;
                 (curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
                 curTh++) ;

            if (curTh < NUM_THREADS) {
              tcThBufs[curTh].ImageNumber = BufferNum;

              if (DosCreateThread( &(tcThBufs[curTh].ThID),
								saveImageThread, curTh, 0, STACK_SIZE))
              {
                PutBox("Image Save","DosCreateThread failed.");
	  	  	      return FALSE;
              } /* if can't create the thread. */
            }
            else {
              PutBox("Image Save","Out of program thread resources.");
	  	  	    return FALSE;
            } /* if can't create the thread. */
          }
          break;

        case IDM_COMP:
          if (WinDlgBox(HWND_DESKTOP,hWndFrame,CompDlgProc,0,COMP_DLG,NULL)) {
            /* send off thread to compare*/
            for (curTh=0;
                 (curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
                 curTh++) ;

            if (curTh < NUM_THREADS) {
              tcThBufs[curTh].ImageNumber = BufferNum;
              tcThBufs[curTh].CompNumber  = CompNum;
              if (DosCreateThread( &(tcThBufs[curTh].ThID),
								CompImages, curTh, 0, STACK_SIZE))
             {
                PutBox("Image Compare","DosCreateThread failed.");
	  	  	      return FALSE;
              } /* if can't create the thread. */
            }
            else {
              PutBox("Image Compare","Out of program thread resources.");
	  	  	    return FALSE;
            } /* if can't create the thread. */
          }
          break;

        case IDM_EXIT:
          WinPostMsg(hWndFrame, WM_CLOSE, NULL, NULL);
          break;
		case IDM_FLOPPY_TO_FILE:
         	if (!WinDlgBox(HWND_DESKTOP,hWndFrame,floppyToFileDlgProc,0,
				IDD_IMAGEDIALOG, NULL)) 
			  	break;
			startThread (floppyToFileThread);
			break;
		case IDM_FILE_TO_FLOPPY:
        	if (!WinDlgBox(HWND_DESKTOP,hWndFrame,fileToFloppyDlgProc,0,
				IDD_IMAGEDIALOG, NULL)) 
			  	break;
			startThread (fileToFloppyThread);
			break;
      }
      break;

    case WM_INITMENU:
      /*set the allowable menu choices.*/
      hMenu = WinWindowFromID(hWndFrame,FID_MENU);

      /* If we don't have anything to write, disable the write, save, and */
      /* delete menus*/
      gotSource = 0;
      for (curBuff=0;curBuff < NUM_IMAGES;curBuff++) {
        if (ImageBuffers[curBuff].Percent == 100) gotSource++;
      }

      WinSendMsg(hMenu,MM_SETITEMATTR,
        MPFROM2SHORT(IDM_WRITE,TRUE),
        MPFROM2SHORT(MIA_DISABLED,gotSource ? 0 : MIA_DISABLED));

      WinSendMsg(hMenu,MM_SETITEMATTR,
        MPFROM2SHORT(IDM_SAVE,TRUE),
        MPFROM2SHORT(MIA_DISABLED,gotSource ? 0 : MIA_DISABLED));

      WinSendMsg(hMenu,MM_SETITEMATTR,
        MPFROM2SHORT(IDM_DELETE,TRUE),
        MPFROM2SHORT(MIA_DISABLED,gotSource ? 0 : MIA_DISABLED));

      WinSendMsg(hMenu,MM_SETITEMATTR,
        MPFROM2SHORT(IDM_COMP,TRUE),
        MPFROM2SHORT(MIA_DISABLED,(gotSource > 1) ? 0 : MIA_DISABLED));
      break;


    case UM_STATUS:
      curTh = SHORT1FROMMP(mp1);
      hPS = WinGetPS(hwnd);
      DisplayStatus(hwnd, hPS, curTh);
      WinReleasePS(hPS);
      break;

    case UM_DONE:
      curTh = SHORT1FROMMP(mp1);
      curBuff = tcThBufs[curTh].ImageNumber;
      hPS = WinGetPS(hwnd);
      WinAlarm(HWND_DESKTOP,WA_NOTE);
      DisplayDone(hwnd, hPS,curBuff);
      FreeThread(curTh);
      WinReleasePS(hPS);
      break;

    case UM_ERROR:
		{int		deleteImage = 0;
		
      curTh = SHORT1FROMMP(mp1);
      curBuff = tcThBufs[curTh].ImageNumber;
      switch (ImageBuffers[curBuff].Busy) {
        case BUSY_READ:
			deleteImage = 1;
          sprintf(szTxtBuf,"Read Error on drive %c", ImageBuffers[curBuff].DriveID[0]);
          break;
        case BUSY_WRITE:
          sprintf(szTxtBuf,"Write Error on drive %c", ImageBuffers[curBuff].DriveID[0]);
          break;
        case BUSY_LOAD:
			deleteImage = 1;
          sprintf(szTxtBuf,"Load Error on drive %s", ImageBuffers[curBuff].FileName);
          break;
        case BUSY_SAVE:
          sprintf(szTxtBuf,"Save Error on file %s", ImageBuffers[curBuff].FileName);
          break;
        }
      Panic(szTxtBuf, tcThBufs[curTh].ErrorCode);
      hPS = WinGetPS(hwnd);
		if(deleteImage)
			{
             /* free the memory used by that buffer and coalesce the list*/
			DosFreeMem (ImageBuffers[curBuff].diskBuffer);
			ImageBuffers[curBuff].diskBuffer = NULL;
			free(ImageBuffers[curBuff].DskLayout);
/* It seems very dangerous to move the buffers in this environment.
   Another task could be running (eg reading an image), and having the
   image buffers shuffled could trash the second thread.
*/
#if 0
			while (((curBuff+1) < NUM_IMAGES) &&
              (ImageBuffers[curBuff].BufferName[0] != '\0'))
				{
				ImageBuffers[curBuff]  = ImageBuffers[curBuff+1];
				curBuff++;
				}
#endif
			ImageBuffers[curBuff].BufferName[0] = '\0';
			GpiErase ( hPS);
			DisplayImageStat (hwnd, hPS);
			}
		else
			{ImageBuffers[curBuff].Busy = 0;
			ImageBuffers[curBuff].Percent = 100;
			DisplayDone(hwnd, hPS, curBuff);
			}
      FreeThread(curTh);
      WinReleasePS(hPS);
      break;
		}

    case UM_COMPOK:
      curTh = SHORT1FROMMP(mp1);
      curBuff = tcThBufs[curTh].ImageNumber;
      WinAlarm(HWND_DESKTOP,WA_NOTE);
      PutBox("Image Compare","Images are identical!");
	ImageBuffers[BufferNum].Busy = 0;
	ImageBuffers[CompNum].Busy = 0;
      hPS = WinGetPS(hwnd);
      DisplayImageStat(hwnd, hPS);
      FreeThread(curTh);
      WinReleasePS(hPS);
      break;

    case UM_COMPERR:
      curTh = SHORT1FROMMP(mp1);
      curBuff = tcThBufs[curTh].ImageNumber;
      WinAlarm(HWND_DESKTOP,WA_WARNING);
      PutBox("Image Compare","Images are different");
      hPS = WinGetPS(hwnd);
      DisplayImageStat(hwnd, hPS);
      FreeThread(curTh);
      WinReleasePS(hPS);
	ImageBuffers[curBuff].Busy = 0;
	ImageBuffers[curBuff].Percent = 100;
      break;
	case UM_DESTDISK:
		PutBox ("Put destination disk", "in drive");
	DosResumeThread ((TID) mp1);
	break;
    case WM_CLOSE:
      DriveActive = FALSE;
      for (curBuff=0;curBuff < NUM_IMAGES;curBuff++)
        if (ImageBuffers[curBuff].Busy) DriveActive = TRUE;

      if (DriveActive) {
	  	  if (MBID_OK != WinMessageBox(HWND_DESKTOP,
                                     hWndFrame,
                                     "are you sure you want to quit?",
                                     "A Drive is still running - ",
                                     0,
                                     MB_OKCANCEL | MB_QUERY))
        break;
      }
      /* else just fall through to default to get to the default proc*/
    default:
      return(WinDefWindowProc(hwnd, id, mp1, mp2));

  } /* switch id */

  return 0L;

} /* clientwndproc */


/*  Panic  --  Put up a message box with an error message.*/
/**/
/*  Inputs:   pszCaption  --  Caption text for message box*/
/**/
/*  Returns:  1L, for error signalling from window procedures.*/
MRESULT Panic(PCH pszCaption,USHORT ErrorNum)
{
CHAR buf[1024];
USHORT cbBuf;

  if (ErrorNum == DSKIM_ERROR_WRONG_FORMAT)
    sprintf(buf, "Error - target disk has incorrect format");
  else if (ErrorNum == DSKIM_ERROR_WRONG_FILE)
    sprintf(buf, "Error - file is not in correct format");
  else 
  {APIRET		rc;
	ULONG		returnedLength;
	
	rc = DosGetMessage (NULL, 0, buf, 1023, ErrorNum, "OSO001.msg", 
		(ULONG *) &cbBuf);
	if (rc)
    	{sprintf(buf, "SYS%04d: error text unavailable", ErrorNum);
	    cbBuf = 31;
		}
  }
  buf[cbBuf] = (char)0;


  WinAlarm(HWND_DESKTOP, WA_ERROR);

  PutBox(pszCaption,buf);

  return( (MRESULT) 1);
} /* panic*/


/* DisplayStatus - Display the status for the drive in the PS*/
/**/
/*  in:  usPct   Percent done*/
/*       Drive   drive letter*/
/*       hwnd    window handle to display on*/
/*       op      operation (eg read, write)*/
/**/
MRESULT DisplayStatus(HWND hwnd, HPS hPS, USHORT curTh) {

RECTL  rctStart;
CHAR   szTxtBuf[MAXMSGLEN];
CHAR   op[10];
CHAR   Object[6];
CHAR   Device[80];
USHORT curBuff;
USHORT compBuff;
int		doPrint = 1;

  curBuff = tcThBufs[curTh].ImageNumber;
  compBuff = tcThBufs[curTh].CompNumber;

  switch (ImageBuffers[curBuff].Busy) {
    case BUSY_READ:
      strcpy(op,"reading");
      strcpy(Object,"Drive");
      Device[0] = ImageBuffers[curBuff].DriveID[0];
      Device[1] = '\0';
      break;
    case BUSY_WRITE:
      strcpy(op,"writing");
      strcpy(Object,"Drive");
      Device[0] = ImageBuffers[curBuff].DriveID[0];
      Device[1] = '\0';
      break;
    case BUSY_LOAD:
      strcpy(op,"loading");
      strcpy(Object,"file");
      strcpy(Device,ImageBuffers[curBuff].FileName);
      break;
    case BUSY_SAVE:
      strcpy(op,"saving");
      strcpy(Object,"file");
      strcpy(Device,ImageBuffers[curBuff].FileName);
      break;
    case BUSY_COMP:
      strcpy(op,"comparing");
      strcpy(Object,"image");
      strcpy(Device,ImageBuffers[compBuff].BufferName);
      break;
	default:
		doPrint = 0;
  }

  rctStart.xLeft = 5L;
  rctStart.xRight = 5L + (CharWidth * MAXMSGLEN);
  rctStart.yBottom = (curBuff * CharHeight) + 5;
  rctStart.yTop = rctStart.yBottom + CharHeight;
if(doPrint)
  sprintf(szTxtBuf,
          "Image %s %s %s %s (%d%% complete)",
          ImageBuffers[curBuff].BufferName,
          op,
          Object,
          Device,
          ImageBuffers[curBuff].Percent);
else
	strcpy (szTxtBuf, " ");
  WinDrawText(hPS, -1, szTxtBuf, &rctStart, CLR_NEUTRAL, CLR_BACKGROUND,
              DT_LEFT | DT_ERASERECT);
  WinValidateRect(hwnd, &rctStart, FALSE);
  return 0;
}


/* DisplayDone - Display an operation complete*/
/**/
/*  in:  Drive   drive letter*/
/*       hwnd    window handle to display on*/
/*       op      operation (eg read, write)*/
/**/
MRESULT DisplayDone(HWND hwnd, HPS hPS, USHORT curBuff) {

RECTL   rctStart;
CHAR    szTxtBuf[MAXMSGLEN];
int		count;

  rctStart.xLeft = 5L;
  rctStart.xRight = 5L + (CharWidth * MAXMSGLEN);
  rctStart.yBottom = (curBuff * CharHeight) + 5;
  rctStart.yTop = rctStart.yBottom + CharHeight;
count =  sprintf(szTxtBuf, "Image %s is full", ImageBuffers[curBuff].BufferName);
sprintf( szTxtBuf+count, " (%s)", ImageBuffers[curBuff].volumeLabel);
  WinDrawText(hPS, -1, szTxtBuf, &rctStart, CLR_NEUTRAL, CLR_BACKGROUND,
              DT_LEFT | DT_ERASERECT);
  WinValidateRect(hwnd, &rctStart, FALSE);
  return 0;
}

/* FreeThread - clean up after a child thread*/
/**/
/*   in: curTh  - Thread to clean up*/
/**/
/* Note: there doesn't seem to be an easy way to determine when a thread is*/
/*       gone.  all the wait routines just work on processes.  So we cheat and*/
/*       check the priority until we get a failure.  Like all busy waits,*/
/*       this is incredibly idiotic, but until we get a "twait" function, I*/
/*       can't think of a better way.*/
/**/
VOID FreeThread(USHORT curTh) {

USHORT curBuff;
USHORT compBuff;
USHORT Prty;

/* sit here until the thread is gone*/
/*  while (DosGetPrty(PRTYS_THREAD,
                    &Prty,
                    tcThBufs[curTh].ThID) != ERROR_INVALID_THREADID) ; */
DosWaitThread(&(tcThBufs[curTh].ThID), 0);
  curBuff  = tcThBufs[curTh].ImageNumber;
  compBuff = tcThBufs[curTh].CompNumber;

  tcThBufs[curTh].ThID = 0;
  tcThBufs[curTh].ImageNumber = NUM_IMAGES+1;
  tcThBufs[curTh].CompNumber = NUM_IMAGES+1;
  tcThBufs[curTh].ErrorCode = 0;
  ImageBuffers[curBuff].Busy = FALSE;
  if (compBuff < NUM_IMAGES) ImageBuffers[compBuff].Busy = FALSE;

}


/* modified 7/94 (ROB) so that image buffers do not have to be contiguous */
VOID DisplayImageStat(HWND hwnd, HPS hPS) {
RECTL  rctStart;
RECTL  rctPaint;
USHORT curBuff;
int		imageCount = 0;

  WinQueryWindowRect(hwnd,&rctPaint);
  for (curBuff=0;curBuff < NUM_IMAGES;curBuff++)
    if (ImageBuffers[curBuff].BufferName[0] != '\0') {
	  ++imageCount;
      if (ImageBuffers[curBuff].Busy) 
		DisplayStatus(hwnd, hPS, curBuff);
      else 
		DisplayDone(hwnd,hPS, curBuff);

      }

  if (!imageCount) {
    rctStart.xLeft = 5L;
    rctStart.xRight = 5L + (CharWidth * 16);
    rctStart.yBottom = 5L;
    rctStart.yTop = rctStart.yBottom + CharHeight;
    WinDrawText(hPS,
                -1,
                "No images in use",
                &rctStart,
                CLR_NEUTRAL,
                CLR_BACKGROUND,
                DT_LEFT | DT_ERASERECT);
  }
}


/**/
/* Put the messages in an application modal message box*/
/**/
VOID PutBox(PSZ msg1, PSZ msg2) {

  WinMessageBox(HWND_DESKTOP,
                hWndFrame,
                msg2,
                msg1,
                0,
                MB_OK | MB_ICONEXCLAMATION);
  return;
} /* PutBox*/

/* *************************** freeImageBuffer ************************** */

void	freeImageBuffer (USHORT	buffer)
{
ImageBuffers[buffer].BufferName[0] = '0';
ImageBuffers[buffer].Busy = 0;
return;
}

/* *************************** copyCheckThread ***************************** */
static void copyCheckThread (void *param)
{int		holdSource,
			imageNum,
			thIndex;
ThreadContext	*pcontext;

thIndex = (int) param;
pcontext = &(tcThBufs[thIndex]);
imageNum = pcontext->ImageNumber;
ImageBuffers[imageNum].Busy = BUSY_WRITE;
writetarget (thIndex);
WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(thIndex),0);
holdSource = pcontext->ImageNumber;
pcontext->ImageNumber = pcontext->CompNumber;
ImageBuffers[pcontext->ImageNumber].Busy = BUSY_READ;
readsource (thIndex);
WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(thIndex),0);
/* See the comment in duplicateThread () for an explaination of the following
   DosSleep ()
*/
DosSleep (50);
pcontext->ImageNumber = holdSource;
ImageBuffers[pcontext->ImageNumber].Busy = BUSY_COMP;
CompImages (thIndex);
tcThBufs[thIndex].ThID = 0;
_endthread();
}

/* *************************** duplicateThread *************************** */
static void duplicateThread (void *param)
{int			holdSource,
				imageNum,
				thID;
ThreadContext	*pcontext;

thID = (int)param;
pcontext = &(tcThBufs[thID]);
imageNum = pcontext->ImageNumber;
ImageBuffers[imageNum].Busy = BUSY_READ;
readsource (thID);
WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(thID),0);
WinPostMsg (hWndFrame, UM_DESTDISK, (MPARAM)(pcontext->ThID), NULL);
DosSuspendThread(pcontext->ThID);
ImageBuffers[imageNum].Busy = BUSY_WRITE;
writetarget (thID);
WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(thID),0);
holdSource = pcontext->ImageNumber;
pcontext->ImageNumber = pcontext->CompNumber;
ImageBuffers[pcontext->ImageNumber].Busy = BUSY_READ;
readsource (thID);
WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(thID),0);
/* the final percent complete message is not being processed until the
   results of the image comparrison are posted with the result that it looks
   to the user that reading of the new disk is not complete.  The following
   DosSleep () gives the message processing thread time to post the final
   percentage.
*/
DosSleep (50);
pcontext->ImageNumber = holdSource;
ImageBuffers[pcontext->ImageNumber].Busy = BUSY_COMP;
CompImages (thID);
_endthread();
}

/* ************************** loadImageThread ****************************** */
static void APIENTRY loadImageThread (ULONG param)
{
LoadImage (param);
WinPostMsg (hWndFrame, UM_DONE, (MPARAM) param, NULL);
_endthread();
}

/* ****************************** readThread **************************** */
static void readThread (void *param)
{int		thIndex;

thIndex = (int)param;
readsource (thIndex);
if (tcThBufs[thIndex].ErrorCode)
	WinPostMsg(hWndFrame,UM_ERROR,MPFROMSHORT(thIndex),0);
else
	WinPostMsg(hWndFrame,UM_DONE,MPFROMSHORT(thIndex),0);
ImageBuffers[tcThBufs[thIndex].ImageNumber].Busy = 0;
_endthread();

}

/* ************************** saveImageThread ****************************** */
static void APIENTRY saveImageThread (ULONG param)
{
SaveImage ( param);
WinPostMsg (hWndFrame, UM_DONE, (MPARAM) param, NULL);
_endthread();
}

/* ****************************** writeThread **************************** */
static void writeThread (void *param)
{int		thIndex;

thIndex = (int)param;
writetarget (thIndex);
if (tcThBufs[thIndex].ErrorCode)
	{
	WinPostMsg(hWndFrame,UM_ERROR,MPFROMSHORT(thIndex),0);
	}
else
	WinPostMsg(hWndFrame,UM_DONE,MPFROMSHORT(thIndex),0);
ImageBuffers[tcThBufs[thIndex].ImageNumber].Busy = 0;
_endthread ();
}

/* *************************** startThread *************************** */
static void startThread (void func (void * param))
{int		curTh;

/* send off thread to read*/
for (curTh=0;	(curTh < NUM_THREADS) && (tcThBufs[curTh].ThID != 0);
				 curTh++)
	;			/* NOTHING */

if (curTh < NUM_THREADS)
	{tcThBufs[curTh].ImageNumber = BufferNum;
	tcThBufs[curTh].CompNumber = CompNum;
	if ((tcThBufs[curTh].ThID = _beginthread (func, NULL, 4*4096, (void *)curTh)) == -1)
		{freeImageBuffer(BufferNum);
		PutBox("Drive Read","DosCreateThread failed.");
		return;
		} /* if can't create the thread. */
	}
else
	{freeImageBuffer(BufferNum);
	PutBox("Drive Read","Out of program thread resources.");
	return;
	} /* if can't create the thread. */
return;
}

/* ************************** floppyToFileThread *************************** */
static void floppyToFileThread (void *param)
{
int		imageNum,
		thID;
ThreadContext	*pcontext;

thID = (int)param;
pcontext = &(tcThBufs[thID]);
imageNum = pcontext->ImageNumber;
ImageBuffers[imageNum].Busy = BUSY_READ;
readsource (thID);
WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(thID),0);
SaveImage (thID);
tcThBufs[thID].ThID = 0;
ImageBuffers[imageNum].Busy = 0;
WinPostMsg (hWndFrame, UM_DONE, (MPARAM) param, NULL);
_endthread ();
}

/* *************************** fileToFloppyThread ************************** */
static void fileToFloppyThread (void *param)
{
int				holdSource,
				imageNum,
				thID;
ThreadContext	*pcontext;

thID = (int) param;
pcontext = &(tcThBufs[thID]);
imageNum = pcontext->ImageNumber;
/* ImageBuffers[imageNum].Busy = BUSY_LOAD; */

#if 1
LoadImage (thID);
#endif

ImageBuffers[imageNum].Busy = BUSY_WRITE;
writetarget (thID);
WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(thID),0);
DosSleep (50);
ImageBuffers[imageNum].Busy = 0;
holdSource = pcontext->ImageNumber;
pcontext->ImageNumber = pcontext->CompNumber;
ImageBuffers[pcontext->ImageNumber].Busy = BUSY_READ;
readsource (thID);
ImageBuffers[pcontext->ImageNumber].Busy = 0;
WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(thID),0);
/* See the comment in duplicateThread () for an explaination of the following
   DosSleep ()
*/
DosSleep (50);
pcontext->ImageNumber = holdSource;
ImageBuffers[pcontext->ImageNumber].Busy = BUSY_COMP;
CompImages (thID);
ImageBuffers[pcontext->ImageNumber].Busy = BUSY_COMP;


tcThBufs[thID].ThID = 0;
WinPostMsg (hWndFrame, UM_DONE, (MPARAM) param, NULL);
_endthread ();
}

