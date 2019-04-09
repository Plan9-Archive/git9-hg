#include <u.h>
#include <libc.h>
#include <pool.h>

#include "git.h"

void
osinit(Objset *s)
{
	s->sz = 16;
	s->nobj = 0;
	s->obj = emalloc(s->sz * sizeof(Hash));
	s->has = emalloc(s->sz);
}

void
osadd(Objset *s, Object *o)
{
	u32int probe;


	probe = GETBE32(o->hash.h) % s->sz;
	while(s->has[probe] && !hasheq(&s->obj[probe], &o->hash))
		probe++;
	s->has[probe] = 1;
	s->obj[probe] = o->hash;
	s->nobj++;
	if(s->sz < 2*s->nobj){
		s->obj = emalloc(s->sz * sizeof(Hash));
		s->has = emalloc(s->sz);
	}
}

int
oshas(Objset *s, Object *o)
{
	u32int probe;

	for(probe = GETBE32(o->hash.h) % s->sz; s->has[probe]; probe++)
		if(hasheq(&s->obj[probe], &o->hash))
			return 1; 
	return 0;
}
