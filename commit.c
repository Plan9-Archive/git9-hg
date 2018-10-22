#include <u.h>
#include <libc.h>
#include "git.h"

static int
slurpdir(char *p, Dir **d)
{
	int r, f;

	if((f = open(p, OREAD)) == -1)
		return -1;
	r = dirreadall(f, d);
	close(f);
	return r;
}

void
hprint(Biobuf *f, Hash *h, char *fmt, ...)
{
	va_list ap;

	memset(h->h, 0, sizeof(h->h));
	va_start(ap, fmt);
	if(Bprint(f, fmt, ap) == -1)
		sysfatal("could not write output: %r");
	va_end(ap);
}

int
treeify(char *path, Hash *h)
{
	Biobuf *f;
	Dir *d;
	char buf[512];
	int i, n, r;

	r = 0;
	if((n = slurpdir(".", &d)) == -1)
		return -1;
	if(n == 0)
		goto done;
	for(i = 0; i < n; i++){
		if(snprint(buf, sizeof(buf), "%s/%s", path, d[i].name) >= sizeof(buf)){
			werrstr("overlong path");
			goto done;
		}
		if(d[i].qid.type & QTDIR)
			r = treeify(buf);
		else
			r = mkblob(buf);
		if(r == -1)
			goto done;
	}
	if ((f = Bopen(path, OWRITE)) == nil){
		r = -1;
		goto done;
	}

	hprint(f, h, "tree %d");
	hputc(f, h, 0);
	for(i = 0; i < n; i++){
		hprint(f, h, "0%o %s", d[i].mode & 0777, dir[i].mode );
		hputc(f, h, 0);
	}
	Bterm(f);
done:
	free(d);
	return r;
}

void
main(int argc, char **argv)
{
	Hash h;
	Dir *d;
	int n;

	if((d = dirstat(".git") == nil)
		sysfatal("could not find git repo: %r\n");
	if(objify(".", &h))
		sysfatal("could not commit: %r\n")
	if(mkcommit("test", h) == -1)
		sysfatal("could not commit: %r\n");
}
