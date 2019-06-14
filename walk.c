#include <u.h>
#include <libc.h>
#include "git.h"

#define NCACHE 256
#define TDIR ".git/index9/tracked"
#define RDIR ".git/index9/removed"
#define BFMT "/mnt/git/branch/%s/tree"
typedef struct Cache	Cache;
typedef struct Wres	Wres;
struct Cache {
	Dir*	cache;
	int	n;
	int	max;
};

struct Wres {
	char	**path;
	int	npath;
	int	pathsz;
};

enum {
	Rflg	= 1 << 0,
	Tflg	= 1 << 1,
	Dflg	= 1 << 2,
	Aflg	= 1 << 3,
};

Cache seencache[NCACHE];
int quiet;
int printflg;
char branch[256] = "/mnt/git/HEAD/tree";
char *rstr = "R ";
char *tstr = "T ";
char *dstr = "D ";
char *astr = "A ";

int
seen(Dir *dir)
{
	Dir *dp;
	int i;
	Cache *c;

	c = &seencache[dir->qid.path&(NCACHE-1)];
	dp = c->cache;
	for(i=0; i<c->n; i++, dp++)
		if(dir->qid.path == dp->qid.path &&
		   dir->type == dp->type &&
		   dir->dev == dp->dev)
			return 1;
	if(c->n == c->max){
		if (c->max == 0)
			c->max = 8;
		else
			c->max += c->max/2;
		c->cache = realloc(c->cache, c->max*sizeof(Dir));
		if(c->cache == nil)
			sysfatal("realloc: %r");
	}
	c->cache[c->n++] = *dir;
	return 0;
}

int
readpaths(Wres *r, char *pfx, char *dir)
{
	char *f, *sub, *full, *sep;
	Dir *d;
	int fd, ret, i, n;

	ret = -1;
	fd = -1;
	sep = "";
	if(dir[0] != 0)
		sep = "/";
	if((full = smprint("%s/%s", pfx, dir)) == nil)
		goto error;
	if((fd = open(full, OREAD)) < 0)
		goto error;
	while((n = dirread(fd, &d)) > 0){
		for(i = 0; i < n; i++){
			if(seen(&d[i]))
				continue;
			if(d[i].qid.type & QTDIR){
				if((sub = smprint("%s%s%s", dir, sep, d[i].name)) == nil)
					sysfatal("smprint: %r");
				if(readpaths(r, pfx, sub) == -1){
					free(sub);
					goto error;
				}
				free(sub);
			}else{
				if(r->npath == r->pathsz){
					r->pathsz = 2*r->pathsz + 1;
					r->path = erealloc(r->path, r->pathsz * sizeof(char*));
				}
				if((f = smprint("%s%s%s", dir, sep, d[i].name)) == nil)
					goto error;
				r->path[r->npath++] = f;
			}
		}
	}
	ret = r->npath;
error:
	close(fd);
	free(full);
	free(d);
	return ret;
}

int
cmp(void *pa, void *pb)
{
	return strcmp(*(char **)pa, *(char **)pb);
}

void
dedup(Wres *r)
{
	int i, o;

	o = 0;
	i = 0;
	qsort(r->path, r->npath, sizeof(r->path[0]), cmp);
	while(i < r->npath){
		r->path[o++] = r->path[i++];
		while(i < r->npath && strcmp(r->path[o], r->path[i]) == 0)
			i++;
	}
	r->npath = o;
}

static void
findroot(void)
{
	char path[256], buf[256], *p;

	if(access("/mnt/git/ctl", AEXIST) != 0)
		sysfatal("no running git/fs");
	if((getwd(path, sizeof(path))) == nil)
		sysfatal("could not get wd: %r");
	while((p = strrchr(path, '/')) != nil){
		snprint(buf, sizeof(buf), "%s/.git", path);
		if(access(buf, AEXIST) == 0){
			chdir(path);
			return;
		}
		*p = '\0';
	}
	sysfatal("not a git repository");
}

int
sameqid(char *f, char *qf)
{
	char indexqid[64], fileqid[64], *p;
	Dir *d;
	int fd, n;

	if((fd = open(qf, OREAD)) == -1)
		return -1;
	if((n = readn(fd, indexqid, sizeof(indexqid) - 1)) == -1)
		return -1;
	indexqid[n] = 0;
	close(fd);
	if((p = strpbrk(indexqid, "  \t\n\r")) != nil)
		*p = 0;

	if((d = dirstat(f)) == nil)
		return -1;
	snprint(fileqid, sizeof(fileqid), "%ullx.%uld.%.2uhhx",
	    d->qid.path, d->qid.vers, d->qid.type);
	if(strcmp(indexqid, fileqid) == 0)
		return 0;
	return -1;
}

void
usage(void)
{
	fprint(2, "usage: %s [-qbc] [-f filt]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char rmpath[256], tpath[256], bpath[256], *p, *dirty;
	Wres r;
	int i;

	ARGBEGIN{
	case 'q':
		quiet++;
		break;
	case 'b':
		snprint(branch, sizeof(branch), BFMT, EARGF(usage()));
		break;
	case 'c':
		rstr = "";
		tstr = "";
		dstr = "";
		astr = "";
		break;
	case 'f':
		for(p = EARGF(usage()); *p; p++)
			switch(*p){
			case 'T':	printflg |= Tflg;	break;
			case 'A':	printflg |= Aflg;	break;
			case 'D':	printflg |= Dflg;	break;
			case 'R':	printflg |= Rflg;	break;
			default:	usage();		break;
		}
		break;
	default:
		usage();
	}ARGEND

	findroot();
	dirty = nil;
	r.path = nil;
	r.npath = 0;
	r.pathsz = 0;
	if(printflg == 0)
		printflg = Tflg | Aflg | Dflg | Rflg;
	if(access(branch, AEXIST) == 0 && readpaths(&r, branch, "") == -1)
		sysfatal("read branch files: %r");
	if(access(TDIR, AEXIST) == 0 && readpaths(&r, TDIR, "") == -1)
		sysfatal("read tracked: %r");
	if(access(RDIR, AEXIST) == 0 && readpaths(&r, RDIR, "") == -1)
		sysfatal("read removed: %r");
	dedup(&r);

	for(i = 0; i < r.npath; i++){
		p = r.path[i];
		snprint(rmpath, sizeof(rmpath), RDIR"/%s", p);
		snprint(tpath, sizeof(tpath), TDIR"/%s", p);
		snprint(bpath, sizeof(bpath), "%s/%s", branch, p);
		if(access(p, AEXIST) != 0 || access(rmpath, AEXIST) == 0){
			dirty = "dirty";
			if(!quiet && (printflg & Rflg))
				print("%s%s\n", rstr, p);
		}else if(sameqid(p, tpath) == -1){
			dirty = "dirty";
			if(!quiet && (printflg & Dflg))
				print("%s%s\n", dstr, p);
		}else if(access(bpath, AEXIST) == -1) {
			dirty = "dirty";
			if(!quiet && (printflg & Aflg))
				print("%s%s\n", astr, p);
		}else{
			if(!quiet && (printflg & Tflg))
				print("%s%s\n", tstr, p);
		}
	}
	exits(dirty);
}
