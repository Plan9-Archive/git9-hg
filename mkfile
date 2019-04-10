</$objtype/mkfile

BIN=/$objtype/bin/git
TARG=\
	conf\
	fetch\
	fs\
	save\
	send\

RC=\
	add\
	branch\
	checkout\
	clone\
	commit\
	init\
	log\
	walk

OFILES=\
	objset.$O\
	pack.$O\
	util.$O\

HFILES=git.h

</sys/src/cmd/mkmany

# Override install target to install rc.
install:V:
	mkdir -p /$objtype/bin/git
	for (i in $TARG)
		mk $MKFLAGS $i.install
	for (i in $RC)
		mk $MKFLAGS $i.rcinstall
	mk $MKFLAGS /sys/lib/git/template

%.c %.h: %.y
	$YACC $YFLAGS -D1 -d -s $stem $prereq
	mv $stem.tab.c $stem.c
	mv $stem.tab.h $stem.h

%.rcinstall:V:
	cp $stem $BIN/$stem

/sys/lib/git/template: template
	mkdir -p /sys/lib/git/template
	dircp template /sys/lib/git/template
