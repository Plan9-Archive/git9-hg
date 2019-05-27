#include <u.h>
#include <libc.h>
#include <ctype.h>

#include "git.h"

typedef struct Eval	Eval;
typedef struct XObject	XObject;

struct Eval {
	char	*str;
	char	*p;
	Object	**stk;
	int	nstk;
	int	stksz;
};

struct XObject {
	Object	*obj;
	Object	*mark;
	XObject	*queue;
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

XObject*
hnode(XObject *ht[], Object *o)
{
	XObject *h;
	int	hh;

	hh = o->hash.h[0] & 0xff;
	for(h = ht[hh]; h; h = h->next)
		if(hasheq(&o->hash, &h->obj->hash))
			return h;

	h = malloc(sizeof(*h));
	h->obj = o;
	h->mark = nil;
	h->queue = nil;
	h->next = ht[hh];
	ht[hh] = h;
	return h;
}

int
ancestor(Eval *ev)
{
	Object *a, *b, *o, *p;
	XObject *ht[256];
	XObject *h, *q, *q1, *q2;
	int i, r;

	if(ev->nstk < 2){
		werrstr("ancestor needs 2 objects");
		return -1;
	}
	a = pop(ev);
	b = pop(ev);
	if(a == b){
		push(ev, a);
		return 0;
	}

	r = -1;
	memset(ht, 0, sizeof(ht));
	q1 = nil;

	h = hnode(ht, a);
	h->mark = a;
	h->queue = q1;
	q1 = h;

	h = hnode(ht, b);
	h->mark = b;
	h->queue = q1;
	q1 = h;

	while(1){
		q2 = nil;
		while(q = q1){
			q1 = q->queue;
			q->queue = nil;
			o = q->obj;
			for(i = 0; i < o->commit->nparent; i++){
				p = readobject(o->commit->parent[i]);
				h = hnode(ht, p);
				if(h->mark != nil){
					if(h->mark != q->mark){
						push(ev, h->obj);
						r = 0;
						goto done;
					}
				} else {
					h->mark = q->mark;
					h->queue = q2;
					q2 = h;
				}
			}
		}
		if(q2 == nil){
			werrstr("no common ancestor");
			break;
		}
		q1 = q2;
	}
done:
	for(i=0; i<nelem(ht); i++){
		while(h = ht[i]){
			ht[i] = h->next;
			free(h);
		}
	}
	return r;
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
	while(1){
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
		}
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
