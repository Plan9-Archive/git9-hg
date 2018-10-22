#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

#include "git.h"

char *mtpt = "/n/git";
#define QOBJ(qid)	((qid)->path & (~0xff))
#define QDIR(qid)	((qid)->path & (0xff))

char *Eperm = "permission denied";
char *Egreg = "wat";
vlong nqid;

enum {
	Qroot,
	Qbranch,
	Qcommit,
	Qobject,
	Qtag,
	Qlog,
	Qmax,
};

char *qnames[] = {
	[Qroot]		= "/",
	[Qbranch]	= "branch",
	[Qcommit]	= "commit",
	[Qobject]	= "object",
	[Qtag]		= "tag",
	[Qlog]		= "log",
	[Qmax]		= nil,
};

typedef struct Gitaux Gitaux;
struct Gitaux {
	Object *obj;
	vlong off;
};

static void *
emalloc(ulong n)
{
	void *v;
	
	v = mallocz(n, 1);
	if(v == nil) sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&n));
	return v;
}

static char *
findobject(Object *o, char *name, Qid *qid, int type)
{
	Hash h;

	if(hparse(&h, name) == -1)
		return "invalid commit hash";
	if(readobject(o, h) == -1)
		return "unable to find object";
	if(type && o->type != type){
		freeobject(o);
		return "wrong object type";
	}
	qid->type = QTFILE;
	if(o->type == GCommit || o->type == GTree)
		qid->type = QTDIR;
	qid->vers = 0;
	qid->path = ++nqid << 4;
	return 0;
}

static char *
findent(Object *o, char *name, Qid *qid)
{
	USED(o); USED(name); USED(qid);
	return "unimplemented: walk git ent";
}

static void
findbranch(char *name, Qid *qid)
{
	USED(name); USED(qid);
}

static int
rootgen(int i, Dir *d, void *)
{
	i++;
	if(i >= Qmax)
		return -1;
	print("i: %d\n", i);
	print("name: %s\n", qnames[i]);

	d->mode = 0555 | DMDIR;
	d->name = estrdup9p(qnames[i]);
	d->qid.path = i;
	d->uid = "glenda";
	d->gid = "glenda";
	d->muid = "glenda";
	return 0;
}

static int
branchgen(int i, Dir *d, void *)
{
	USED(i); USED(d);
	return -1;
}

static int
commitgen(int i, Dir *d, void *)
{
	USED(i); USED(d);
	return -1;
}

static int
taggen(int i, Dir *d, void *)
{
	USED(i); USED(d);
	return -1;
}

static void
gitattach(Req *r)
{
	Gitaux *aux;

	aux = emalloc(sizeof(Gitaux));
	r->ofcall.qid = (Qid){Qroot, 0, QTDIR};
	r->fid->qid = r->ofcall.qid;
	r->fid->aux = aux;
	print("aux: %p\n", r->fid->aux);
	respond(r, nil);
}

static char *
gitwalk1(Fid *fid, char *name, Qid *qid)
{
	Gitaux *aux;
	char *e;

	e = nil;
	if((fid->qid.type & QTDIR) == 0)
		return "walk in non-directory";

	aux = fid->aux;
	switch(QDIR(qid)){
	case Qroot:
		if(strcmp(name, "branch") == 0){
			qid->type = QTDIR;
			qid->path = Qbranch;
		}else if(strcmp(name, "log") == 0){
			qid->type = QTFILE;
			qid->path = Qlog;
		}
		break;
	case Qbranch:
		findbranch(name, qid);
		fid->qid = *qid;
		break;
	case Qcommit:
		e = findobject(aux->obj, name, qid, GCommit);
		fid->qid = *qid;
		break;
	case Qtag:
		e = findobject(aux->obj, name, qid, GTag);
		fid->qid = *qid;
		break;
	case Qlog:
		break;
	case Qobject:
		if(aux->obj->type == GTree)
			e = findent(aux->obj, name, qid);
		else
			return "walk in non-directory";
		break;
	default:
		return "invalid qid";
	}
	return e;
}

static char*
gitclone(Fid *, Fid *n)
{
	n->aux = emalloc(sizeof(Gitaux));
	return nil;
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
gitopen(Req *r)
{
	respond(r, "unimplemented open");
}

static void
gitcreate(Req *r)
{
	respond(r, "permission denied");
}

static void
gitread(Req *r)
{
	switch(QDIR(&r->fid->qid)){
	case Qroot:
		if(QOBJ(&r->fid->qid)){
			respond(r, "invalid qid");
			return;
		}
		print("data ptr: %p\n", (uchar*)r->ofcall.data);
		dirread9p(r, rootgen, nil);
		respond(r, nil);
		break;
	case Qbranch:
		dirread9p(r, branchgen, r->fid->aux);
		break;
	case Qcommit:
		dirread9p(r, commitgen, r->fid->aux);
		break;
	case Qtag:
		dirread9p(r, commitgen, r->fid->aux);
		break;
	case Qlog:
		break;
	default:
		print("qid: %lld\n", r->fid->qid.path);
		break;
	}
}

static void
gitwrite(Req *r)
{
	respond(r, "unimplemented write");
}

static void
gitstat(Req *r)
{
	switch(QDIR(&r->fid->qid)){
	case Qroot:
	case Qbranch:
		dirread9p(r, branchgen, r->fid->aux);
		break;
	case Qcommit:
		dirread9p(r, commitgen, r->fid->aux);
		break;
	case Qtag:
		dirread9p(r, commitgen, r->fid->aux);
		break;
	case Qlog:
		break;
	default:
		print("qid: %lld\n", r->fid->qid.path);
		break;
	}
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

static void
gitend(Srv*)
{
}

Srv gitsrv = {
	.attach=gitattach,
	.walk1=gitwalk1,
	.clone=gitclone,
	.open=gitopen,
	.read=gitread,
	.write=gitwrite,
	.stat=gitstat,
	.wstat=gitwstat,
	.remove=gitremove,
	.destroyfid=gitdestroyfid,
	.end=gitend,
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

	threadpostmountsrv(&gitsrv, nil, mtpt, MCREATE);
}
