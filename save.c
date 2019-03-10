#include <u.h>
#include <libc.h>
#include "git.h"

typedef struct Objbuf Objbuf;
struct Objbuf {
	int off;
	char *hdr;
	int nhdr;
	char *dat;
	int ndat;
};
enum {
	Maxparents = 16,
};

static int
bwrite(void *p, void *buf, int nbuf)
{
	return Bwrite(p, buf, nbuf);
}

static int
objbytes(void *p, void *buf, int nbuf)
{
	Objbuf *b;
	int r, n, o;
	char *s;

	b = p;
	n = 0;
	if(b->off < b->nhdr){
		r = b->nhdr - b->off;
		memcpy(buf, b->hdr, (nbuf < r) ? nbuf : r);
		b->off += r;
		nbuf -= r;
		n += r;
	}
	if(b->off < b->ndat + b->nhdr){
		s = buf;
		o = b->off - b->nhdr;
		r = b->ndat - o;
		memcpy(s + n, b->dat + o, (nbuf < r) ? nbuf : r);
		b->off += r;
		n += r;
	}
	return n;
}

void
writeobj(Hash *h, char *hdr, int nhdr, char *dat, int ndat)
{
	Objbuf b = {.off=0, .hdr=hdr, .nhdr=nhdr, .dat=dat, .ndat=ndat};
	char s[64], o[256];
	SHA1state *st;
	Biobuf *f;

	st = sha1((uchar*)hdr, nhdr, nil, nil);
	st = sha1((uchar*)dat, ndat, nil, st);
	sha1(nil, 0, h->h, st);
	if(snprint(s, sizeof(s), "%H", *h) >= sizeof(s))
		sysfatal("bad hash");
	snprint(o, sizeof(o), ".git/objects/%c%c", s[0], s[1]);
	create(o, OREAD, DMDIR | 0755);
	snprint(o, sizeof(o), ".git/objects/%c%c/%s", s[0], s[1], s + 2);
	if(access(o, AREAD) == -1){
		if((f = Bopen(o, OWRITE)) == nil)
			sysfatal("could not open %s: %r", o);
		if(deflatezlib(f, bwrite, &b, objbytes, 9, 0) == -1)
			sysfatal("could not write %s: %r", o);
		Bterm(f);
	}
}

int
gitmode(int m)
{
	return m & 0x777;
}

void
blobify(char *path, vlong size, Hash *bh)
{
	char h[64], *d;
	int f, nh;

	nh = snprint(h, sizeof(h), "%T %lld", GBlob, size) + 1;
	if((f = open(path, OREAD)) == -1)
		sysfatal("could not open %s: %r", path);
	d = emalloc(size);
	if(readall(f, d, size) != size)
		sysfatal("could not read blob %s: %r", path);
	writeobj(bh, h, nh, d, size);
	free(d);
}

int
treeify(char *path, Hash *th)
{
	char *t, h[64], l[256], ep[256];
	int nd, nl, nt, nh, i, s;
	Hash eh;
	Dir *d;

	if((nd = slurpdir(path, &d)) == -1)
		sysfatal("could not read %s", path);
	if(nd == 0)
		return 0;

	t = nil;
	nt = 0;
	for(i = 0; i < nd; i++){
		if((snprint(ep, sizeof(ep), "%s/%s", path, d[i].name)) >= sizeof(ep))
			sysfatal("overlong path");
		if(d[i].qid.type & QTDIR){
			if(treeify(ep, &eh) == 0)
				continue;
		}else
			blobify(ep, d[i].length, &eh);

		if((nl = snprint(l, sizeof(l), "%o %s", gitmode(d[i].mode), d[i].name)) >= sizeof(l))
			sysfatal("overlong name %s", ep);
		s = nt + nl + sizeof(eh.h) + 1;
		t = realloc(t, s);
		memcpy(t + nt, l, nl + 1);
		memcpy(t + nt + nl + 1, eh.h, sizeof(eh.h));
		nt = s;
	}
	free(d);
	nh = snprint(h, sizeof(h), "%T %d", GTree, nt) + 1;
	if(nh >= sizeof(h))
		sysfatal("overlong header");
	writeobj(th, h, nh, t, nt);
	return nd;
}


void
mkcommit(Hash *c, char *msg, char *name, char *email, Hash *parents, int nparents, Hash tree)
{
	char *s, h[64];
	int ns, nh, i;
	Fmt f;

	fmtstrinit(&f);
	fmtprint(&f, "tree %H\n", tree);
	for(i = 0; i < nparents; i++)
		fmtprint(&f, "parent %H\n", parents[i]);
	fmtprint(&f, "author %s <%s> %lld 0000\n", name, email, (vlong)time(nil));
	fmtprint(&f, "\n");
	fmtprint(&f, "%s", msg);
	s = fmtstrflush(&f);

	ns = strlen(s);
	nh = snprint(h, sizeof(h), "%T %d", GCommit, ns) + 1;
	writeobj(c, h, nh, s, ns);
	free(s);
}

void
usage(void)
{
	fprint(2, "usage: git/commit -n name -e email -m message -d dir");
	exits("usage");
}

int
resolveref(Hash *h, char *ref)
{
	char buf[256];
	char s[64];
	int r, f;

	if((r = hparse(h, ref)) != -1)
		return r;

	snprint(buf, sizeof(buf), ".git/%s", ref);
	if((f = open(buf, OREAD)) == -1){
		snprint(buf, sizeof(buf), ".git/refs/%s", ref);
		if((f = open(buf, OREAD)) == -1)
			return -1;
	}
	if(readall(f, s, sizeof(s)) >= 40)
		r = hparse(h, s);
	close(f);

	if(r == -1 && strstr(buf, "ref: ") == buf)
		return resolveref(h, buf + strlen("ref: "));
	return r;
}

void
main(int argc, char **argv)
{
	Hash c, t, parents[Maxparents];
	char *msg, *name, *email, *dir;
	int r, nparents;


	msg = nil;
	name = nil;
	email = nil;
	dir = nil;
	nparents = 0;
	gitinit();
	ARGBEGIN{
	case 'm':	msg = EARGF(usage());	break;
	case 'n':	name = EARGF(usage());	break;
	case 'e':	email = EARGF(usage());	break;
	case 'd':	dir = EARGF(usage());	break;
	case 'p':
		if(nparents >= Maxparents)
			sysfatal("too many parents");
		if(resolveref(&parents[nparents++], EARGF(usage())) == -1)
			sysfatal("invalid parentL: %r");
		break;
	}ARGEND;

	if(!msg) sysfatal("missing message");
	if(!name) sysfatal("missing name");
	if(!email) sysfatal("missing email");
	if(!dir) sysfatal("missing dir");
	if(nparents == 0) sysfatal("need at least one parent");

	if(!msg || !name)
		usage();

	gitinit();
	if(access(".git", AEXIST) != 0)
		sysfatal("could not find git repo: %r\n");
	r = treeify(dir, &t);
	if(r == -1)
		sysfatal("could not commit: %r\n");
	if(r == 0)
		sysfatal("empty commit: aborting");
	mkcommit(&c, msg, name, email, parents, nparents, t);
	print("%H\n", c);
}
