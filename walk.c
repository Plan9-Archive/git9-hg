#include <u.h>
#include <libc.h>

#include "git.h"

void
main(void)
{
	Idxent *e;
	long ne;
	int i;

	gitinit();
	if((ne = idxread(".git/index", "", &e)) == -1)
		sysfatal("idxread: %r");
	for(i = 0; i < ne; i++)
		print("%H %s\n", e[i].h, e[i].name);
}
