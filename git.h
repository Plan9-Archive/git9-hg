#include <bio.h>
#include <ctype.h>
#include <mp.h>
#include <libsec.h>
#include <flate.h>
#include <avl.h>
#include <regexp.h>

typedef struct Hash Hash;
typedef struct Object Object;
typedef struct Pack Pack;
typedef struct Buf Buf;
typedef struct Dirent Dirent;

enum {
	Pathmax=512,
	Hashsz=20,
};

struct Buf {
	int len;
	int sz;
	char *data;
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

struct Object {
	Avl;
	int id;

	/* Git data */
	Hash hash;
	Type type;
	vlong size;
	char *data;
	char *all;

	/* Commit */
	Hash *parent;
	int nparent;
	Hash tree;
	char *author;
	char *committer;
	char *msg;
	int nmsg;
	vlong ctime;
	vlong mtime;

	/* Tree */
	Dirent *ent;
	int nent;
};

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

#define isblank(c) \
	(((c) != '\n') && isspace(c))

extern Reprog *authorpat;

#pragma varargck type "H" Hash
#pragma varargck type "T" Type
#pragma varargck type "O" Object*
int Hfmt(Fmt*);
int Tfmt(Fmt*);
int Ofmt(Fmt*);

void gitinit(void);

/* object io */
Object* readobject(Hash);
void parseobject(Object *);
void freeobject(Object *);

/* util functions */
void *emalloc(ulong);
char *estrdup(char *);
int slurpdir(char *, Dir **);
int hparse(Hash *, char *);
int bdecompress(Buf *, Biobuf *, vlong *);
int decompress(void **, Biobuf *, vlong *);
int hassuffix(char *, char *);
int swapsuffix(char *, int, char *, char *, char *);

