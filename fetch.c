#include <u.h>
#include <libc.h>

#include "git.h"

enum {
	Nproto	= 16,
	Nport	= 16,
	Nhost	= 256,
	Npath	= 128,
	Nrepo	= 64,
};

Object *indexed;
char *clonebranch;

void
usage(void)
{
	fprint(2, "usage: %s [-V] [-b br] remote\n", argv0);
	fprint(2, "\t-b br:	only fetch matching branch 'br'\n");
	fprint(2, "remote:	fetch from this repository\n");
	exits("usage");
}

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

	fprint(2, "dialing %s...\n", host);
	if(pipe(pfd) == -1)
		sysfatal("unable to open pipe: %r");
	pid = fork();
	if(pid == -1)
		sysfatal("unable to fork");
	if(pid == 0){
		close(pfd[1]);
		dup(pfd[0], 0);
		dup(pfd[0], 1);
		execl("/bin/ssh", "ssh", host, "git-upload-pack", path, nil);
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
	fprint(2, "dialing %s...\n", ds);
	fd = dial(ds, nil, nil, nil);
	if(fd == -1)
		return -1;
	l = 0;
	l += snprint(cmd, sizeof(cmd), "git-upload-pack %s", path) + 1;
	l += snprint(cmd + l, sizeof(cmd) - l, "host=%s", host) + 1;
	if(writepkt(fd, cmd, l) == -1){
		fprint(2, "failed to write message\n");
		close(fd);
		return -1;
	}
	return fd;
}

int
resolveremote(Hash *h, char *ref)
{
	char buf[128], *s;
	int r, f;

	ref = strip(ref);
	if((r = hparse(h, ref)) != -1)
		return r;
	/* Slightly special handling: translate remote refs to local ones. */
	if(strcmp(ref, "HEAD") == 0){
		snprint(buf, sizeof(buf), ".git/HEAD");
	}else if(strstr(ref, "refs/heads") == ref){
		ref += strlen("refs/heads");
		snprint(buf, sizeof(buf), ".git/refs/remotes/%s/%s", clonebranch, ref);
	}else if(strstr(ref, "refs/tags") == ref){
		ref += strlen("refs/tags");
		snprint(buf, sizeof(buf), ".git/refs/tags/%s/%s", clonebranch, ref);
	}else{
		return -1;
	}

	s = strip(buf);
	if((f = open(s, OREAD)) == -1)
		return -1;
	if(readn(f, buf, sizeof(buf)) >= 40)
		r = hparse(h, buf);
	close(f);

	if(r == -1 && strstr(buf, "ref:") == buf)
		return resolveremote(h, buf + strlen("ref:"));
	
	return r;
}

int
rename(char *pack, char *idx, Hash h)
{
	char name[128];
	Dir st;

	nulldir(&st);
	st.name = name;
	snprint(name, sizeof(name), "%H.pack", h);
	if(dirwstat(pack, &st) == -1)
		return -1;
	snprint(name, sizeof(name), "%H.idx", h);
	if(dirwstat(idx, &st) == -1)
		return -1;
	return 0;
}

int
checkhash(int fd, vlong sz, Hash *hcomp)
{
	DigestState *st;
	Hash hexpect;
	char buf[65536];
	vlong n, r;
	int nr;
	
	if(sz < 28){
		werrstr("undersize packfile");
		return -1;
	}

	st = nil;
	n = 0;
	while(n != sz - 20){
		nr = sizeof(buf);
		if(sz - n - 20 < sizeof(buf))
			nr = sz - n - 20;
		if((r = readn(fd, buf, nr)) != nr)
			return -1;
		st = sha1((uchar*)buf, nr, nil, st);
		n += r;
	}
	sha1(nil, 0, hcomp->h, st);
	if(readn(fd, hexpect.h, sizeof(hexpect.h)) != sizeof(hexpect.h))
		sysfatal("truncated packfile");
	if(!hasheq(hcomp, &hexpect)){
		werrstr("bad hash: %H != %H", *hcomp, hexpect);
		return -1;
	}
	return 0;
}

int
fetchpack(int fd, char *packtmp)
{
	char buf[65536];
	char idxtmp[256];
	char *sp[3];
	Hash h, *have, *want;
	int nref, refsz;
	int i, n, req, pfd;
	vlong packsz;

	nref = 0;
	refsz = 16;
	have = emalloc(refsz * sizeof(have[0]));
	want = emalloc(refsz * sizeof(want[0]));
	while(1){
		n = readpkt(fd, buf, sizeof(buf));
		if(n == -1)
			return -1;
		if(n == 0)
			break;
		if(strncmp(buf, "ERR ", 4) == 0)
			sysfatal("%s", buf + 4);
		getfields(buf, sp, nelem(sp), 1, " \t\n\r");
		if(strstr(sp[1], "^{}"))
			continue;
		if(refsz == nref + 1){
			refsz *= 2;
			have = erealloc(have, refsz * sizeof(have[0]));
			want = erealloc(want, refsz * sizeof(want[0]));
		}	
		if(hparse(&want[nref], sp[0]) == -1)
			sysfatal("invalid hash %s", sp[0]);
		if (resolveremote(&have[nref], sp[1]) == -1)
			memset(&have[nref], 0, sizeof(have[nref]));
		print("remote %s %H local %H\n", sp[1], want[nref], have[nref]);
		nref++;
	}

	req = 0;
	for(i = 0; i < nref; i++){
		if(memcmp(have[i].h, want[i].h, sizeof(have[i].h)) == 0)
			continue;
		n = snprint(buf, sizeof(buf), "want %H", want[i]);
		print("want %H\n", want[i]);
		if(writepkt(fd, buf, n) == -1)
			sysfatal("could not send want for %H", want[i]);
		req = 1; 
	}
	flushpkt(fd);
	for(i = 0; i < nref; i++){
		if(memcmp(have[i].h, Zhash.h, sizeof(Zhash.h)) == 0)
			continue;
		n = snprint(buf, sizeof(buf), "have %H\n", have[i]);
		if(writepkt(fd, buf, n + 1) == -1)
			sysfatal("could not send have for %H", have[i]);
	}
	if(!req){
		fprint(2, "up to date\n");
		flushpkt(fd);
	}
	n = snprint(buf, sizeof(buf), "done\n");
	if(writepkt(fd, buf, n) == -1)
		sysfatal("lost connection write");
	if((n = readpkt(fd, buf, sizeof(buf))) == -1)
		sysfatal("lost connection read");
	buf[n] = 0;
	pfd = create(packtmp, ORDWR, 0644);
	if(pfd == -1)
		sysfatal("could not open pack %s", packtmp);
	fprint(2, "fetching...\n");

	packsz = 0;
	while(1){
		n = readn(fd, buf, sizeof buf);
		if(n == 0)
			break;
		if(n == -1 || write(pfd, buf, n) != n)
			sysfatal("could not fetch packfile: %r");
		packsz += n;
	}
	if(seek(pfd, 0, 0) == -1)
		sysfatal("packfile seek: %r");
	if(checkhash(pfd, packsz, &h) == -1)
		sysfatal("corrupt packfile: %r");
	close(pfd);
	n = strlen(packtmp) - strlen(".tmp");
	memcpy(idxtmp, packtmp, n);
	memcpy(idxtmp + n, ".idx", strlen(".idx") + 1);
	if(indexpack(packtmp, idxtmp, h) == -1)
		sysfatal("could not index fetched pack: %r");
	if(rename(packtmp, idxtmp, h) == -1)
		sysfatal("could not rename indexed pack: %r");
	return 0;
}

void
main(int argc, char **argv)
{
	char proto[Nproto], host[Nhost], port[Nport];
	char repo[Nrepo], path[Npath];
	int fd;

	ARGBEGIN{
	case 'b':	clonebranch=EARGF(usage());	break;
	default:	usage();			break;
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
	if(fetchpack(fd, ".git/objects/pack/fetch.tmp") == -1)
		sysfatal("fetch failed: %r");
	exits(nil);
}
