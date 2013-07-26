/* DskImage 1.0, Copyright 1990, Greg Bryant*/
/**/

#define BSPBLK BIOSPARAMETERBLOCK

#define BUFFERNMSZ  8
#define VOLUMENMSZ  11
#define FILENMSZ    256

#define BUSY        0xFFFF
#define BUSY_READ   0x0001
#define BUSY_WRITE  0x0002
#define BUSY_SAVE   0x0004
#define BUSY_LOAD   0x0008
#define BUSY_COMP   0x0010

static ULONG		dummyParamInOut,
					dummyDataInOut;
/* Note, for the lockdrive/unlockdrive macros, the global variable _lockCmd*/
/* must be accessable and set to zero!*/

#define lockdrive(hf)   (tcThBufs[curTh].ErrorCode = DosDevIOCtl(hf, IOCTL_DISK, DSK_LOCKDRIVE, NULL, 0, NULL, NULL, 0, NULL))
#define unlockdrive(hf) (tcThBufs[curTh].ErrorCode = DosDevIOCtl(hf, IOCTL_DISK, DSK_UNLOCKDRIVE, NULL, 0, &dummyParamInOut, NULL, 0, &dummyDataInOut))

typedef struct _DskImage {
  USHORT       Percent;      /* percent completion*/
  BOOL         Busy;         /* Busy flag*/
  CHAR         DriveID[1];   /* Drive letter*/
  USHORT       FormatOptions;/* Bit map indicating formatting choice*/
  CHAR         Volume[VOLUMENMSZ];    /* Volume Name*/
  CHAR         BufferName[BUFFERNMSZ];/* Image Buffer Name*/
  CHAR         FileName[FILENMSZ];    /* Image file Name*/
  BIOSPARAMETERBLOCK  DskParms;     /* from sourceParms*/
  SEL          DskSel;       /* selector for huge pointer to track buffer*/
  USHORT       SelOff;       /* huge selector offset*/
  PTRACKLAYOUT DskLayout;    /* from sourceLayout*/
  USHORT       usLayoutSize; /* from sizeofLayoutElement*/
	char	volumeLabel[12];
  PVOID			diskBuffer;	/* put the diskette data here */
} DskImage;
