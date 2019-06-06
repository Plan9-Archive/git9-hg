#include <u.h>
#include <libc.h>
#include "git.h"

int
resolveref(Hash *h, char *ref)
{
	char buf[256];
	char s[64];
	int r, f;

	if((r = hparse(h, ref)) != -1)
		return r;
	snprint(buf, sizeof(buf), ".git/%s", ref);
	if((f = open(buf, OREAD)) == -1)
		return -1;
	if(readn(f, s, sizeof(s)) >= 40)
		r = hparse(h, s);
	close(f);

	if(r == -1 && strstr(buf, "ref: ") == buf)
		return resolveref(h, buf + strlen("ref: "));
	return r;
}
