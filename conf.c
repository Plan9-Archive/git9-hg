#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>

static char *
strip(char *p)
{
	for(; *p == '\t' || *p == ' '; p++)
		/* nothing */;
	return p;
}

static void
showconf(char *cfg, char *sect, char *key)
{
	char *ln, *p;
	Biobuf *f;
	int foundsect, nsect, nkey;

	if((f = Bopen(cfg, OREAD)) == nil)
		sysfatal("could not open %s: %r", cfg);

	nsect = sect ? strlen(sect) : 0;
	nkey = strlen(key);
	foundsect = (sect == nil);
	print("sect:%s\nkey=%s\n", sect, key);
	while((ln = Brdstr(f, '\n', 1)) != nil){
		p = strip(ln);
		if(*p == '[' && sect){
			foundsect = strncmp(sect, ln, nsect) == 0;
		}else if(foundsect && strncmp(p, key, nkey) == 0){
			p = strip(p + nkey);
			if(*p != '=')
				continue;
			p = strip(p + 1);
			print("%s\n", p);
			free(ln);
			return;
		}
		free(ln);
	}
}

void
usage(void)
{
	print("usage: %s [-f file]\n", argv0);
	print("\t-f:	use file 'file' (default: .gitignore)\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *file, *p, sect[128];
	int i;

	file = ".git/config";
	ARGBEGIN{
	case 'f':	file=EARGF(usage());
	}ARGEND;

	for(i = 0; i < argc; i++){
		if((p = strchr(argv[i], '.')) == nil)
			showconf(file, nil, argv[i]);
		else{
			*p = 0;
			p++;
			snprint(sect, sizeof(sect), "[%s]", argv[i]);
			showconf(file, sect, p);
		}
	}
}
