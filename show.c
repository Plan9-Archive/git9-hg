#include <u.h>
#include <libc.h>
#include "git.h"

void
main(int argc, char **argv)
{
	Object *o;
	Hash h;
	int i;

	gitinit();
	ARGBEGIN{
	}ARGEND;

	for(i = 0; i < argc; i++){
		hparse(&h, argv[i]);
		o = readobject(h);
		print("%O\n", o);
		freeobject(o);
	}
}
