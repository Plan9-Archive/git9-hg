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

Cache seencache[NCACHE];
int quiet;
char branch[256] = "/mnt/git/HEAD/tree";
char *rstr = "R ";
char *tstr = "T ";
char *dstr = "D ";

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
	qsort(r->path, r->npath, sizeof(r->path[0]), cmp);
	for(i = 0; i < r->npath; i++){
		if(strcmp(r->path[o], r->path[i]) != 0){
			o++;
			r->path[o] = r->path[i];
		}
	}
	r->npath = o;
}

static void
findroot(void)
{
	char path[256], buf[256], *p;

	if((getwd(path, sizeof(path))) == nil)
		sysfatal("could not get wd: %r");
	while((p = strrchr(path, '/')) != nil){
		if(snprint(buf, sizeof(buf), "%s/.git", path) >= sizeof(buf))
			sysfatal("path too long");
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
	char qs[64], fqs[64], *q;
	Dir *d;
	int fd, n;

	if((fd = open(qf, OREAD)) == -1)
		return -1;
	if((n = readn(fd, fqs, sizeof(fqs) - 1)) == -1)
		return -1;
	close(fd);
	fqs[n] = 0;
	q = strtok(fqs, " \t\n\r");

	if((d = dirstat(f)) == nil)
		return -1;
	snprint(qs, sizeof(qs), "%ullx.%uld.%.2uhhx",
	    d->qid.path, d->qid.vers, d->qid.type);
	if(strcmp(qs, q) == 0)
		return 0;
	return -1;
}

void
usage(void)
{
	fprint(2, "usage: %s [-q]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char rmpath[256], tpath[256], *p, *b, *dirty;
	Wres r;
	int i;

	ARGBEGIN{
	case 'q':
		quiet++;
		break;
	case 'b':
		b = EARGF(usage());
		if(snprint(branch, sizeof(branch), BFMT, b) >= sizeof(branch))
			sysfatal("overlong branch name");			
		break;
	case 'c':
		rstr = "";
		tstr = "";
		dstr = "";
		break;
	default:
		usage();
	}ARGEND

	findroot();
	dirty = nil;
	r.path = nil;
	r.npath = 0;
	r.pathsz = 0;
	if(access(branch, AEXIST) == 0 && readpaths(&r, branch, "") == -1)
		sysfatal("read branch files: %r");
	if(access(TDIR, AEXIST) == 0 && readpaths(&r, TDIR, "") == -1)
		sysfatal("read tracked: %r");
	if(access(RDIR, AEXIST) == 0 && readpaths(&r, RDIR, "") == -1)
		sysfatal("read removed: %r");
	dedup(&r);

	for(i = 0; i < r.npath; i++){
		p = r.path[i];
		if(snprint(rmpath, sizeof(rmpath), RDIR"/%s", p) >= sizeof(rmpath))
			sysfatal("overlong path");
		if(snprint(tpath, sizeof(tpath), TDIR"/%s", p) >= sizeof(tpath))
			sysfatal("overlong path");
		if(access(p, AEXIST) != 0 || access(rmpath, AEXIST) == 0){
			print("%s: nope: %r\n", p);
			dirty = "dirty";
			if(!quiet)
				print("%s%s\n", rstr, p);
		}else if(sameqid(p, tpath) != 0){
			dirty = "dirty";
			if(!quiet)
				print("%s%s\n", dstr, p);
		}else{
			if(!quiet)
				print("%s%s\n", tstr, p);
		}
	}
	exits(dirty);
}
