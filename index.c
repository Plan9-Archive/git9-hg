#include <u.h>
#include <libc.h>

#include "git.h"

int
idxheader(int fd, int *nobj, int *vers)
{
	char buf[12];

	if(readn(fd, buf, sizeof(buf)) != sizeof(buf)){
		werrstr("short header");
		return -1;
	}
	if(memcmp(buf, "DIRC", 4) != 0){
		werrstr("invalid signature");
		return -1;
	}		
	*vers = GETBE32(buf+4);
	if(*vers < 2 || *vers > 4){
		werrstr("invalid version %d", *vers);
		return -1;
	}
	*nobj = GETBE32(buf+8);
	return 0;
}

int
idxobject(int fd, Idxent *c, char *sb, int nsb, int vers)
{
	char buf[10*4 + 20 + 4];
	int fixed, nname, mode, flags, n;

	fixed = 10*4 + 20 + 2;
	if(vers >= 3)
		fixed += 2;
	if(readn(fd, buf, fixed) != fixed){
		werrstr("short index entry");
		return -1;
	}
	c->atime = GETBE32(buf + 0);
	c->mtime = GETBE32(buf + 8);
	c->dev = GETBE32(buf + 16);
	c->qid.path = GETBE32(buf + 20);
	mode = GETBE32(buf + 24);
	c->mode = mode & 0777;
	if((mode & 0xf000) != 0x8000){
		print("non-file in index");
		//return -1;
	}
	c->uid = nil;
	c->gid = nil;
	c->length = GETBE32(buf + 36);
	memcpy(c->h.h, buf + 40, sizeof(c->h.h));
	flags = GETBE16(buf + 60);
	nname = flags & 0x0fff;
	if(nname + 1 >= nsb){
		werrstr("out of string space");
		return -1;
	}
	if(readn(fd, sb, nname) != nname){
		werrstr("short read of name");
		return -1;
	}
	c->name = sb;
	sb[nname] = 0;
	n = 8 - (fixed + nname)%8;
	if(readn(fd, buf, n) != n)
		return -1;
	return nname + 1;
}

long
idxread(char *idx, char *pfx, Idxent **entp)
{
	int nobj, vers, nmatch, npfx;
	int fd, slack, n, i;
	Idxent *ent;
	char *sbuf;

	if((fd = open(idx, OREAD)) == -1)
		return -1;
	if(idxheader(fd, &nobj, &vers) == -1){
		werrstr("invalid index %s", idx);
		return -1;
	}

	nmatch = 0;
	npfx = strlen(pfx);
	slack = nobj*256;
	ent = emalloc(nobj*sizeof(Idxent) + slack);
	sbuf = (char*)(ent + nobj);
	for(i = 0; i < nobj; i++){
		if((n = idxobject(fd, ent + nmatch, sbuf, slack, vers)) == -1){
			free(ent);
			return -1;	
		}
		if(strncmp(ent[nmatch].name, pfx, npfx) == 0){
			slack -= n;
			sbuf += n;
			nmatch++;
		}
	}
	*entp = ent;
	return nmatch;
}

int
idxwrite(int, Idxent *, long)
{
	werrstr("unimplemented");
	return -1;
}

void
idxput(Idxent*, int, Idxent*)
{
}

void
idxdel(Idxent*, int, char *)
{
}
