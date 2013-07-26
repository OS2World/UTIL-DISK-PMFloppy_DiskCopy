/*
           dskim.c
           
           disk handling routines for pmfloppy.c

           Copyright G. Bryant, 1990

   
  Change Log
	5/95 - ROB Extensive changes to seperate the utility functions (i.e.
		reading and writing) from the tasking functions in readsource() and
		writetarget().  These changes allow essentially the same code to 
		be executed as a subroutine and as a task.

*/

#define VOID void

#define INCL_DOSERRORS
#define INCL_DOSFILEMGR
#define INCL_DOSPROCESS
#define INCL_BASE
#define INCL_DOSDEVIOCTL
#define INCL_DOSSESMGR
#define INCL_DOSMISC
#define INCL_DOSMEMMGR
#include <os2.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pmfloppy.h"
#include "DskIm.h"
#include "funcdefs.h"

/* Global variables ------------------------------------------------------ */

/* PM vbls*/
extern HWND   hWndFrame ;

/* user response vbls*/
extern DskImage ImageBuffers[NUM_IMAGES];

/* Thread variables*/
extern ThreadContext tcThBufs[NUM_THREADS];

/* "local" global vbls*/
ULONG        _fmtData;            /* Used with DSK_FORMATVERIFY         */

/* Global function prototypes*/

/* local function prototypes*/
SHORT    fmttbl_bytessec(USHORT, USHORT *);
SHORT    bspblkcmp(BSPBLK *, BSPBLK *);
VOID     ThreadErrorHandler(USHORT, USHORT, HFILE);
USHORT   SetBufferSel(USHORT);
static PBYTE    MakeHugeP(USHORT, USHORT);




/* Code ------------------------------------------------------------------ */

/* ************************** readsource ****************************** */
/*   Read source disk into memory*/
/**/
/*  when done track data is in huge space starting with selector*/
/*  stored in the global*/
/*  sets global variables:*/
/*     tcThBufs*/
/*     ImageBuffers*/
/**/
VOID APIENTRY readsource(ULONG curTh) {

ULONG		bufferLengthInOut,
			parameterLengthInOut;
BYTE         parmCmd = 1;
USHORT       trk, hd, cyl;
HFILE        dHandle;
ULONG       result;
static CHAR  szdrive[] = "A:";
ULONG        sourceBytes;         /* # bytes on source disk             */
USHORT       sourceTracks;        /* # tracks on source disk            */
USHORT       curBuff;

PBYTE        DskBuf;              /* pointer to track data*/
ULONG		nextPercent = 0;
CHAR		infoBuffer[20];

  curBuff = tcThBufs[curTh].ImageNumber;

  tcThBufs[curTh].ErrorCode = 0;
  /* If this isn't the first time here, free memory from last time first */
  if (ImageBuffers[curBuff].Percent == 100) {
    free(ImageBuffers[curBuff].DskLayout);
	/* if the following code has any problems we will free the code
	   twice unless we take precautions
	*/
	ImageBuffers[curBuff].Percent = 0;
  }

  /* Get source disk parameters */
  DosError(FERR_DISABLEHARDERR);
  szdrive[0] = ImageBuffers[curBuff].DriveID[0];
  tcThBufs[curTh].ErrorCode = DosOpen(szdrive,
                                      &dHandle,
                                      &result,
                                      0L,
                                      0,
                                      FILE_OPEN,
                                      OPENFLAGS,
                                      0L);

  if (tcThBufs[curTh].ErrorCode)
    ThreadErrorHandler(UM_ERROR, curTh, dHandle);

  lockdrive(dHandle);
  if (tcThBufs[curTh].ErrorCode)
    ThreadErrorHandler(UM_ERROR, curTh, dHandle);

parameterLengthInOut = 1;
bufferLengthInOut = sizeof(ImageBuffers[curBuff].DskParms);
  tcThBufs[curTh].ErrorCode = DosDevIOCtl(dHandle,
                                    IOCTL_DISK,
                                    DSK_GETDEVICEPARAMS,
									&parmCmd, 
									parameterLengthInOut, 
									&parameterLengthInOut,
									&ImageBuffers[curBuff].DskParms,
									bufferLengthInOut,
									&bufferLengthInOut
                                    );

  if (!tcThBufs[curTh].ErrorCode) {
    /* Set all the informational variables and build a track layout table
    **  for use with the following sector reads.
    */
    sourceBytes   = (ULONG)(ImageBuffers[curBuff].DskParms.usBytesPerSector) *
                    (ULONG)(ImageBuffers[curBuff].DskParms.cSectors);
    sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors         /
                    ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

    ImageBuffers[curBuff].usLayoutSize = sizeof(TRACKLAYOUT)   +
                          ((2 * sizeof(USHORT)) *
                          (ImageBuffers[curBuff].DskParms.usSectorsPerTrack - 1));

    if (ImageBuffers[curBuff].DskLayout =
        (PTRACKLAYOUT)malloc(ImageBuffers[curBuff].usLayoutSize * sizeof(BYTE))) {
      ImageBuffers[curBuff].DskLayout->bCommand = 1;
      ImageBuffers[curBuff].DskLayout->usFirstSector = 0;
      ImageBuffers[curBuff].DskLayout->cSectors = ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
      for (trk = 0; trk < ImageBuffers[curBuff].DskParms.usSectorsPerTrack; trk++) {
        ImageBuffers[curBuff].DskLayout->TrackTable[trk].usSectorNumber = trk+1;
        ImageBuffers[curBuff].DskLayout->TrackTable[trk].usSectorSize = 
                                                                      ImageBuffers[curBuff].DskParms.usBytesPerSector;
      }
    }
    else
      ThreadErrorHandler(UM_ERROR, curTh, dHandle);



    /* Allocate huge memory to hold the track data*/
    if (tcThBufs[curTh].ErrorCode = SetBufferSel(curBuff))
      ThreadErrorHandler(UM_ERROR, curTh, dHandle);

    /* For each track, set the pointer and read the sector into it*/
    for (trk = 0, cyl = 0;
         trk < sourceTracks;
         trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
      ImageBuffers[curBuff].DskLayout->usCylinder = cyl;

      for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
        ImageBuffers[curBuff].Percent =
          (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
			if(ImageBuffers[curBuff].Percent >= nextPercent)
				{nextPercent += 5;
        WinPostMsg(hWndFrame,UM_STATUS,MPFROMSHORT(curTh),0);
				}
        ImageBuffers[curBuff].DskLayout->usHead = hd;

        DskBuf = MakeHugeP(curBuff,trk+hd);
			parameterLengthInOut = sizeof(ImageBuffers[curBuff].DskLayout);
			bufferLengthInOut = 10000;		/* pick some arbitrary value 
											   and hope for the best  */
        if (tcThBufs[curTh].ErrorCode = DosDevIOCtl(dHandle,
                                    IOCTL_DISK,
                                    DSK_READTRACK,
                                    ImageBuffers[curBuff].DskLayout,
									parameterLengthInOut,
									&parameterLengthInOut,
									DskBuf,
									bufferLengthInOut,
									&bufferLengthInOut
                                    ))
				ThreadErrorHandler(UM_ERROR, curTh, dHandle);
      }
    }
	if (!DosQueryFSInfo ((ULONG) (toupper(ImageBuffers[curBuff].DriveID[0])-'A')+1,
								FSIL_VOLSER, infoBuffer, 20))
		{strcpy (ImageBuffers[curBuff].volumeLabel, infoBuffer+5);
		}
	else
		*(ImageBuffers[curBuff].volumeLabel) = '\0';
    ImageBuffers[curBuff].Percent = 100;
	WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);
  }
  else {			/* error reading drive parameters */
	;		/* NOTHING */
  }
unlockdrive (dHandle);
  if (dHandle) 
	{APIRET		rc;
	UCHAR		parameter = 0,
				data = 0;
	ULONG		parameterLengthInOut = 1,
				dataLengthInOut = 1;

	rc = DosDevIOCtl ( dHandle, 8L, 1L, &parameter, 1L, &parameterLengthInOut,
			&data, 1l, &dataLengthInOut);
 	DosClose(dHandle);
	}
  DosError(FERR_ENABLEHARDERR);
return;
/*  DosExit(EXIT_THREAD,0);*/
}  /* readsource*/

/* *********************** fmttbl_bytessec *************************** */

/* --- Translate bytes per sector into 0-3 code ---
**       the four sector sizes listed below are alluded to in the OS/2
**        docs however only 512 byte sectors are allowed under OS/2 1.x
**       returns the code or -1 and sets DiskError
*/
SHORT fmttbl_bytessec(USHORT bytesPerSec, USHORT *DiskError)
{

  *DiskError = NO_ERROR;
  switch (bytesPerSec)  {
    case 128:  return 0;
    case 256:  return 1;
    case 512:  return 2;
    case 1024: return 3;
  }
  *DiskError = ERROR_BAD_FORMAT;
  return -1;
}


/* ****************************** writetarget ************************* */

/* --- write information read by readsource() onto target disk ---
**       parameter is drive handle as returned by opendrive()
**       checks the target disk, if it's the same format as the source
**        or not formatted at all, write the information contained in
**        DskBuffer formatting if neccessary.
**       returns 0 if successful else errorcode
**
*/
VOID APIENTRY writetarget(ULONG curTh) {

ULONG		parameterLengthInOut,
			bufferLengthInOut;		
BYTE         _parmCmd = 1;
PTRACKFORMAT trkfmt;
USHORT       sizeofTrkfmt;
USHORT       i, trk, hd, cyl, needFormat = FALSE;
HFILE        hf;
ULONG       result;
static CHAR  szdrive[] = "A:";
BSPBLK       targetParms;
USHORT       sourceTracks;        /* # tracks on source disk            */
USHORT       curBuff;
APIRET		rc;
PBYTE        DskBuf;              /* huge pointer to track data*/
ULONG nextPercent = 0;

  curBuff = tcThBufs[curTh].ImageNumber;

  tcThBufs[curTh].ErrorCode = 0;
  /* Get source disk parameters */
  DosError(FERR_DISABLEHARDERR);
  szdrive[0] = ImageBuffers[curBuff].DriveID[0];
  tcThBufs[curTh].ErrorCode = DosOpen(szdrive,
                                      &hf,
                                      &result,
                                      0L,
                                      0,
                                      FILE_OPEN,
                                      OPENFLAGS,
                                      0L);

  if (tcThBufs[curTh].ErrorCode)
    ThreadErrorHandler(UM_ERROR, curTh, hf);

  lockdrive(hf);
  if (tcThBufs[curTh].ErrorCode)
    ThreadErrorHandler(UM_ERROR, curTh, hf);

  /* Get target disk parameters */
bufferLengthInOut = sizeof(targetParms);
parameterLengthInOut = 1;
  tcThBufs[curTh].ErrorCode = DosDevIOCtl(hf,
                                          IOCTL_DISK,
                                          DSK_GETDEVICEPARAMS,
                                          &_parmCmd,
										1, &parameterLengthInOut,
										&targetParms,
										bufferLengthInOut,
										&bufferLengthInOut
                                        );

  if ((tcThBufs[curTh].ErrorCode == ERROR_READ_FAULT) &&
      (ImageBuffers[curBuff].FormatOptions == IDD_WRF_NEVER))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

  if (((tcThBufs[curTh].ErrorCode == ERROR_READ_FAULT) &&
       (ImageBuffers[curBuff].FormatOptions == IDD_WRF_MAYBE)) ||
      (ImageBuffers[curBuff].FormatOptions == IDD_WRF_ALWAYS))  {
    /* If the disk needs formatting we build a format table for it based
    **  on the source disk.
    */
    needFormat = TRUE;
    tcThBufs[curTh].ErrorCode = 0;
    /* Set all the informational variables needed for formatting*/

    sizeofTrkfmt = sizeof(TRACKFORMAT) +
                    ((4 * sizeof(BYTE)) *
                    (ImageBuffers[curBuff].DskParms.usSectorsPerTrack - 1));
    if ((trkfmt = (PTRACKFORMAT)malloc(sizeofTrkfmt * sizeof(BYTE))) == NULL)
      ThreadErrorHandler(UM_ERROR, curTh, hf);

    trkfmt->bCommand = 1;
    trkfmt->cSectors = ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
    for (trk = 0; trk < trkfmt->cSectors; trk++) {
      trkfmt->FormatTable[trk].idSector = (BYTE)(trk+1);
      trkfmt->FormatTable[trk].bBytesSector =
             fmttbl_bytessec(ImageBuffers[curBuff].DskParms.usBytesPerSector,
                             &(tcThBufs[curTh].ErrorCode));
    }
  }
  else if (!tcThBufs[curTh].ErrorCode)
    /* Else if no other error, make sure that the target disk is the same
    **  format as the source.
    */
    if (bspblkcmp(&(ImageBuffers[curBuff].DskParms), &targetParms))
      tcThBufs[curTh].ErrorCode = DSKIM_ERROR_WRONG_FORMAT;

  sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors         /
                  ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  if (!tcThBufs[curTh].ErrorCode) {
    for (trk = 0, cyl = 0; trk < sourceTracks; trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
      ImageBuffers[curBuff].DskLayout->usCylinder = cyl;
      for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
        ImageBuffers[curBuff].Percent =
          (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
			if (ImageBuffers[curBuff].Percent >= nextPercent)
				{nextPercent += 5;
		        WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);
				}
        ImageBuffers[curBuff].DskLayout->usHead = hd;
        if (needFormat)  {
          trkfmt->usHead = hd;
          trkfmt->usCylinder = cyl;
          for (i = 0; i < trkfmt->cSectors; i++) {
            trkfmt->FormatTable[i].bHead = (BYTE)hd;
            trkfmt->FormatTable[i].bCylinder = (BYTE)cyl;
          }

				parameterLengthInOut = sizeof(TRACKFORMAT);
				bufferLengthInOut = sizeof (ULONG);
          if (tcThBufs[curTh].ErrorCode = DosDevIOCtl(hf,
                                                      IOCTL_DISK,
                                                      DSK_FORMATVERIFY,
                                                      trkfmt,
													parameterLengthInOut,
													&parameterLengthInOut,
													&_fmtData,
													bufferLengthInOut,
													&bufferLengthInOut
                                                    ))
            ThreadErrorHandler(UM_ERROR, curTh, hf);
        }
        DskBuf = MakeHugeP(curBuff,trk+hd);
			parameterLengthInOut = sizeof(ImageBuffers[curBuff].DskLayout);
			bufferLengthInOut = 10000;			/* Just a hopeful guess  */
        if (tcThBufs[curTh].ErrorCode = DosDevIOCtl(hf,
                                    IOCTL_DISK,
                                    DSK_WRITETRACK,
                                    ImageBuffers[curBuff].DskLayout,
									parameterLengthInOut,
									&parameterLengthInOut,
									DskBuf,
									bufferLengthInOut,
									&bufferLengthInOut
                                    ))
          ThreadErrorHandler(UM_ERROR, curTh, hf);
      }
    }
    ImageBuffers[curBuff].Percent = 100;
	WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);
    if (needFormat)
		free(trkfmt);
  }
  else {
	;		/* NOTHING */
  }
unlockdrive (hf);
  if (hf) 
	{UCHAR		parameter = 0,
				data = 0;
	ULONG		parameterLengthInOut = 1,
				dataLengthInOut = 1;

	rc = DosDevIOCtl ( hf, 8L, 1L, &parameter, 1L, &parameterLengthInOut,
			&data, 1l, &dataLengthInOut);
 	DosClose(hf);
	}
  DosError(FERR_ENABLEHARDERR);
return;
/*  DosExit(EXIT_THREAD,0);*/
} /*writetarget*/


/* **************************** bspblkcmp ******************************* */

  /* --- compare two BSPBLK structures ---
  **       returns 0 if both are the same except for possibly the
  **        abReserved field, else returns non-zero.
  */
SHORT bspblkcmp(BSPBLK *blk1, BSPBLK *blk2)  {

BSPBLK tmp1, tmp2;

  tmp1 = *blk1;
  tmp2 = *blk2;
  memset(tmp1.abReserved, 0, 6);
  memset(tmp2.abReserved, 0, 6);
  return memcmp(&tmp1, &tmp2, sizeof(BSPBLK));
}

/* ************************** ThreadErrorHandler ************************** */

VOID ThreadErrorHandler(USHORT Msg, USHORT curTh, HFILE dHandle)

{APIRET		rc;
UCHAR		parameter = 0,
			data = 0;
ULONG		parameterLengthInOut = 1,
			dataLengthInOut = 1;

  WinPostMsg(hWndFrame,Msg,MPFROMSHORT(curTh),0);

  if (dHandle)
	{
	rc = DosDevIOCtl ( dHandle, 8L, 1L, &parameter, 1L, &parameterLengthInOut,
			&data, 1l, &dataLengthInOut);
	DosClose(dHandle);
	}
  DosError(FERR_ENABLEHARDERR);
/*return;*/
  DosExit(EXIT_THREAD, 0);
}

/* ************************* SetBufferSel ******************************* */

/* Set the selectors for the buffers to hold to disk data*/
/**/
/* returns errnum if an error occurred, else 0*/
/**/
/* sets DskSel & SelOff in ImageBuffers[curBuff]*/
/**/
USHORT SetBufferSel(USHORT curBuff) {

USHORT TpSeg;     /* Tracks per Segment*/
USHORT RemT;      /* Remaining tracks*/
USHORT tSeg;      /* total segments*/
USHORT srcT;      /* # tracks on disk*/
USHORT bpT;       /* Bytes per track*/
USHORT sCnt;      /* shift count*/
ULONG  rc;        /* return code from AllocHuge*/

rc = 0;
  srcT  = ImageBuffers[curBuff].DskParms.cSectors         /
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
if (ImageBuffers[curBuff].diskBuffer)
	DosFreeMem (ImageBuffers[curBuff].diskBuffer);
rc = DosAllocMem (&(ImageBuffers[curBuff].diskBuffer), srcT*bpT,
	PAG_COMMIT | PAG_READ | PAG_WRITE);
return (rc? rc:	FALSE);

}

/* ***************************** MakeHugeP ******************************* */

static PBYTE MakeHugeP(USHORT curBuff, USHORT Trk) {

USHORT TpSeg;     /* Tracks per Segment*/
USHORT Offs;      /* offset into segment*/
USHORT nSel;      /* number of selector*/
USHORT TSel;      /* Track selector*/
USHORT bpT;       /* Bytes per track*/

  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

return (PCHAR)ImageBuffers[curBuff].diskBuffer + bpT * Trk;


}

/* ********************************* LoadImage *************************** */

/* Load the disk image.*/
/**/
/*  Note that although it is not used here, we will create the layout*/
/*  table as it is needed by writetarget*/
/**/
VOID APIENTRY LoadImage(ULONG curTh) {

USHORT curBuff;
USHORT bpT;            /* Bytes per track*/
USHORT sourceTracks;   /* # tracks on source disk*/
ULONG  sourceBytes;    /* # bytes on source disk*/
USHORT trk;
USHORT hd;
USHORT cyl;
HFILE  hf;
ULONG result;
CHAR   Header[] = "DskImage";
CHAR   NewHead[9];     /* must be size of header*/
PBYTE  DskBuf;         /* huge pointer to track data*/

  curBuff = tcThBufs[curTh].ImageNumber;
  tcThBufs[curTh].ErrorCode = 0;

/* If this isn't the first time here, free memory from last time first */
  if (ImageBuffers[curBuff].Percent == 100) {
    free(ImageBuffers[curBuff].DskLayout);
  }
ImageBuffers[curBuff].volumeLabel[0] = '\0';

  if (tcThBufs[curTh].ErrorCode = DosOpen(ImageBuffers[curBuff].FileName,
                                       &hf,
                                       &result,
                                       0L,
                                       FILE_NORMAL,
                                       FILE_OPEN,
                                       OPEN_ACCESS_READONLY | OPEN_SHARE_DENYWRITE,
                                       NULL
                                       ))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

/* read dskim header*/
  if (tcThBufs[curTh].ErrorCode = DosRead(hf,&NewHead,sizeof NewHead,&result))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

  if (strcmp(Header,NewHead)) {
    tcThBufs[curTh].ErrorCode = DSKIM_ERROR_WRONG_FILE;
    ThreadErrorHandler(UM_ERROR, curTh, hf);
  }

/* read parameter block*/
  if (tcThBufs[curTh].ErrorCode = DosRead(hf,
                                          &ImageBuffers[curBuff].DskParms,
                                          sizeof ImageBuffers[curBuff].DskParms,
                                          &result))
    ThreadErrorHandler(UM_ERROR, curTh, hf);


  sourceBytes   = (ULONG)(ImageBuffers[curBuff].DskParms.usBytesPerSector) *
                  (ULONG)(ImageBuffers[curBuff].DskParms.cSectors);
  sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors /
                  ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  ImageBuffers[curBuff].usLayoutSize = sizeof(TRACKLAYOUT)   +
                        ((2 * sizeof(USHORT)) *
                        (ImageBuffers[curBuff].DskParms.usSectorsPerTrack - 1));

  if (ImageBuffers[curBuff].DskLayout =
      (PTRACKLAYOUT)malloc(ImageBuffers[curBuff].usLayoutSize * sizeof(BYTE))) {
    ImageBuffers[curBuff].DskLayout->bCommand = 1;
    ImageBuffers[curBuff].DskLayout->usFirstSector = 0;
    ImageBuffers[curBuff].DskLayout->cSectors = ImageBuffers[curBuff].DskParms.usSectorsPerTrack;
    for (trk = 0; trk < ImageBuffers[curBuff].DskParms.usSectorsPerTrack; trk++) {
      ImageBuffers[curBuff].DskLayout->TrackTable[trk].usSectorNumber = trk+1;
      ImageBuffers[curBuff].DskLayout->TrackTable[trk].usSectorSize =
                                                                    ImageBuffers[curBuff].DskParms.usBytesPerSector;
    }
  }
  else
    ThreadErrorHandler(UM_ERROR, curTh, hf);



  /* Allocate huge memory to hold the track data*/
  if (tcThBufs[curTh].ErrorCode = SetBufferSel(curBuff))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

/* read disk data*/
  for (trk = 0, cyl = 0;
       trk < sourceTracks;
       trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
    for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
      ImageBuffers[curBuff].Percent =
        (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
      WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);

      DskBuf = MakeHugeP(curBuff,trk+hd);

      if (tcThBufs[curTh].ErrorCode = DosRead(hf,DskBuf,bpT,&result))
        ThreadErrorHandler(UM_ERROR, curTh, hf);
    }
  }
  ImageBuffers[curBuff].Percent = 100;
  
  if (hf) DosClose(hf);
return;
/*  DosExit(EXIT_THREAD,0);*/
}

/* ******************************** SaveImage ************************** */

/* Save the following fields*/
/**/
/*  BSPBLK       DskParms*/
/*  PTRACKLAYOUT DskLayout*/
/*  USHORT       usLayoutSize*/

VOID APIENTRY SaveImage(ULONG curTh) {

USHORT curBuff;
USHORT bpT;            /* Bytes per track*/
USHORT sourceTracks;   /* # tracks on source disk*/
USHORT trk;
USHORT hd;
USHORT cyl;
HFILE  hf;
ULONG result;
ULONG  FileSize;
CHAR   Header[] = "DskImage";
PBYTE  DskBuf;              /* huge pointer to track data*/

  curBuff = tcThBufs[curTh].ImageNumber;
  tcThBufs[curTh].ErrorCode = 0;

/* calculate file size*/
  sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors         /
                  ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  FileSize = (sourceTracks * bpT) +
              sizeof Header +
              sizeof ImageBuffers[curBuff].DskParms;

  if (tcThBufs[curTh].ErrorCode = DosOpen(ImageBuffers[curBuff].FileName,
                                       &hf,
                                       &result,
                                       FileSize,
                                       FILE_NORMAL,
                                       FILE_OPEN | FILE_CREATE,
                                       OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYREADWRITE,
                                       NULL
                                       ))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

/* write dskim header*/
  if (tcThBufs[curTh].ErrorCode = DosWrite(hf,&Header,sizeof Header,&result))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

/* write parameter block*/
  if ((tcThBufs[curTh].ErrorCode = DosWrite(hf,
                                           &ImageBuffers[curBuff].DskParms,
                                           sizeof ImageBuffers[curBuff].DskParms,
                                           &result)))
    ThreadErrorHandler(UM_ERROR, curTh, hf);

/* write disk data*/
  for (trk = 0, cyl = 0;
       trk < sourceTracks;
       trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
    for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
      ImageBuffers[curBuff].Percent =
        (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
      WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);

      DskBuf = MakeHugeP(curBuff,trk+hd);

      if ( (tcThBufs[curTh].ErrorCode = DosWrite(hf,DskBuf,bpT,&result)))
        ThreadErrorHandler(UM_ERROR, curTh, hf);
    }
  }
  ImageBuffers[curBuff].Percent = 100;
  
  if (hf) DosClose(hf);
return;
/*  DosExit(EXIT_THREAD,0);*/
}

/* ******************************** CompImages *************************** */

VOID APIENTRY CompImages(ULONG curTh) {

USHORT curBuff;
USHORT compBuff;
USHORT bpT;            /* Bytes per track*/
USHORT sourceTracks;   /* # tracks on source disk*/
USHORT trk;
USHORT hd;
USHORT cyl;
HFILE  hf;              /* only needed for error handling*/
PBYTE  DskBuf1;         /* huge pointer to track data*/
PBYTE  DskBuf2;         /* huge pointer to track data*/
int		nextPercent = 0;

  curBuff = tcThBufs[curTh].ImageNumber;
  compBuff = tcThBufs[curTh].CompNumber;
  tcThBufs[curTh].ErrorCode = 0;

/* compare BPB*/

  if (memcmp(&(ImageBuffers[curBuff].DskParms),
             &(ImageBuffers[compBuff].DskParms),
             sizeof(BSPBLK))) {
    ThreadErrorHandler(UM_COMPERR, curTh, hf);
	return;
/*    DosExit(EXIT_THREAD,0);*/
  }

/* compare data*/
  sourceTracks  = ImageBuffers[curBuff].DskParms.cSectors         /
                  ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  bpT   = ImageBuffers[curBuff].DskParms.usBytesPerSector *
          ImageBuffers[curBuff].DskParms.usSectorsPerTrack;

  for (trk = 0, cyl = 0;
       trk < sourceTracks;
       trk += ImageBuffers[curBuff].DskParms.cHeads, cyl++) {
    for (hd = 0; hd < ImageBuffers[curBuff].DskParms.cHeads; hd++) {
      ImageBuffers[curBuff].Percent =
        (USHORT)(((float)((cyl*2)+hd)/(float)sourceTracks)*100.0);
		if(ImageBuffers[curBuff].Percent >= nextPercent)
			{nextPercent += 10;
      WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);
			}

      DskBuf1 = MakeHugeP(curBuff,trk+hd);
      DskBuf2 = MakeHugeP(compBuff,trk+hd);

      if (memcmp(DskBuf1,DskBuf2,bpT)) {
        ThreadErrorHandler(UM_COMPERR, curTh, hf);
			return;
/*        DosExit(EXIT_THREAD,0);*/
      }
    }
  }
  ImageBuffers[curBuff].Percent = 100;
  WinPostMsg(hWndFrame,UM_STATUS, MPFROMSHORT(curTh),0);
  WinPostMsg(hWndFrame,UM_COMPOK,MPFROMSHORT(curTh),0);
return;
/*  DosExit(EXIT_THREAD,0);*/
} /* CompImages*/
