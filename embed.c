#include <stdio.h>
#define PER_LINE 16
int main(int argc, char**argv) {
    FILE *in = fdopen(0, "rb"), *out=stdout;
    int i;

    fprintf(out, "/* AUTOMATICALLY GENERATED.  DO NOT EDIT. */\n");
    fprintf(out, "unsigned char %s[] = {\n", argc>1 ? argv[1] : "file");
    for (i=0; !feof(in); i++) {
	unsigned char c;
	if (0==fread(&c, sizeof(c), 1, in)) continue;
	fprintf(out, "%s%3d",(i==0)?"  ":(0==(i%PER_LINE))?",\n  ":",",
		(unsigned)c);
    }
    fprintf(out, "\n};\n");
    fclose(in); fclose(out);
}
