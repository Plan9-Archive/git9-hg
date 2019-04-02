</$objtype/mkfile

BIN=/$objtype/bin/git
TARG=\
	fs\
	fetch\
	send\
	save\
	conf\

RC=\
	clone\
	commit\
	log\
	add\
	walk

OFILES=\
	pack.$O\
	util.$O\
	objset.$O

HFILES=git.h

</sys/src/cmd/mkmany

# Override install target to install rc.
install:V:
	for (i in $TARG)
		mk $MKFLAGS $i.install
	for (i in $RC)
		mk $MKFLAGS $i.rcinstall

%.rcinstall:
	cp $stem /rc/bin/git/$stem
