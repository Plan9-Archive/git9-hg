#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

#include "git.h"

char *mtpt = "/n/git";
typedef struct Ols Ols;

char *Eperm = "permission denied";
char *Eexist = "does not exist";
char *E2long = "path too long";
char *Enodir = "not a directory";
char *Egreg = "wat";

enum {
	Qroot,
	Qbranch,
	Qcommit,
	Qcommitmsg,
	Qcommitparent,
	Qcommittree,
	Qcommitdata,
	Qcommithash,
	Qcommitauthor,
	Qobject,
	Qctl,
	Qmax,
};

enum {
	Npath = 64,
};

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
	Object *oprev;
	char pfx[32];
};

typedef struct Gitaux Gitaux;
struct Gitaux {
	Qid	 path[Npath];
	Object  *opath[Npath];
	int	 npath;
	Object	*obj;
	char 	*refpath;
	Ols	*ols;
	int	 qdir;
};

char *qroot[] = {
	"branch",
	"object",
	"ctl",
};

char *username;

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
gtreegen(int i, Dir *d, void *p)
{
	Gitaux *aux;
	Object *o;

	aux = p;
	if(i >= aux->obj->nent)
		return -1;
	
	if((o = readobject(aux->obj->ent[i].h)) == nil)
		die("could not read object %H", aux->obj->ent[i].h);
	d->mode = aux->obj->ent[i].mode;
	d->name = estrdup9p(aux->obj->ent[i].name);
	d->qid.path = QPATH(o->id, aux->qdir);
	d->length = o->size;
	return 0;
}

static int
gcommitgen(int i, Dir *d, void *p)
{
	Object *o;

	o = ((Gitaux*)p)->obj;
	d->uid = estrdup9p(username);
	d->gid = estrdup9p(username);
	d->muid = estrdup9p(username);
	d->mode = 0444;
	d->atime = o->ctime;
	d->mtime = o->ctime;

	switch(i){
	case 0:
		d->mode = 0555 | DMDIR;
		d->name = estrdup9p("tree");
		d->qid.path = QPATH(o->id, Qcommittree);
		break;
	case 1:
		d->name = estrdup9p("parent");
		d->qid.path = QPATH(o->id, Qcommitparent);
		break;
	case 2:
		d->name = estrdup9p("msg");
		d->qid.path = QPATH(o->id, Qcommitmsg);
		break;
	case 3:
		d->name = estrdup9p("hash");
		d->qid.path = QPATH(o->id, Qcommithash);
		break;
	case 4:
		d->name = estrdup9p("author");
		d->qid.path = QPATH(o->id, Qcommitauthor);
		break;
	default:
		return -1;
	}
	return 0;
}

static void
obj2dir(Dir *d, Object *o, long qdir)
{
	char name[64];

	snprint(name, sizeof(name), "%H", o->hash);
	d->name = estrdup9p(name);
	d->qid.type = QTDIR;
	d->qid.path = QPATH(o->id, qdir);
	d->atime = o->ctime;
	d->mtime = o->mtime;
	d->mode = 0755 | DMDIR;
	if(o->type == GBlob || o->type == GTag){
		d->qid.type = 0;
		d->mode = 0644;
		d->length = o->size;
	}

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
	st->oprev = nil;
	if((st->n0 = slurpdir(".git/objects", &st->d0)) == -1)
		return -1;
	return 0;
}

static int
openpack(char *path, int *np)
{
	int fd;
	char buf[4];

	if((fd = open(path, OREAD)) == -1)
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
	char *n;
	int nn, np;

	close(st->pfd);
	while(1){
		if(st->l1 == st->n1){
			st->inpack = 0;
			return -1;
		}
		n = st->d1[st->l1].name;
		nn = strlen(n);
		np = strlen(".idx");
		st->l1++;
		if(nn > np && strcmp(n + nn - np, ".idx") != 0)
			continue;
		snprint(path, sizeof(path), ".git/objects/pack/%s", n);
		if((st->pfd = openpack(path, &st->np)) == -1)
			return -1;
		return 0;
	}
}

static int
nextdir(Ols *st)
{
	char path[128];

	if(st->l0 == st->n0)
		return -1;
	snprint(path, sizeof(path), ".git/objects/%s", st->d0[st->l0].name);
	st->inpack = 0;
	if(strcmp(st->d0[st->l0].name, "pack") == 0)
		st->inpack = 1;
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
				obj2dir(d, o, Qobject);
				st->lp++;
				st->oprev = o;
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
				obj2dir(d, o, Qobject);
				st->l1++;
				st->oprev = o;
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
	/* We tried to sent it, but it didn't fit */
	if(st->oprev && i == st->last - 1){
		obj2dir(d, st->oprev, Qobject);
		st->oprev = nil;
		return 0;
	}
	/* We restarted */
	if(i < st->last){
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
objread(Req *r, Gitaux *aux)
{
	Object *o;

	o = aux->obj;
	switch(o->type){
	case GBlob:
		readbuf(r, o->data, o->size);
		break;
	case GTag:
		readbuf(r, o->data, o->size);
		break;
	case GTree:
		dirread9p(r, gtreegen, aux);
		break;
	case GCommit:
		dirread9p(r, gcommitgen, aux);
		break;
	default:
		die("invalid object type %d", o->type);
	}
}

static void
readcommitparent(Req *r, Object *o)
{
	char *buf, *p;
	int i, n;

	n = o->nparent * (40 + 2);
	buf = emalloc(n);
	p = buf;
	for (i = 0; i < o->nparent; i++)
		p += sprint(p, "%H\n", o->parent[i]);
	readbuf(r, buf, n);
	free(buf);
}


static void
gitattach(Req *r)
{
	Gitaux *aux;

	aux = emalloc(sizeof(Gitaux));
	aux->path[0] = (Qid){Qroot, 0, QTDIR};
	aux->opath[0] = nil;
	aux->npath = 1;
	r->ofcall.qid = (Qid){Qroot, 0, QTDIR};
	r->fid->qid = r->ofcall.qid;
	r->fid->aux = aux;
	respond(r, nil);
}

static char*
gitclone(Fid *o, Fid *n)
{
	n->aux = emalloc(sizeof(Gitaux));
	memcpy(n->aux, o->aux, sizeof(Gitaux));
	return nil;
}

static char *
objwalk1(Qid *q, Gitaux *aux, char *name, vlong qdir)
{
	Object *o, *w;
	char *e;
	int i;

	e = nil;
	o = aux->obj;
	if(o->type == GTree){
		q->type = 0;
		for(i = 0; i < o->nent; i++){
			if(strcmp(o->ent[i].name, name) != 0)
				continue;
			w = readobject(o->ent[i].h);
			if(!w)
				die("could not read object %H (%s)", o->ent[i].h, o->ent[i].name);
			aux->obj = readobject(o->ent[i].h);
			q->type = (w->type == GTree) ? QTDIR : 0;
			q->path = QPATH(w->id, qdir);
			aux->obj = w;
		}
	}else if(o->type == GCommit){
		q->type = 0;
		assert(qdir == Qcommit || qdir == Qobject || qdir == Qcommittree);
		if(strcmp(name, "msg") == 0)
			q->path = QPATH(aux->obj->id, Qcommitmsg);
		else if(strcmp(name, "parent") == 0)
			q->path = QPATH(aux->obj->id, Qcommitparent);
		else if(strcmp(name, "hash") == 0)
			q->path = QPATH(aux->obj->id, Qcommithash);
		else if(strcmp(name, "author") == 0)
			q->path = QPATH(aux->obj->id, Qcommitauthor);
		else if(strcmp(name, "tree") == 0){
			q->type = QTDIR;
			q->path = QPATH(aux->obj->id, Qcommittree);
			aux->obj = readobject(aux->obj->tree);
		}
		else
			e = Eexist;
	}else if(o->type == GTag){
		e = "tag walk unsupported";
	}
	return e;
}

static Object *
readref(char *pathstr)
{
	char buf[128], path[128], *p, *e;
	Hash h;
	int n, f;

	snprint(path, sizeof(path), "%s", pathstr);
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
		if(strncmp(buf, "ref:", 4) !=  0)
			break;

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



static char*
gitwalk1(Fid *fid, char *name, Qid *q)
{
	char path[128];
	Gitaux *aux;
	char *e;
	Dir *d;
	Hash h;

	e = nil;
	aux = fid->aux;
	q->vers = 0;

	if(strcmp(name, "..") == 0){
		if(aux->npath > 1)
			aux->npath--;
		*q = aux->path[aux->npath - 1];
		aux->obj = aux->opath[aux->npath - 1];
		fid->qid = *q;
		return nil;
	}
	

	switch(QDIR(&fid->qid)){
	case Qroot:
		if(strcmp(name, "object") == 0){
			*q = (Qid){Qobject, 0, QTDIR};
			aux->ols = emalloc(sizeof(*aux->ols));
			olsinit(aux->ols);
		}else if(strcmp(name, "branch") == 0){
			*q = (Qid){Qbranch, 0, QTDIR};
			aux->refpath = estrdup(".git/refs/");
		}else if(strcmp(name, "ctl") == 0){
			*q = (Qid){Qctl, 0, 0};
		}else{
			return Eexist;
		}
		break;
	case Qbranch:
		if(strcmp(aux->refpath, ".git/refs/heads") == 0 && strcmp(name, "HEAD") == 0)
			snprint(path, sizeof(path), ".git/HEAD");
		else if(snprint(path, sizeof(path), "%s/%s", aux->refpath, name) >= sizeof(path))
			return E2long;
		if((d = dirstat(path)) == nil)
			return Eexist;
		if(d->qid.type == QTDIR)
			aux->refpath = estrdup(path);
		else if((aux->obj = readref(path)) != nil)
			q->path = QPATH(aux->obj->id, Qcommit);
		else
			e = Eexist;
		free(d);
		break;
	case Qobject:
		if(aux->obj){
			e = objwalk1(q, aux, name, Qobject);
		}else{
			if(hparse(&h, name) == -1)
				return "invalid object name";
			if((aux->obj = readobject(h)) == nil)
				return "could not read object";
			q->path = QPATH(aux->obj->id, Qobject);
			q->type = (aux->obj->type == GBlob) ? 0 : QTDIR;
			q->vers = 0;
		}
		break;
	case Qcommit:
		e = objwalk1(q, aux, name, Qcommit);
		break;	
	case Qcommittree:
		e = objwalk1(q, aux, name, Qcommittree);
		break;
	case Qcommitparent:
	case Qcommitmsg:
	case Qcommitdata:
	case Qcommithash:
	case Qcommitauthor:
	case Qctl:
		return Enodir;
	default:
		die("walk: bad qid %Q", *q);
	}
	if(aux->npath >= Npath)
		e = E2long;
	if(QDIR(q) >= Qmax){
		print("npath: %d\n", aux->npath);
		print("walking to %llx\n", q->path);
		print("walking from %llx\n", fid->qid.path);
		print("QDIR=%lld\n", QDIR(&fid->qid));
		if(aux->obj)
			print("obj=%O\n", aux->obj);
		abort();
	}

	aux->path[aux->npath] = *q;
	aux->opath[aux->npath] = aux->obj;
	aux->npath++;
	fid->qid = *q;
	return e;
}

static void
gitdestroyfid(Fid *f)
{
	Gitaux *aux;

	if(aux = f->aux)
		free(aux);
}

static void
gitread(Req *r)
{
	char buf[64];
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
		if(o)
			objread(r, aux);
		else
			dirread9p(r, branchgen, aux);
		break;
	case Qobject:
		if(o)
			objread(r, aux);
		else
			dirread9p(r, objgen, aux->ols);
		break;
	case Qcommit:
		objread(r, aux);
		break;
	case Qcommitmsg:
		readbuf(r, o->msg, o->nmsg);
		break;
	case Qcommitparent:
		readcommitparent(r, o);
		break;
	case Qcommittree:
		objread(r, aux);
		break;
	case Qcommitdata:
		objread(r, aux);
		break;
	case Qcommithash:
		snprint(buf, sizeof(buf), "%H\n", o->hash);
		readstr(r, buf);
		break;
	case Qcommitauthor:
		readstr(r, o->author);
		break;
	case Qctl:
		readstr(r, "");
		break;
	default:
		die("read: bad qid %Q", *q);
	}
	respond(r, nil);
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
	if(aux->obj){
		obj2dir(&r->d, aux->obj, QDIR(q));
		goto done;
	}

	switch(QDIR(q)){
	case Qroot:
		r->d.name = estrdup9p("/");
		break;
	case Qbranch:
		if(aux->obj)
			r->d.name = estrdup9p("branch");
		break;
	case Qobject:
		if(!aux->obj)
			r->d.name = estrdup9p("object");
		break;
	case Qctl:
		r->d.name = estrdup9p("ctl");
		r->d.mode = 0666;
		break;
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
	case Qcommitparent:
		r->d.name = estrdup9p("info");
		r->d.mode = 0644;
		break;
	case Qcommithash:
		r->d.name = estrdup9p("hash");
		r->d.mode = 0644;
		break;
	default:
		die("stat: bad qid %Q", *q);
	}

done:
	respond(r, nil);
}

Srv gitsrv = {
	.attach=gitattach,
	.walk1=gitwalk1,
	.clone=gitclone,
	.read=gitread,
	.stat=gitstat,
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
