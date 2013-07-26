/* pmfloppy.h */
#ifndef PMFLOPPY_H
#define PMFLOPPY_H

#define NUM_IMAGES  20
#define NUM_THREADS 5
#define MAXMSGLEN   127
#define STACK_SIZE	4096
#define MAX_SEG     65536
#define READ_SOURCE 1
#define COPY_TARGET 2
#define EXIT_DSKCPY -1

#define DSKIM_ERROR_WRONG_FORMAT 0xffbf
#define DSKIM_ERROR_WRONG_FILE   0xffc0

#define BUFSIZE   1024
#define OPENFLAGS (OPEN_FLAGS_DASD | OPEN_SHARE_DENYREADWRITE | OPEN_ACCESS_READWRITE)

/* Resource IDs*/
#define IDR_PMFLOPPY  1


/* Menu IDs*/
#define IDM_DISK_MENU	  2050
#define IDM_READ        2052
#define IDM_WRITE       2053
#define IDM_ABOUT       2054
#define IDM_LOAD        2055
#define IDM_SAVE        2056
#define IDM_DELETE      2057
#define IDM_COMP        2058
#define IDM_DUPLICATE	2059
#define IDM_COPYCHECK	2060
#define IDM_FLOPPY_TO_FILE	2061
#define IDM_FILE_TO_FLOPPY	2062
#define IDM_EXIT      	2065


/* Dialog IDs*/
#include "copydlgs.h"
#include "copy.h"

/* Local messages*/
#define UM_ERROR   WM_USER + 0
#define UM_STATUS  WM_USER + 1
#define UM_DONE    WM_USER + 2
#define UM_COMPOK  WM_USER + 3
#define UM_COMPERR WM_USER + 4
#define UM_DESTDISK	WM_USER + 5

typedef struct _ThreadContext {
  TID    ThID;         /* Thread ID for the subprocess*/
  SEL    selStk;       /* stack selector for subprocess*/
  USHORT ImageNumber;  /* Image number this thread is using*/
  USHORT CompNumber;   /* Image number compare is using*/
  USHORT ErrorCode;    /* Fatal Error generated on thread*/
} ThreadContext;
#endif
