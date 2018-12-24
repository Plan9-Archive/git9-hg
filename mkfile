</$objtype/mkfile

BIN=/$objtype/bin/git
TARG=\
	fs\
	show\
	fetch\
<<<<<<< local
	commit\
=======
>>>>>>> other
	send\
<<<<<<< local
=======
	commit
>>>>>>> other

OFILES=\
	pack.$O\
	util.$O\

HFILES=git.h

</sys/src/cmd/mkmany

install:
	for 