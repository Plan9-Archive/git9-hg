#include <u.h>
#include <libc.h>
#include <pool.h>

#include "git.h"

typedef struct Objq	Objq;
typedef struct Buf	Buf;
typedef struct Compout	Compout;
typedef struct Update	Update;

struct Buf {
	int off;
	int sz;
	uchar *data;
};

struct Compout {
	int fd;
	DigestState *st;
};


enum {
	Nproto	= 16,
	Nport	= 16,
	Nhost	= 256,
	Npath	= 128,
	Nrepo	= 64,
	Nbranch	= 32,
};

struct Objq {
	Objq *next;
	Object *obj;
};

struct Update {
	char	ref[128];
	Hash	theirs;
	Hash	ours;
};

int chatty;
int sendall;
char *curbranch = "refs/heads/master";
char *removed[128];
int nremoved;

static int
readpkt(int fd, char *buf, int nbuf)
{
	char len[5];
	char *e;
	int n;

	if(readn(fd, len, 4) == -1)
		return -1;
	len[4] = 0;
	n = strtol(len, &e, 16);
	if(n == 0)
		return 0;
	if(e != len + 4 || n <= 4)
		sysfatal("invalid packet line length");
	n  -= 4;
	if(n >= nbuf)
		sysfatal("buffer too small");
	if(readn(fd, buf, n) != n)
		return -1;
	buf[n] = 0;
	return n;
}

static int
hwrite(int fd, void *buf, int nbuf, DigestState **st)
{
	if(write(fd, buf, nbuf) != nbuf)
		return -1;
	*st = sha1(buf, nbuf, nil, *st);
	return nbuf;
}

int
writepkt(int fd, char *buf, int nbuf)
{
	char len[5];

	snprint(len, sizeof(len), "%04x", nbuf + 4);
	if(write(fd, len, 4) != 4)
		return -1;
	if(write(fd, buf, nbuf) != nbuf)
		return -1;
	return 0;
}

int
flushpkt(int fd)
{
	return write(fd, "0000", 4);
}

static void
grab(char *dst, int n, char *p, char *e)
{
	int l;

	l = e - p;
	if(l >= n)
		sysfatal("overlong component");
	memcpy(dst, p, l);
	dst[l + 1] = 0;

}

static int
parseuri(char *uri, char *proto, char *host, char *port, char *path, char *repo)
{
	char *s, *p, *q;
	int n;

	p = strstr(uri, "://");
	if(!p){
		werrstr("missing protocol");
		return -1;
	}
	grab(proto, Nproto, uri, p);
	s = p + 3;

	p = strstr(s, "/");
	if(!p || strlen(p) == 1){
		werrstr("missing path");
		return -1;
	}
	q = memchr(s, ':', p - s);
	if(q){
		grab(host, Nhost, s, q);
		grab(port, Nport, q + 1, p);
	}else{
		grab(host, Nhost, s, p);
		snprint(port, Nport, "9418");
	}
	
	snprint(path, Npath, "%s", p);
	p = strrchr(p, '/') + 1;
	if(!p || strlen(p) == 0){
		werrstr("missing repository in uri");
		return -1;
	}
	n = strlen(p);
	if(hassuffix(p, ".git"))
		n -= 4;
	grab(repo, Nrepo, p, p + n);
	return 0;
}

int
dialssh(char *host, char *, char *path)
{
	int pid, pfd[2];

	print("dialing via ssh %s...\n", host);
	if(pipe(pfd) == -1)
		sysfatal("unable to open pipe: %r");
	pid = fork();
	if(pid == -1)
		sysfatal("unable to fork");
	if(pid == 0){
		close(pfd[1]);
		dup(pfd[0], 0);
		dup(pfd[0], 1);
		execl("/bin/ssh", "ssh", host, "git-receive-pack", path, nil);
	}else{
		close(pfd[0]);
		return pfd[1];
	}
	return -1;
}

int
dialgit(char *host, char *port, char *path)
{
	char *ds, cmd[128];
	int fd, l;

	ds = netmkaddr(host, "tcp", port);
	print("dialing %s...\n", ds);
	fd = dial(ds, nil, nil, nil);
	if(fd == -1)
		return -1;
	l = snprint(cmd, sizeof(cmd), "git-receive-pack %s", path);
	if(writepkt(fd, cmd, l + 1) == -1){
		print("failed to write message\n");
		close(fd);
		return -1;
	}
	return fd;
}

void
pack(Objset *send, Objset *skip, Object *o)
{
	Dirent *e;
	Object *s;
	int i;

	if(oshas(send, o) || oshas(skip, o))
		return;
	osadd(send, o);
	switch(o->type){
	case GCommit:
		if((s = readobject(o->commit->tree)) == nil)
			sysfatal("could not read tree %H: %r", o->hash);
		pack(send, skip, s);
		break;
	case GTree:
		for(i = 0; i < o->tree->nent; i++){
			e = &o->tree->ent[i];
			if ((s = readobject(e->h)) == nil)
				sysfatal("could not read entry %H: %r", e->h);
			pack(send, skip, s);
		}
		break;
	default:
		break;
	}
}

int
compread(void *p, void *dst, int n)
{
	Buf *b;

	b = p;
	if(n > b->sz - b->off)
		n = b->sz - b->off;
	memcpy(dst, b->data + b->off, n);
	b->off += n;
	return n;
}

int
compwrite(void *p, void *buf, int n)
{
	Compout *o;

	o = p;
	o->st = sha1(buf, n, nil, o->st);
	return write(o->fd, buf, n);
}

int
compress(int fd, void *buf, int sz, DigestState **st)
{
	int r;
	Buf b ={
		.off=0,
		.data=buf,
		.sz=sz,
	};
	Compout o = {
		.fd = fd,
		.st = *st,
	};

	r = deflatezlib(&o, compwrite, &b, compread, 6, 0);
	*st = o.st;
	return r;
}

int
writeobject(int fd, Object *o, DigestState **st)
{
	char hdr[8];
	uvlong sz;
	int i;

	i = 1;
	sz = o->size;
	hdr[0] = o->type << 4;
	hdr[0] |= sz & 0xf;
	if(sz >= (1 << 4)){
		hdr[0] |= 0x80;
		sz >>= 4;
	
		for(i = 1; i < sizeof(hdr); i++){
			hdr[i] = sz & 0x7f;
			if(sz <= 0x7f){
				i++;
				break;
			}
			hdr[i] |= 0x80;
			sz >>= 7;
		}
	}

	if(hwrite(fd, hdr, i, st) != i)
		return -1;
	if(compress(fd, o->data, o->size, st) == -1)
		return -1;
	return 0;
}

int
writepack(int fd, Update *upd, int nupd)
{
	Objset send, skip;
	Object *o, *p;
	Objq *q, *n, *e;
	DigestState *st;
	Update *u;
	char buf[4];
	Hash h;
	int i;

	osinit(&send);
	osinit(&skip);
	for(i = 0; i < nupd; i++){
		u = &upd[i];
		if(hasheq(&u->theirs, &Zhash))
			continue;
		if((o = readobject(u->theirs)) == nil)
			sysfatal("could not read %H", u->theirs);
		pack(&skip, &skip, o);
	}

	q = nil;
	e = nil;
	for(i = 0; i < nupd; i++){
		u = &upd[i];
		if((o = readobject(u->ours)) == nil)
			sysfatal("could not read object %H", u->ours);

		n = emalloc(sizeof(Objq));
		n->obj = o;
		if(!q){
			q = n;
			e = n;
		}else{
			e->next = n;
		}
	}

	for(n = q; n; n = n->next)
		e = n;
	for(; q; q = n){
		o = q->obj;
		if(oshas(&skip, o) || oshas(&send, o))
			goto iter;
		pack(&send, &skip, o);
		for(i = 0; i < o->commit->nparent; i++){
			if((p = readobject(o->commit->parent[i])) == nil)
				sysfatal("could not read parent of %H", o->hash);
			e->next = emalloc(sizeof(Objq));
			e->next->obj = p;
			e = e->next;
		}
iter:
		n = q->next;
		free(q);
	}

	st = nil;
	PUTBE32(buf, send.nobj);
	if(hwrite(fd, "PACK\0\0\0\02", 8, &st) != 8)
		return -1;
	if(hwrite(fd, buf, 4, &st) == -1)
		return -1;
	for(i = 0; i < send.sz; i++){
		if(!send.obj[i])
			continue;
		if(writeobject(fd, send.obj[i], &st) == -1)
			return -1;
	}
	sha1(nil, 0, h.h, st);
	if(write(fd, h.h, sizeof(h.h)) == -1)
		return -1;
	return 0;
}

Update*
findref(Update *u, int nu, char *ref)
{
	int i;

	for(i = 0; i < nu; i++)
		if(strcmp(u[i].ref, ref) == 0)
			return &u[i];
	return nil;
}

int
readours(Update **ret)
{
	Update *u, *r;
	Hash *h;
	int nd, nu, i;
	char *pfx;
	Dir *d;

	nu = 0;
	if(!sendall){
		u = emalloc((nremoved + 1)*sizeof(Update));
		snprint(u[nu].ref, sizeof(u[nu].ref), "%s", curbranch);
		if(resolveref(&u[nu].ours, curbranch) == -1)
			sysfatal("broken branch %s", curbranch);
		nu++;
	}else{
		if((nd = slurpdir(".git/refs/heads", &d)) == -1)
			sysfatal("read branches: %r");
		u = emalloc((nremoved + nd)*sizeof(Update));
		for(i = 0; i < nd; i++){
			snprint(u->ref, sizeof(u->ref), "refs/heads/%s", d[nu].name);
			if(resolveref(&u[nu].ours, u[nu].ref) == -1)
				continue;
			nu++;
		}
	}
	for(i = 0; i < nremoved; i++){
		pfx = "refs/heads/";
		if(strstr(removed[i], "heads/") == removed[i])
			pfx = "refs/";
		if(strstr(removed[i], "refs/heads/") == removed[i])
			pfx = "";
		snprint(u[nu].ref, sizeof(u[nu].ref), "%s%s", pfx, removed[i]);
		h = &u[nu].ours;
		if((r = findref(u, nu, u[nu].ref)) != nil)
			h = &r->ours;
		else
			nu++;
		memcpy(h, &Zhash, sizeof(Hash));
	}

	*ret = u;
	return nu;	
}

int
sendpack(int fd)
{
	char buf[65536];
	char *sp[3];
	Update *upd, *u;
	int i, n, nupd, updating;

	if((nupd = readours(&upd)) == -1)
		sysfatal("read refs: %r");
	while(1){
		n = readpkt(fd, buf, sizeof(buf));
		if(n == -1)
			return -1;
		if(n == 0)
			break;
		if(strncmp(buf, "ERR ", 4) == 0)
			sysfatal("%s", buf + 4);

		if(getfields(buf, sp, nelem(sp), 1, " \t\n\r") != 2)
			sysfatal("invalid ref line %.*s", utfnlen(buf, n), buf);
		if((u = findref(upd, nupd, sp[1])) == nil)
			continue;
		if(hparse(&u->theirs, sp[0]) == -1)
			sysfatal("invalid hash %s", sp[0]);
		snprint(u->ref, sizeof(u->ref), sp[1]);
	}

	updating = 0;
	for(i = 0; i < nupd; i++){
		u = &upd[i];
		if(!hasheq(&u->theirs, &Zhash) && readobject(u->theirs) == nil){
			fprint(2, "remote has diverged: pull and try again\n");
			updating = 0;
			break;
		}
		if(hasheq(&u->ours, &Zhash)){
			print("%s: deleting\n", u->ref);
			continue;
		}
		if(hasheq(&u->theirs, &u->ours)){
			print("%s: up to date\n", u->ref);
			continue;
		}
		print("%s: %H => %H\n", u->ref, u->theirs, u->ours);
		n = snprint(buf, sizeof(buf), "%H %H %s", u->theirs, u->ours, u->ref);
		if(n >= sizeof(buf))
			sysfatal("overlong update");
		if(writepkt(fd, buf, n) == -1)
			sysfatal("unable to send update pkt");
		updating = 1;
	}
	flushpkt(fd);
	if(updating)
		return writepack(fd, upd, nupd);
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: %s remote [reponame]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char proto[Nproto], host[Nhost], port[Nport];
	char repo[Nrepo], path[Npath];
	int fd;

	ARGBEGIN{
	default:	usage();	break;
	case 'a':	sendall++;	break;
	case 'd':	chatty++;	break;
	case 'r':
		if(nremoved == nelem(removed))
			sysfatal("too many deleted branches");
		removed[nremoved++] = EARGF(usage());
		break;
	case 'b':
		curbranch = smprint("refs/%s", EARGF(usage()));
		break;
	}ARGEND;

	gitinit();
	if(argc != 1)
		usage();
	fd = -1;
	if(parseuri(argv[0], proto, host, port, path, repo) == -1)
		sysfatal("bad uri %s", argv0);
	if(strcmp(proto, "ssh") == 0 || strcmp(proto, "git+ssh") == 0)
		fd = dialssh(host, port, path);
	else if(strcmp(proto, "git") == 0)
		fd = dialgit(host, port, path);
	else if(strcmp(proto, "http") == 0 || strcmp(proto, "git+http") == 0)
		sysfatal("http clone not implemented");
	else
		sysfatal("unknown protocol %s", proto);
	
	if(fd == -1)
		sysfatal("could not dial %s:%s: %r", proto, host);
	if(sendpack(fd) == -1)
		sysfatal("send failed: %r");
	exits(nil);
}
