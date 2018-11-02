#include <u.h>
#include <libc.h>
#include "git.h"

static int readpacked(Biobuf *, Object *);



static int
preadbe32(Biobuf *b, int *v, vlong off)
{
	char buf[4];
	
	if(Bseek(b, off, 0) == -1)
		return -1;
	if(Bread(b, buf, sizeof(buf)) == -1)
		return -1;
	*v = GETBE32(buf);

	return 0;
}
static int
preadbe64(Biobuf *b, vlong *v, vlong off)
{
	char buf[4];
	
	if(Bseek(b, off, 0) == -1)
		return -1;
	if(Bread(b, buf, sizeof(buf)) == -1)
		return -1;
	*v = GETBE32(buf);
	return 0;
}

int
readvint(char *p, char **pp)
{
	int i, n, c;
	
	i = 0;
	n = 0;
	do {
		c = *p++;
		n |= (c & 0x7f) << i;
		i += 7;
	} while (c & 0x80);
	*pp = p;

	return n;
}

static int
hashsearch(Hash *hlist, int nent, Hash h)
{
	int hi, lo, mid, d;

	lo = 0;
	hi = nent;
	while(lo < hi){
		mid = (lo + hi)/2;
		d = memcmp(hlist[mid].h, h.h, sizeof h.h);
		if(d < 0)
			lo = mid + 1;
		else if(d > 0)
			hi = mid;
		else
			return mid;
	}
	return -1;
}

int
hasheq(Hash *a, Hash *b)
{
	return memcmp(a->h, b->h, sizeof(a->h)) == 0;
}

static int
applydelta(Object *dst, Object *base, char *d, int nd)
{
	char *r, *b, *ed, *er;
	int n, nr, c;
	vlong o, l;

	ed = d + nd;
	b = base->data;
	n = readvint(d, &d);
	if(n != base->size){
		werrstr("mismatched source size");
		return -1;
	}

	nr = readvint(d, &d);
	r = malloc(nr + 64);
	n = snprint(r, 64, "%T %d", base->type, nr) + 1;
	dst->all = r;
	dst->type = base->type;
	dst->data = r + n;
	dst->size = nr;
	er = r + nr;
	r += n;

	while(1){
		if(d == ed)
			break;
		c = *d++;
		if(!c){
			werrstr("bad delta encoding");
			return -1;
		}
		/* copy from base */
		if(c & 0x80){
			o = 0;
			l = 0;
			/* Offset in base */
			if(c & 0x01) o |= (*d++ <<  0) & 0x000000ff;
			if(c & 0x02) o |= (*d++ <<  8) & 0x0000ff00;
			if(c & 0x04) o |= (*d++ << 16) & 0x00ff0000;
			if(c & 0x08) o |= (*d++ << 24) & 0xff000000;

			/* Length to copy */
			if(c & 0x10) l |= (*d++ <<  0) & 0x0000ff;
			if(c & 0x20) l |= (*d++ <<  8) & 0x00ff00;
			if(c & 0x40) l |= (*d++ << 16) & 0xff0000;
			if(l == 0) l = 0x10000;

			assert(o + l <= base->size);
			memmove(r, b + o, l);
			r += l;
		/* inline data */
		}else{
			memmove(r, d, c);
			d += c;
			r += c;
		}

	}
	if(r != er){
		werrstr("truncated delta");
		return -1;
	}

	return nr;
}

static int
readrdelta(Biobuf *f, Object *o, vlong n)
{
	Object *b;
	Hash h;
	char *d;

	d = nil;
	if(Bread(f, h.h, sizeof(h.h)) != sizeof(h.h))
		goto error;
	if(hasheq(&o->hash, &h))
		goto error;
	if(decompress(&d, f, nil) == -1)
		goto error;
	if((b = readobject(h)) == nil)
		goto error;
	if(applydelta(o, b, d, n) == -1)
		goto error;
	freeobject(b);
	return 0;
error:
	print("could not read rdelta: %r\n");
	free(d);
	return -1;
}

static int
readodelta(Biobuf *f, Object *o, vlong n, vlong p)
{
	Object b;
	char *d;
	vlong r;
	int c;

	r = 0;
	d = nil;
	while(1){
		if((c = Bgetc(f)) == -1)
			goto error;
		r |= c & 0x7f;
		if (!(c & 0x80))
			break;
		r++;
		r <<= 7;
	}while(c & 0x80);
	print("p: %zd, r: %zd\n", p, r);
	if(p - r < 0)
		goto error;
	if(decompress(&d, f, nil) == -1)
		goto error;
	if(Bseek(f, p - r, 0) == -1)
		goto error;
	if(readpacked(f, &b) == -1)
		goto error;
	if(applydelta(o, &b, d, n) == -1)
		goto error;
	free(d);
	return 0;
error:
	free(d);
	return -1;
}

static int
readpacked(Biobuf *f, Object *o)
{
	int c, s, n;
	vlong l, p;
	Type t;
	Buf b;

	p = Boffset(f);
	c = Bgetc(f);
	if(c == -1)
		return -1;
	l = c & 0xf;
	s = 4;
	t = (c >> 4) & 0x7;
	if(!t)
		print("unknown type for byte %x\n", c);
	/* For now, disallow packed objects larger than 1g */
	while(c & 0x80){
		if((c = Bgetc(f)) == -1)
			return -1;
		l |= (c & 0x7f) << s;
		s += 7;
	}

	switch(t){
	case GNone:
		werrstr("invalid object at %lld", Boffset(f));
		return -1;
	case GCommit:
	case GTree:
	case GTag:
	case GBlob:
		b.sz = 64 + l;
		b.data = malloc(b.sz);
		if(!b.data)
			return -1;
		n = snprint(b.data, 64, "%T %lld", t, l) + 1;
		b.len = n;
		if(bdecompress(&b, f, nil) == -1)
			return -1;
		o->type = t;
		o->all = b.data;
		o->data = b.data + n;
		o->size = b.len - n;
		break;
	case GOdelta:
		print("offset delta\n");
		if(readodelta(f, o, s, p) == -1)
			return -1;
		break;
	case GRdelta:
		if(readrdelta(f, o, s) == -1)
			return -1;
		break;	
	}
	return 0;
}

static int
readloose(Biobuf *f, Object *o)
{
	struct { char *tag; int type; } *p, types[] = {
		{"blob", GBlob},
		{"tree", GTree},
		{"commit", GCommit},
		{"tag", GTag},
		{nil},
	};
	char *d, *s, *e;
	vlong sz, n;
	int l;

	n = decompress(&d, f, nil);
	if(n == -1)
		return -1;

	s = d;
	o->type = GNone;
	for(p = types; p->tag; p++){
		l = strlen(p->tag);
		if(strncmp(s, p->tag, l) == 0){
			s += l;
			o->type = p->type;
			while(!isspace(*s))
				s++;
			break;
		}
	}
	if(o->type == GNone){
		free(o->data);
		return -1;
	}
	sz = strtol(s, &e, 0);
	if(e == s || *e++ != 0){
		print("malformed object header");
		goto error;
	}
	if(sz != n - (e - d)){
		print("mismatched sizes");
		goto error;
	}
	o->size = sz;
	o->data = e;
	o->all = d;
	return 0;

error:
	free(d);
	return -1;
}

static vlong
searchindex(Biobuf *f, Hash h)
{
	int lo, hi, idx, i, nent;
	vlong o, oo;
	Hash hh;

	o = 8;
	/*
	 * Read the fanout table. The fanout table
	 * contains 256 entries, corresponsding to
	 * the first byte of the hash. Each entry
	 * is a 4 byte big endian integer, containing
	 * the total number of entries with a leading
	 * byte <= the table index, allowing us to
	 * rapidly do a binary search on them.
	 */
	if (h.h[0] == 0){
		lo = 0;
		if(preadbe32(f, &hi, o) == -1)
			goto err;
	} else {
		o += h.h[0]*4 - 4;
		if(preadbe32(f, &lo, o + 0) == -1)
			goto err;
		if(preadbe32(f, &hi, o + 4) == -1)
			goto err;
	}
	if(hi == lo)
		goto notfound;
	if(preadbe32(f, &nent, 8 + 255*4) == -1)
		goto err;

	/*
	 * Now that we know the range of hashes that the
	 * entry may exist in, read them in so we can do
	 * a bsearch.
	 */
	idx = -1;
	Bseek(f, Hashsz*lo + 8 + 256*4, 0);
	for(i = 0; i < hi - lo; i++){
		if(Bread(f, hh.h, sizeof(hh.h)) == -1)
			goto err;
		if(hasheq(&hh, &h))
			idx = lo + i;
	}
	if(idx == -1)
		goto notfound;


	/*
	 * We found the entry. If it's 32 bits, then we
	 * can just return the oset, otherwise the 32
	 * bit entry contains the oset to the 64 bit
	 * entry.
	 */
	oo = 8;			/* Header */
	oo += 256*4;		/* Fanout table */
	oo += Hashsz*nent;	/* Hashes */
	oo += 4*nent;		/* Checksums */
	oo += 4*idx;		/* Offset offset */
	if(preadbe32(f, &i, oo) == -1)
		goto err;
	o = i & 0xffffffff;
	if(o & (1ull << 31)){
		o &= 0x7fffffff;
		if(preadbe64(f, &o, o) == -1)
			goto err;
	}
	print("found offset %lld\n", o);
	return o;

err:
	fprint(2, "unable to read packfile: %r\n");
	return -1;
notfound:
	werrstr("not present: %H", h);
	return -1;		
}

static int
scanword(char **str, int *nstr, char *buf, int nbuf)
{
	char *p;
	int n, r;

	r = -1;
	p = *str;
	n = *nstr;
	while(n && isblank(*p)){
		n--;
		p++;
	}

	for(; n && !isspace(*p); p++, n--){
		r = 0;
		if(nbuf > 1){
			*buf++ = *p;
			nbuf--;
		}
	}
	*buf = 0;
	*str = p;
	*nstr = n;
	return r;
}

static int
nextline(char **str, int *nstr)
{
	while(*nstr && isblank(**str)){
		(*nstr)--;
		(*str)++;
	}
	if(*nstr && **str == '\n'){
		(*nstr)--;
		(*str)++;
		return 1;
	}
	return 0;
}

static int
parseauthor(char **str, int *nstr, char **name, vlong *time)
{
	char buf[128];
	Resub m[4];
	int nm, ne;
	char *e;

	memset(m, 0, sizeof(m));
	for(e = *str; *e != '\n'; e++){
		ne = e - *str;
		if(ne == *nstr || ne == sizeof(buf))
			break;
		buf[ne] = *e;
	}
	if(!regexec(authorpat, buf, m, nelem(m)))
		sysfatal("invalid author line\n");
	nm = m[1].ep - m[1].sp;
	*name = emalloc(nm + 1);
	memcpy(*name, m[1].sp, nm);
	
	nm = m[2].ep - m[2].sp;
	memcpy(buf, m[2].sp, nm);
	buf[nm] = 0;
	*time = atoll(buf);

	nm = m[0].ep - m[0].sp;
	*str += nm;
	*nstr -= nm;
	return 0;
}
static void
parsecommit(Object *o)
{
	char *p, buf[128];
	int np;

	p = o->data;
	np = o->size;
	while(1){
		if(scanword(&p, &np, buf, sizeof(buf)) == -1)
			break;
		if(strcmp(buf, "tree") == 0){
			if(scanword(&p, &np, buf, sizeof(buf)) == -1)
				sysfatal("invalid commit: tree missing");
			if(hparse(&o->tree, buf) == -1)
				sysfatal("invalid commit: garbled tree");
		}else if(strcmp(buf, "parent") == 0){
			if(scanword(&p, &np, buf, sizeof(buf)) == -1)
				sysfatal("invalid commit: missing parent");
			o->parent = realloc(o->parent, ++o->nparent * sizeof(Hash));
			if(!o->parent)
				sysfatal("unable to malloc: %r");
			if(hparse(&o->parent[o->nparent], buf) == -1)
				sysfatal("invalid commit: garbled parent");
		}else if(strcmp(buf, "author") == 0){
			parseauthor(&p, &np, &o->author, &o->mtime);
		}else if(strcmp(buf, "committer") == 0){
			parseauthor(&p, &np, &o->author, &o->mtime);
		}else{
			print("unknown commit header '%s' (len: %ld)\n", buf, strlen(buf));
		}
		nextline(&p, &np);
	}
	while (np && isspace(*p)) {
		p++;
		np--;
	}
	o->msg = p;
	o->nmsg = np;
}

static void
parsetree(Object *o)
{
	char *p, buf[128];
	int np, nn, m;
	Dirent *t;

	p = o->data;
	np = o->size;
	while(np > 0){
		if(scanword(&p, &np, buf, sizeof(buf)) == -1)
			break;
		while(np && isblank(*p)){
			p++;
			np--;
		}
		o->ent = realloc(o->ent, ++o->nent * sizeof(Dirent));
		t = &o->ent[o->nent - 1];
		m = strtol(buf, nil, 8);
		t->mode = m & 0777 | ((m & ~0777) ? DMDIR : 0);
		t->name = p;
		nn = strlen(p) + 1;
		p += nn;
		np -= nn;
		if(np < sizeof(t->h.h))
			sysfatal("malformed tree %H, remaining %d (%s)", o->hash, np, p);
		memcpy(t->h.h, p, sizeof(t->h.h));
		p += sizeof(t->h.h);
		np -= sizeof(t->h.h);
	}
}

static void
parsetag(Object *)
{
}

void
parseobject(Object *o)
{
	switch(o->type){
	case GTree:	parsetree(o);	break;
	case GCommit:	parsecommit(o);	break;
	case GTag:	parsetag(o);	break;
	default:	break;
	}
}

Object*
readobject(Hash h)
{
	char path[Pathmax];
	char pack[Pathmax];
	char hbuf[41];
	Biobuf *f;
	Object *obj, *ret;
	int l, i, n;
	vlong o;
	Dir *d;

	d = nil;
	ret = nil;
	obj = emalloc(sizeof(Object));
	obj->hash = h;
	snprint(hbuf, sizeof(hbuf), "%H", h);
	snprint(path, sizeof(path), ".git/objects/%c%c/%s", hbuf[0], hbuf[1], hbuf + 2);
	if((f = Bopen(path, OREAD)) != nil){
		if(readloose(f, obj) == -1)
			goto error;
		Bterm(f);
		parseobject(obj);
		return obj;
	}

	if ((n = slurpdir(".git/objects/pack", &d)) == -1)
		goto error;
	o = -1;
	l = 0;
	for(i = 0; i < n; i++){
		l = strlen(d[i].name);
		if(l > 4 && strcmp(d[i].name + l - 4, ".idx") != 0)
			continue;
		if(snprint(path, sizeof(path), ".git/objects/pack/%s", d[i].name) >= sizeof(path))
			goto error;
		if((f = Bopen(path, OREAD)) == nil)
			continue;
		if((o = searchindex(f, h)) == -1){
			Bterm(f);
			continue;
		}
		Bterm(f);
		break;
	}

	if (obj == nil){
		f = nil;
		goto error;
	}

	if(snprint(pack, sizeof(pack), ".git/objects/pack/%.*s.pack", l - 4, d[i].name) >= sizeof(path))
		goto error;
	if((f = Bopen(pack, OREAD)) == nil)
		goto error;
	if(Bseek(f, o, 0) == -1)
		goto error;
	if(readpacked(f, obj) == -1)
		goto error;
	parseobject(obj);
error:
	if(f != nil)
		Bterm(f);
	free(d);
	free(obj);
	return ret;
}

void
freeobject(Object *o)
{
	if(!o)
		return;
	free(o->all);
	free(o);
}
