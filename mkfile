</$objtype/mkfile

BIN=/$objtype/bin/git
RCBIN=/rc/bin/git
TARG=\
	fs\
	show\
	fetch\
	send\
	commit

RC=\
	clone

OFILES=\
	pack.$O\
	util.$O\

HFILES=git.h

</sys/src/cmd/mkmany

%.installrc:V: /rc/bin/git/%

$RCBIN/%: %
	cp $stem $RCBIN/$stem

install.bin:V: $BIN $RCBIN
	for (i in $TARG)
		mk $MKFLAGS $i.install

install.rc:V: $BIN $RCBIN
	for (i in $RC)
		mk $MKFLAGS $i.installrc

install:V: install.bin install.rc

$BIN:
	mkdir -p $BIN
$RCBIN:
	mkdir -p $RCBIN
