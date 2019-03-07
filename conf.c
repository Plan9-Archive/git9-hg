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

static int
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
			return 1;
		}
		free(ln);
	}
	return 0;
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
	char *file[32], *p, sect[128];
	int i, j, nfile;

	nfile = 0;
	ARGBEGIN{
	case 'f':	file[nfile++]=EARGF(usage());
	}ARGEND;
	if(nfile == 0){
		file[nfile++] = ".git/config";
		if((p = getenv("home")) != nil)
			file[nfile++] = smprint("%s/lib/git/config", p);
	}

	for(i = 0; i < argc; i++){
		for(j = 0; j < nfile; j++){
			if((p = strchr(argv[i], '.')) == nil){
				if(showconf(file[j], nil, argv[i]))
					break;
			}else{
				*p = 0;
				p++;
				snprint(sect, sizeof(sect), "[%s]", argv[i]);
				if(showconf(file[j], sect, p))
					break;
			}
		}
	}
}
