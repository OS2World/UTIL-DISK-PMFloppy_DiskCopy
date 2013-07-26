DO_DEBUG = YES
.IF $(DO_DEBUG)
	DEBUG = -Ti+
	LINK_DEBUG = -DEBUG
.ELSE
	DEBUG = -G4 -O+
.END
	CFLAGS = -Q+ -Sp1 -Gm+ -Gd- $(DEBUG) -DRC_INVOKED
	CC = icc
	LINK = ilink
	LINK_OPTS = $(LINK_DEBUG) /PM:PM /NOI #/NOD

OBJECTS = pmfloppy.obj dskim.obj copydlgs.obj

pmfloppy.exe:	$(OBJECTS) pmfloppy.res
	ilink $(LINK_OPTS) $(OBJECTS) -OUT:diskcopy.exe diskcopy.def -NOLOGO
	rc pmfloppy.res diskcopy.exe

pmfloppy.res:	pmfloppy.rc copydlgs.dlg copydlgs.h
	rc -r pmfloppy.rc

$(OBJECTS) pmfloppy.res:	pmfloppy.h dskim.h copydlgs.h copy.h copyGen.dlg
$(OBJECTS):		funcdefs.h

copy.h: copy.hpp
	sed -f resEdit.sed <copy.hpp >copy.h

copyGen.dlg:	copy.dlg
	sed -f resEdit.sed <copy.dlg >copyGen.dlg
