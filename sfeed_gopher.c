#include <sys/types.h>

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

static struct feed f;
static char *prefixpath = "/", *host = "127.0.0.1", *port = "70"; /* default */
static char *line;
static size_t linesize;
static time_t comparetime;

/* Escape characters in gopher, CR and LF are ignored */
void
gophertext(FILE *fp, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '\r': /* ignore CR */
		case '\n': /* ignore LF */
			break;
		case '\t':
			fputs("        ", fp);
			break;
		default:
			fputc(*s, fp);
			break;
		}
	}
}

static void
printfeed(FILE *fpitems, FILE *fpin, struct feed *f)
{
	char *fields[FieldLast];
	ssize_t linelen;
	unsigned int isnew;
	struct tm *tm;
	time_t parsedtime;

	if (f->name[0]) {
		fprintf(fpitems, "i%s\t\t%s\t%s\r\n", f->name, host, port);
		fprintf(fpitems, "i\t\t%s\t%s\r\n", host, port);
	}

	while ((linelen = getline(&line, &linesize, fpin)) > 0) {
		if (line[linelen - 1] == '\n')
			line[--linelen] = '\0';
		if (!parseline(line, fields))
			break;

		parsedtime = 0;
		if (strtotime(fields[FieldUnixTimestamp], &parsedtime))
			continue;
		if (!(tm = localtime(&parsedtime)))
			err(1, "localtime");

		isnew = (parsedtime >= comparetime) ? 1 : 0;
		f->totalnew += isnew;
		f->total++;

		if (fields[FieldLink][0]) {
			fputs("h", fpitems);
			fprintf(fpitems, "%c %04d-%02d-%02d %02d:%02d ",
			        isnew ? 'N' : ' ',
			        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			        tm->tm_hour, tm->tm_min);
			gophertext(fpitems, fields[FieldTitle]);
			fputs("\tURL:", fpitems);
			gophertext(fpitems, fields[FieldLink]);

		} else {
			fprintf(fpitems, "i%c %04d-%02d-%02d %02d:%02d ",
			        isnew ? 'N' : ' ',
			        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			        tm->tm_hour, tm->tm_min);
			gophertext(fpitems, fields[FieldTitle]);
			fputs("\t", fpitems);
		}
		fprintf(fpitems, "\t%s\t%s\r\n", host, port);
	}
	fputs(".\r\n", fpitems);
}

int
main(int argc, char *argv[])
{
	FILE *fpitems, *fpindex, *fp;
	char *name, *p, path[PATH_MAX + 1];
	int i, r;

	if (pledge(argc == 1 ? "stdio" : "stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

	if ((comparetime = time(NULL)) == -1)
		err(1, "time");
	/* 1 day is old news */
	comparetime -= 86400;

	if ((p = getenv("SFEED_GOPHER_HOST")))
		host = p;
	if ((p = getenv("SFEED_GOPHER_PORT")))
		port = p;

	if (argc == 1) {
		f.name = "";
		printfeed(stdout, stdin, &f);
	} else {
		if ((p = getenv("SFEED_GOPHER_PATH")))
			prefixpath = p;

		/* write main index page */
		if (!(fpindex = fopen("index", "wb")))
			err(1, "fopen: index");

		for (i = 1; i < argc; i++) {
			memset(&f, 0, sizeof(f));
			name = ((name = strrchr(argv[i], '/'))) ? name + 1 : argv[i];
			f.name = name;

			if (!(fp = fopen(argv[i], "r")))
				err(1, "fopen: %s", argv[i]);

			r = snprintf(path, sizeof(path), "%s", name);
			if (r < 0 || (size_t)r >= sizeof(path))
				errx(1, "path truncation: %s", path);
			if (!(fpitems = fopen(path, "wb")))
				err(1, "fopen");
			printfeed(fpitems, fp, &f);
			if (ferror(fp))
				err(1, "ferror: %s", argv[i]);
			fclose(fp);
			fclose(fpitems);

			/* append directory item to index */
			fputs("1", fpindex);
			gophertext(fpindex, name);
			fprintf(fpindex, " (%lu/%lu)\t", f.totalnew, f.total);
			gophertext(fpindex, prefixpath);
			gophertext(fpindex, path);
			fprintf(fpindex, "\t%s\t%s\r\n", host, port);
		}
		fputs(".\r\n", fpindex);
		fclose(fpindex);
	}

	return 0;
}
