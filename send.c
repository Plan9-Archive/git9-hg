#include <u.h>
#include <libc.h>
#include <pool.h>

#include "git.h"

enum {
	Nproto	= 16,
	Nport	= 16,
	Nhost	= 256,
	Npath	= 128,
	Nrepo	= 64,
	Nbranch	= 32,
};

int chatty;
int pushall;
char *curbranch = "master";
char *base = "/n/git";
Object *indexed;

void
usage(void)
{
	print("git/send remote [reponame]\n");
	exits("usage");
}

static int
readpkt(int fd, char *buf, int nbuf)
{
	char len[5];
	char *e;
	int n;

	if(readall(fd, len, 4) == -1)
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
	if(readall(fd, buf, n) != n)
		return -1;
	buf[n] = 0;
	return n;
}

int
writepkt(int fd, char *buf, int nbuf)
{
	char len[5];

	snprint(len, sizeof(len), "%04x", nbuf + 4);
	if(writeall(fd, len, 4) != 4)
		return -1;
	if(writeall(fd, buf, nbuf) != nbuf)
		return -1;
	return 0;
}

int
flushpkt(int fd)
{
	return writeall(fd, "0000", 4);
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
		sysfatal("unable to open pipe: %r\n");
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
		print("talking over fd %d\n", pfd[1]);
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

int
resolveref(Hash *h, char *ref)
{
	char buf[256];
	char s[64];
	int r, f;

	r = -1;
	if(strstr(ref, "refs/heads") == ref){
		ref += strlen("refs/heads");
		snprint(buf, sizeof(buf), "%s/branch/%s/hash", base, ref);
	}else if(strstr(ref, "refs/tags") == ref){
		ref += strlen("refs/tags");
		snprint(buf, sizeof(buf), "%s/tag/%s/hash", base, ref);
	}
	if((f = open(buf, OREAD)) == -1)
		return -1;
	if(readall(f, s, sizeof(s)) >= 40)
		r = hparse(h, s);
	return r;
}

int
addknown(Hash h)
{
	USED(h);
	return 0;
}

int
writepack(int fd, Hash *theirs, int ntheirs, Hash *send, int nsend)
{
	USED(fd); USED(theirs); USED(ntheirs); USED(send); USED(nsend);
	return -1;
}

int
sendpack(int fd)
{
	char buf[65536];
	char *sp[3];
	Hash zero;
	Hash theirs[64];
	Hash ours[64];
	char refnames[64][64];
	int i, n, nref, updating;

	nref = 0;
	memset(&zero, 0, sizeof(Hash));
	for(i = 0; i < nelem(theirs); i++){
		n = readpkt(fd, buf, sizeof(buf));
		if(n == -1)
			return -1;
		if(n == 0)
			break;
		if(strncmp(buf, "ERR ", 4) == 0)
			sysfatal("%s", buf + 4);

		getfields(buf, sp, nelem(sp), 1, " \t\n\r");
		if(resolveref(&ours[nref], sp[1]) == -1)
			continue;
		if(hparse(&theirs[nref], sp[0]) == -1)
			sysfatal("invalid hash %s", sp[0]);
		if(snprint(refnames[nref], sizeof(refnames[nref]), sp[1]) >= sizeof(refnames[i]))
			sysfatal("overlong ref %s", sp[1]);
		nref++;
		print("they have %s: %H\n", refnames[nref], theirs[nref]);
	}

	updating = 0;
	for(i = 0; i < nref; i++){
		if(pushall || strcmp(curbranch, refnames[i]) == 0){
			n = snprint(buf, sizeof(buf), "update %s %H\r\n", refnames[i], ours[i]);
			if(n >= sizeof(buf))
				sysfatal("overlong update\n");
			if(writepkt(fd, buf, n) == -1)
				sysfatal("unable to send update pkt");
			updating = 1;
		}
	}
	if(!updating)
		sysfatal("nothing to do here\n");
	flushpkt(fd);
	return writepack(fd, theirs, nref, ours, nref);
}

void
main(int argc, char **argv)
{
	char proto[Nproto], host[Nhost], port[Nport];
	char repo[Nrepo], path[Npath];
	int fd;

	ARGBEGIN{
	case '?':
		usage();
		break;
	case 'd':
		chatty++;
		break;
	}ARGEND;

	gitinit();
	if(argc != 1 && argc != 2)
		usage();
	fd = -1;
	if(access("/n/git/ctl", AEXIST) == -1)
		sysfatal("expect /n/git to be mounted");
	if(parseuri(argv[0], proto, host, port, path, repo) == -1)
		sysfatal("bad uri %s", argv0);
	if(argc == 2)
		strecpy(repo, repo + sizeof(repo), argv[1]);
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
		sysfatal("fetch failed: %r");

}
