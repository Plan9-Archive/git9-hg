#include <u.h>
#include <libc.h>
#include <ctype.h>

#include "git.h"

typedef struct Eval	Eval;
struct Eval {
	char	*str;
	char	*p;
	Object	**stk;
	int	nstk;
	int	stksz;
};

struct XObject {
	Object	*mark;
	Object	*val;
	XObject	*next;
};

int fullpath;

void
eatspace(Eval *ev)
{
	while(isspace(ev->p[0]))
		ev->p++;
}

void
push(Eval *ev, Object *o)
{
	if(ev->nstk == ev->stksz){
		ev->stksz = 2*ev->stksz + 1;
		ev->stk = erealloc(ev->stk, ev->stksz*sizeof(Object*));
	}
	ev->stk[ev->nstk++] = o;
}

Object*
pop(Eval *ev)
{
	if(ev->nstk == 0)
		sysfatal("stack underflow");
	return ev->stk[--ev->nstk];
}

Object*
peek(Eval *ev)
{
	if(ev->nstk == 0)
		sysfatal("stack underflow");
	return ev->stk[ev->nstk - 1];
}

int
word(Eval *ev, char *b, int nb)
{
	char *p, *e;
	int n;

	p = ev->p;
	for(e = p; isalnum(*e) || *e == '/'; e++)
		/* nothing */;
	/* 1 for nul terminator */
	n = e - p + 1;
	if(n >= nb)
		n = nb;
	snprint(b, n, "%s", p);
	ev->p = e;
	return n > 0;
}

int
take(Eval *ev, char *m)
{
	int l;

	l = strlen(m);
	if(strncmp(ev->p, m, l) != 0)
		return 0;
	ev->p += l;
	return 1;
}

int
ancestor(Eval *ev)
{
	Object *a, *b;
	XObject **tab;

	a = pop(ev);
	b = pop(ev);

	while(1){
		if(a == b){
			push(ev, a);
			return 0;
		}
		
	}
	return -1;
}

int
parent(Eval *ev)
{
	Object *o, *p;

	o = pop(ev);
	/* Special case: first commit has no parent. */
	if(o->commit->nparent == 0 || (p = readobject(o->commit->parent[0])) == nil){
		werrstr("no parent for %H", o->hash);
		return -1;
	}
	push(ev, p);
	return 0;
}

int
range(Eval *)
{
	werrstr("unimplemented");
	return -1;
}

int
readref(Hash *h, char *ref)
{
	static char *try[] = {"", "heads/", "remotes/", "tags/", nil};
	char buf[256], s[64], **pfx;
	int r, f, n;

	/* TODO: support hash prefixes */
	if((r = hparse(h, ref)) != -1)
		return r;
	if(strcmp(ref, "HEAD") == 0){
		snprint(buf, sizeof(buf), ".git/HEAD");
		if((f = open(buf, OREAD)) == -1)
			return -1;
		if(readn(f, s, sizeof(s)) >= 40)
			r = hparse(h, s);
		goto found;
	}
	for(pfx = try; *pfx; pfx++){
		snprint(buf, sizeof(buf), ".git/refs/%s%s", *pfx, ref);
		if((f = open(buf, OREAD)) == -1)
			continue;
		n = readn(f, s, sizeof(s) - 1);
		if(n >= 0)
			s[n] = 0;
		r = hparse(h, s);
		close(f);
		goto found;
	}
	return -1;

found:
	if(r == -1 && strstr(buf, "ref: ") == buf)
		r = readref(h, buf + strlen("ref: "));
	return r;
}

int
evalpostfix(Eval *ev)
{
	char name[256];
	Object *o;
	Hash h;

	eatspace(ev);
	if(!word(ev, name, sizeof(name))){
		werrstr("expected name in expression");
		return -1;
	}
	if(readref(&h, name) == -1){
		werrstr("could not resolve ref %s", name);
		return -1;
	}else if((o = readobject(h)) == nil){
		werrstr("invalid ref %s (hash %H)", name, h);
		return -1;
	}
	push(ev, o);

	while(1){
		eatspace(ev);
		switch(ev->p[0]){
		case '^':
			ev->p++;
			if(parent(ev) == -1)
				return -1;
			break;
		case '@':
			ev->p++;
			if(ancestor(ev) == -1)
				return -1;
			break;
		default:
			goto done;
			break;
		}	
	}
done:
	return 0;
}

int
evalexpr(Eval *ev)
{
	if(evalpostfix(ev) == -1)
		return -1;
	if(ev->p[0] == '\0')
		return 0;
	else if(take(ev, ":") || take(ev, "..")){
		if(evalpostfix(ev) == -1)
			return -1;
		if(ev->p[0] != '\0'){
			werrstr("junk at end of expression");
			return -1;
		}
		return range(ev);
	}else{
		werrstr("unknown operator %c", *ev->p);
		return -1;
	}
}

int
resolverefs(Hash **r, char *ref)
{
	Eval ev;
	Hash *h;
	int i;

	memset(&ev, 0, sizeof(ev));
	ev.str = ref;
	ev.p = ref;
	if(evalexpr(&ev) == -1){
		free(ev.stk);
		return -1;
	}
	h = emalloc(ev.nstk*sizeof(Hash));
	for(i = 0; i < ev.nstk; i++)
		h[i] = ev.stk[i]->hash;
	*r = h;
	return ev.nstk;
}

void
usage(void)
{
	fprint(2, "usage: %s [-p]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, j, n;
	Hash *h;

	ARGBEGIN{
	case 'p':	fullpath++;	break;
	default:	usage();	break;
	}ARGEND;

	gitinit();
	for(i = 0; i < argc; i++){
		if((n = resolverefs(&h, argv[i])) == -1)
			sysfatal("resolve %s: %r", argv[i]);
		for(j = 0; j < n; j++)
			print("%H\n", h[j]);
	}
}
