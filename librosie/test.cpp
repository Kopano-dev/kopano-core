#include <stdio.h>
#include "librosie.h"

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Require filename\n");
		return 1;
	}

	FILE *fh = fopen(argv[1], "r");
	if (fh == NULL) {
		fprintf(stderr, "Cannot open file\n");
		return 1;
	}

	fseek(fh, 0, SEEK_END);
	long int size = ftell(fh);
	fseek(fh, 0, SEEK_SET);

	char *buffer = new char[size];
	fread(buffer, 1, size, fh);
	fclose(fh);

	std::string html_in = std::string(buffer, size), html_out;
	delete[] buffer;

	std::vector<std::string> errors;
	fprintf(stderr, "result code: %d\n", rosie_clean_html(html_in, &html_out, &errors));
	for (unsigned int i=0; i<errors.size(); i++)
		fprintf(stderr, "\t%s\n", errors.at(i).c_str());
	printf("%s", html_out.c_str());
	return 0;
}
