</$objtype/mkfile

BIN=/$objtype/bin/git
TARG=\
	fs\
	show\
	fetch\
	send\
	commit

OFILES=\
	pack.$O\
	util.$O\

HFILES=git.h

</sys/src/cmd/mkmany

install:
	for 