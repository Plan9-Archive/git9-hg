#include <bio.h>
#include <mp.h>
#include <libsec.h>
#include <ctype.h>
#include <flate.h>
#include <avl.h>
#include <regexp.h>

typedef struct Hash Hash;
typedef struct Object Object;
typedef struct Objset Objset;
typedef struct Pack Pack;
typedef struct Buf Buf;
typedef struct Dirent Dirent;
typedef struct Idxent Idxent;

enum {
	Pathmax=512,
	Hashsz=20,
};

typedef enum Type {
	GNone	= 0,
	GCommit	= 1,
	GTree	= 2,
	GBlob	= 3,
	GTag	= 4,
	GOdelta	= 6,
	GRdelta	= 7,
} Type;

struct Hash {
	uchar h[20];
};

struct Dirent {
	char *name;
	int mode;
	Hash h;
};

struct Idxent {
	Dir;
	Hash h;
};

struct Object {
	/* Cache */
	Avl;
	int	id;

	/* Git data */
	Hash	hash;
	Type	type;
	vlong	size;	/* excluding header */
	char	*data;
	char	*all;
	int	parsed;

	/* For indexing */
	vlong	off;

	/* Commit */
	Hash	*parent;
	int	nparent;
	Hash	tree;
	char	*author;
	char	*committer;
	char	*msg;
	int	nmsg;
	vlong	ctime;
	vlong	mtime;

	/* Tree */
	Dirent	*ent;
	int	nent;
};

struct Objset {
	Object	**obj;
	int	nobj;
	int	sz;
};

#define GETBE16(b)\
		((((b)[0] & 0xFFul) <<  8) | \
		 (((b)[1] & 0xFFul) <<  0))

#define GETBE32(b)\
		((((b)[0] & 0xFFul) << 24) | \
		 (((b)[1] & 0xFFul) << 16) | \
		 (((b)[2] & 0xFFul) <<  8) | \
		 (((b)[3] & 0xFFul) <<  0))
#define GETBE64(b)\
		((((b)[0] & 0xFFull) << 56) | \
		 (((b)[1] & 0xFFull) << 48) | \
		 (((b)[2] & 0xFFull) << 40) | \
		 (((b)[3] & 0xFFull) << 32) | \
		 (((b)[4] & 0xFFull) << 24) | \
		 (((b)[5] & 0xFFull) << 16) | \
		 (((b)[6] & 0xFFull) <<  8) | \
		 (((b)[7] & 0xFFull) <<  0))

#define PUTBE16(b, n)\
	do{ \
		(b)[0] = (n) >> 8; \
		(b)[1] = (n) >> 0; \
	} while(0)

#define PUTBE32(b, n)\
	do{ \
		(b)[0] = (n) >> 24; \
		(b)[1] = (n) >> 16; \
		(b)[2] = (n) >> 8; \
		(b)[3] = (n) >> 0; \
	} while(0)

#define PUTBE64(b, n)\
	do{ \
		(b)[0] = (n) >> 56; \
		(b)[1] = (n) >> 48; \
		(b)[2] = (n) >> 40; \
		(b)[3] = (n) >> 32; \
		(b)[4] = (n) >> 24; \
		(b)[5] = (n) >> 16; \
		(b)[6] = (n) >> 8; \
		(b)[7] = (n) >> 0; \
	} while(0)

#define QDIR(qid)	((int)(qid)->path & (0xff))
#define QPATH(id, dt)	(((uvlong)(id) << 8) | ((dt) & 0x7f))
#define isblank(c) \
	(((c) != '\n') && isspace(c))

extern Reprog *authorpat;
extern Avltree *objcache;
extern Hash Zhash;

#pragma varargck type "H" Hash
#pragma varargck type "T" Type
#pragma varargck type "O" Object*
#pragma varargck type "Q" Qid
int Hfmt(Fmt*);
int Tfmt(Fmt*);
int Ofmt(Fmt*);
int Qfmt(Fmt*);

void gitinit(void);

/* object io */
Object* readobject(Hash);
void parseobject(Object *);
int indexpack(char *, char *, Hash);
int hasheq(Hash *, Hash *);

/* index */
long idxread(char *, char *, Idxent **);
int idxwrite(int, Idxent *, long);

/* object sets */
void osinit(Objset *);
void osadd(Objset *, Object *);
int oshas(Objset *, Object *);

/* util functions */
void *emalloc(ulong);
void *erealloc(void *, ulong);
char *estrdup(char *);
int slurpdir(char *, Dir **);
int hparse(Hash *, char *);
int hassuffix(char *, char *);
int swapsuffix(char *, int, char *, char *, char *);
void die(char *, ...);

