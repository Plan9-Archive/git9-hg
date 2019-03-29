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
	print("hdr: off: %d, nhdr: %d\n", b->off, b->nhdr);
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
	print("return: %d\n", n);
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
	print("commit has object %s\n", o);
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
	print("writing blob object");
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
		print("blobifying %s (dir: %d)\n", d[i].name, d[i].qid.type & QTDIR);
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
mkcommit(Hash *c, char *msg, char *author, Hash *parents, int nparents, Hash tree)
{
	char *s, h[64];
	int ns, nh;

	s = smprint(
		"tree %H\n"
		"parent %H\n"
		"author %s <%s>\n"
		"\n"
		"%s",
		tree, parents[0], name, email, msg);
	USED(nparents);
	ns = strlen(s);
	nh = snprint(h, sizeof(h), "%T %d", GCommit, ns) + 1;
	writeobj(c, h, nh, s, ns);
}

void
usage(void)
{
	print("usage: git/commit -a author -m message dir");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Hash c, t;
	char *msg, *name, *email;
	int r;


	msg = nil;
	author = nil;
	ARGBEGIN{
	case 'm':	msg = EARGF(usage());	break;
	case 'n':	name = EARGF(usage());	break;
	case 'e':	email = EARGF(usage());	break;
	}ARGEND;
	if(!msg || !author)
		usage();

	gitinit();
	if(access(".git", AEXIST) != 0)
		sysfatal("could not find git repo: %r\n");
	r = treeify("/tmp/newcommit", &t);
	if(r == -1)
		sysfatal("could not commit: %r\n");
	if(r == 0)
		sysfatal("empty commit: aborting\n");
	mkcommit(&c, msg, name, email, &Zhash, 1, t);
	print("committed %H\n", c);
}
