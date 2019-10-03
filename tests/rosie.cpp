#include <stdio.h>
#include "HtmlToTextParser.h"

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
	if (size < 0) {
		perror("ftell");
		return EXIT_FAILURE;
	}
	fseek(fh, 0, SEEK_SET);

	char *buffer = new char[size];
	if (fread(buffer, 1, size, fh) != size) {
		perror("incomplete fread");
		return EXIT_FAILURE;
	}
	fclose(fh);

	std::string html_in(buffer, size), html_out;
	delete[] buffer;

	std::vector<std::string> errors;
	fprintf(stderr, "result code: %d\n", KC::rosie_clean_html(html_in, &html_out, &errors));
	for (unsigned int i=0; i<errors.size(); i++)
		fprintf(stderr, "\t%s\n", errors.at(i).c_str());
	printf("%s", html_out.c_str());
	return 0;
}
