#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

#include "git.h"

char *mtpt = "/n/git";
#define QDIR(qid)	((qid)->path & (0xff))

char *Eperm = "permission denied";
char *Eexist = "does not exist";
char *E2long = "path too long";
char *Enodir = "not a directory";
char *Egreg = "wat";
char *username;
long nqid;

enum {
	Qroot,
	Qbranch,
	Qcommit,
	Qcommitmsg,
	Qcommitinfo,
	Qcommittree,
	Qcommitdata,
	Qobject,
	Qmax,
};

typedef struct Ols Ols;
struct Ols {
	int qtype;
	int l0;
	int n0;
	Dir *d0;
	int l1;
	int n1;
	Dir *d1;
	int lp;
	int np;
	int pfd;
	int inpack;
	int last;
	char pfx[32];
};

typedef struct Gitaux Gitaux;
struct Gitaux {
	Object	*obj;
	char 	*refpath;
	Ols	*ols;
};

char *qroot[] = {
	"branch",
	"object",
};

Object *objcache;
int nobjcache;

static char *
findent(Object *o, char *name, Qid *qid)
{
	USED(o); USED(name); USED(qid);
	return "unimplemented: walk git ent";
}

static int
treegen(int i, Dir *d, void *)
{
	USED(i); USED(d);
	return -1;	
}

static int
rootgen(int i, Dir *d, void *)
{

	if (i >= nelem(qroot))
		return -1;
	d->mode = 0555 | DMDIR;
	d->name = estrdup9p(qroot[i]);
	d->qid.path = i;
	d->uid = estrdup9p(username);
	d->gid = estrdup9p(username);
	d->muid = estrdup9p(username);
	return 0;
}

static int
branchgen(int i, Dir *d, void *p)
{
	Gitaux *aux;
	Dir *refs;
	int n;

	aux = p;
	refs = nil;
	d->mode = 0555 | DMDIR;
	d->uid = estrdup9p(username);
	d->gid = estrdup9p(username);
	d->muid = estrdup9p(username);
	if((n = slurpdir(aux->refpath, &refs)) < 0)
		return -1;
	if(i < n){
		d->name = estrdup9p(refs[i].name);
		free(refs);
		return 0;
	}else{
		free(refs);
		return -1;
	}
}

static void
gitcreate(Req *r)
{
	respond(r, "permission denied");
}

static int
gtreegen(int, Dir *, void *)
{
	return -1;
}

static int
gcommitgen(int, Dir *, void *)
{
	return -1;
}

static void
obj2dir(Dir *d, Object *o)
{
	char name[64];

	snprint(name, sizeof(name), "%H", o->hash);
	d->name = estrdup9p(name);
	d->qid.type = (o->type == GBlob) ? 0 : QTDIR;
	d->mode = (o->type == GBlob) ? 0644 : 0755 | DMDIR;
	d->qid.path = 42;
}

int
olsinit(Ols *st)
{
	st->d0 = nil;
	st->d1 = nil;
	st->l0 = 0;
	st->l1 = 0;
	st->n1 = 0;
	st->last = 0;
	st->pfd = -1;
	if((st->n0 = slurpdir(".git/objects", &st->d0)) == -1)
		return -1;
	return 0;
}

static int
openpack(char *path, int *np)
{
	int fd;
	char buf[4];

	if(fd = open(path, OREAD) == -1)
		return -1;
	if(seek(fd, 8 + 255*4, 0) == -1)
		return -1;
	if(read(fd, buf, sizeof(buf)) != sizeof(buf))
		return -1;
	*np = GETBE32(buf);
	return fd;
}

static int
nextpack(Ols *st){
	char path[128];

	close(st->pfd);
	if(st->lp == st->np){
		st->inpack = 0;
		return -1;
	}
	snprint(path, sizeof(path), ".git/objects/pack/%s", st->d1[st->l1].name);
	if((st->pfd = openpack(path, &st->np)) == -1)
		return -1;
	st->l1++;
	return 0;
}

static int
nextdir(Ols *st)
{
	char path[128];

	if(st->l0 == st->n0)
		return -1;
	if(strcmp(st->d0[st->l0].name, "pack") == 0)
		st->inpack = 1;
	else
		snprint(st->pfx, sizeof(st->pfx), "%s", st->d0[st->l0].name);
	snprint(path, sizeof(path), ".git/objects/%s", st->d0[st->l0].name);
	free(st->d1);
	st->l1 = 0;
	st->d1 = nil;
	if((st->n1 = slurpdir(path, &st->d1)) == -1)
		return -1;
	st->l0++;
	return 0;
}

static int
objgen1(Dir *d, Ols *st)
{
	char name[64];
	Object *o;
	Hash h;

	while(1){
		if(st->inpack){
			if(st->lp < st->np){
				if(read(st->pfd, h.h, sizeof(h.h)) == -1)
					return -1;
				if((o = readobject(h)) == nil)
					return -1;
				obj2dir(d, o);
				freeobject(o);
				st->lp++;
				return 0;
			}
			if(nextpack(st) == -1 && nextdir(st) == -1)
				return -1;
		}else{
			if(st->l1 < st->n1){
				snprint(name, sizeof(name), "%s%s", st->pfx, st->d1[st->l1].name);
				if(hparse(&h, name) == -1)
					return -1;
				if((o = readobject(h)) == nil)
					return -1;
				obj2dir(d, o);
				freeobject(o);
				st->l1++;
				return 0;
			}
			if(nextdir(st) == -1)
				return -1;
		}
	}
}

static int
objgen(int i, Dir *d, void *p)
{
	Ols *st;

	st = p;
	if(i < st->last){
		print("restarting st: i=%d, last=%d\n", i, st->last);
		st->last = 0;
		free(st->d0);
		free(st->d1);
		close(st->pfd);
		if(olsinit(st) == -1)
			return -1;
	}
	while  (st->last++ < i)
		if (objgen1(d, st) == -1)
			goto done;

	if(objgen1(d, st) == 0)
		return 0;
done:
	free(st->d0);
	free(st->d1);
	st->d0 = nil;
	st->d1 = nil;
	return -1;
}

static void
readobj(Req *r, Object *o)
{
	switch(o->type){
	case GBlob:
		readbuf(r, o->data, o->size);
		break;
	case GTag:
		readbuf(r, o->data, o->size);
		break;
	case GTree:
		dirread9p(r, gtreegen, o);
		break;
	case GCommit:
		dirread9p(r, gcommitgen, o);
		break;
	default:
		sysfatal("invalid object type");
	}
}

static void
readcommitinfo(Req *r, Object*)
{
	readstr(r, "commit info not yet implemented");
}


static void
gitattach(Req *r)
{
	r->ofcall.qid = (Qid){Qroot, 0, QTDIR};
	r->fid->qid = r->ofcall.qid;
	r->fid->aux = emalloc(sizeof(Gitaux));;
	respond(r, nil);
}

static char*
gitclone(Fid *, Fid *n)
{
	n->aux = emalloc(sizeof(Gitaux));
	return nil;
}

static char *
objwalk1(Qid *, Object *, char *, vlong)
{
	return "unimplemented object walk";
}

static Object *
readref(char *pathstr)
{
	char buf[128], path[128], *p, *e;
	Hash h;
	int n, f;

	snprint(path, sizeof(path), "%s", pathstr);
	print("reading path %s\n", path);
	while(1){
		if((f = open(path, OREAD)) == -1){
			print("failed to open path: %r\n");
			return nil;
		}
		if((n = read(f, buf, sizeof(buf) - 1)) == -1){
			print("failed to read\n");
			return nil;
		}
		buf[n] = 0;
		if(strncmp(buf, "ref:", 4) !=  0){
			print("%s direct hash\n", buf);
			break;
		}

		p = buf + 4;
		while(isspace(*p))
			p++;
		if((e = strchr(p, '\n')) != nil)
			*e = 0;
		snprint(path, sizeof(path), ".git/%s", p);
	}

	if(hparse(&h, buf) == -1){
		print("failed to parse hash %s\n", buf);
		return nil;
	}

	return readobject(h);
}

static char *
gitwalk1(Fid *fid, char *name, Qid *qid)
{
	char path[128];
	Gitaux *aux;
	char *e;
	Dir *d;
	Hash h;

	e = nil;
	aux = fid->aux;
	switch(QDIR(&fid->qid)){
	case Qroot:
		if(strcmp(name, "object") == 0){
			*qid = (Qid){Qobject, 0, QTDIR};
			aux->ols = emalloc(sizeof(*aux->ols));
			olsinit(aux->ols);
		}else if(strcmp(name, "branch") == 0){
			*qid = (Qid){Qbranch, 0, QTDIR};
			aux->refpath = estrdup(".git/refs/heads");
		}else{
			return Eexist;
		}
		break;
	case Qbranch:
		if(snprint(path, sizeof(path), "%s/%s", aux->refpath, name) >= sizeof(path))
			return E2long;
		if((d = dirstat(path)) == nil)
			return Eexist;
		if(d->qid.type == QTDIR)
			aux->refpath = estrdup(path);
		else if((aux->obj = readref(path)) != nil)
			qid->path = (++nqid << 8) | Qcommit;
		else
			e = Eexist;
		free(d);
		break;
	case Qobject:
		if(aux->obj){
			objwalk1(qid, aux->obj, name, Qobject);
		}else{
			if(hparse(&h, name) == -1)
				return "invalid object name";
			if((aux->obj = readobject(h)) == nil)
				return "could not read object";
			qid->path = (++nqid << 8) | Qobject;
			qid->type = (aux->obj->type == GBlob) ? 0 : QTDIR;
			qid->vers = 0;
		}
		break;
	case Qcommit:
		qid->path &= ~0xff;
		if(strcmp(name, "tree") == 0)
			qid->path |= Qcommittree;
		else if(strcmp(name, "info") == 0)
			qid->path |= Qcommitinfo;
		else if(strcmp(name, "msg") == 0)
			qid->path |= Qcommitmsg;
		else
			e = Eexist;
		break;	
	case Qcommittree:
		objwalk1(qid, aux->obj, name, Qcommittree);
		break;
	case Qcommitinfo:
	case Qcommitmsg:
	case Qcommitdata:
		return Enodir;
	default:
		sysfatal("walk: bad qid");
	}
	fid->qid = *qid;
	return e;
}

static void
gitdestroyfid(Fid *f)
{
	Gitaux *aux;

	if(aux = f->aux){
		if(aux->obj)
			freeobject(aux->obj);
		free(aux->obj);
		free(f->aux);
	}
}

static void
gitread(Req *r)
{
	Gitaux *aux;
	Object *o;
	Qid *q;

	q = &r->fid->qid;
	o = nil;
	if(aux = r->fid->aux)
		o = aux->obj;

	switch(QDIR(q)){
	case Qroot:
		dirread9p(r, rootgen, nil);
		break;
	case Qbranch:
		dirread9p(r, branchgen, aux);
		break;
	case Qobject:
		if(o)
			readobj(r, o);
		else
			dirread9p(r, objgen, aux->ols);
		break;
	case Qcommit:
		readobj(r, o);
		break;
	case Qcommitmsg:
		readstr(r, o->msg);
		break;
	case Qcommitinfo:
		readcommitinfo(r, o);
		break;
	case Qcommittree:
		readobj(r, o);
		break;
	case Qcommitdata:
		readobj(r, o);
		break;
	default:
		sysfatal("read: bad qid");
	}
	respond(r, nil);
}

static void
gitwrite(Req *r)
{
	respond(r, "unimplemented write");
}

static void
gitstat(Req *r)
{
	Gitaux *aux;
	Qid *q;

	q = &r->fid->qid;
	aux = r->fid->aux;
	r->d.uid = estrdup9p(username);
	r->d.gid = estrdup9p(username);
	r->d.muid = estrdup9p(username);
	r->d.mtime = time(0);
	r->d.atime = r->d.mtime;
	r->d.qid = r->fid->qid;

	r->d.mode = 0755 | DMDIR;
	switch(QDIR(q)){
	case Qroot:
		r->d.name = estrdup9p("/");
		break;
	case Qbranch:
		if(aux && aux->obj)
			goto statobj;
		r->d.name = estrdup9p("branch");
		break;
	case Qobject:
		if(aux && aux->obj)
			goto statobj;
		r->d.name = estrdup9p("object");
	case Qcommit:
		r->d.name = smprint("%H", aux->obj->hash);
		break;
	case Qcommitmsg:
		r->d.name = estrdup9p("msg");
		r->d.mode = 0644;
		break;
	case Qcommittree:
		r->d.name = estrdup9p("tree");
		break;
	case Qcommitinfo:
		r->d.name = estrdup9p("info");
		r->d.mode = 0644;
		break;
	default:
		sysfatal("stat: bad qid");
	}
	if(0){
statobj:
		r->d.name = emalloc9p(41);
		if(aux->obj->type == GBlob || aux->obj->type == GTag)
			r->d.mode = 0644;
		snprint(r->d.name, 41, "%H", aux->obj->hash);
	}
	respond(r, nil);
}

static void
gitwstat(Req *r)
{
	respond(r, "unimplemented wstat");
}

static void
gitremove(Req *r)
{
	respond(r, "unimplemented remove");
}

Srv gitsrv = {
	.attach=gitattach,
	.walk1=gitwalk1,
	.clone=gitclone,
	.read=gitread,
	.write=gitwrite,
	.stat=gitstat,
	.wstat=gitwstat,
	.remove=gitremove,
	.destroyfid=gitdestroyfid,
};

void
usage(void)
{
	print("usage: %s [-Rd[ [-m mtpt]\n", argv0);
	print("\t-R:	mount readonly\n");
	print("\t-d:	debug\n");
	print("\t-m mp:	mount on mountpoint'mp'\n");
}

void
threadmain(int argc, char **argv)
{
	gitinit();
	ARGBEGIN{
	case 'd':	chatty9p++;		break;
	case 'm':	mtpt = EARGF(usage());	break;
	}ARGEND;

	username = getuser();
	threadpostmountsrv(&gitsrv, nil, mtpt, MCREATE);
}
