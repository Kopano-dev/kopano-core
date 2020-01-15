#include <cstdio>
#include <kopano/ustringutil.h>

using namespace KC;

int main()
{
	/* make StringToUnicode do the right thing with our source strings */
	setlocale(LC_ALL, "en_US.UTF-8");

	auto l_en = createLocaleFromName("en");
	auto l_de = createLocaleFromName("de");
	auto l_sv = createLocaleFromName("sv");
	static const char ae_nfc[] = "a\xc3\xab", AE_nfc[] = "A\xc3\x8b";
	static const char ae_nfd[] = "ae\xcc\x88", AE_nfd[] = "AE\xcc\x88";
	static const char aai_nfc[] = "a\xc3\xa4i", AAI_nfc[] = "A\xc3\x84I";
	static const char aai_nfd[] = "aa\xcc\x88i", AAI_nfd[] = "AA\xcc\x88I";
	static const char aei_nfc[] = "a\xc3\xabi", AEI_nfc[] = "A\xc3\x8bI";
	static const char aei_nfd[] = "ae\xcc\x88i", AEI_nfd[] = "AE\xcc\x88I";
	printf("eq: %d, exp 1\n", str_equals(aei_nfc, aei_nfd, l_en));

	for (const auto &y : {aei_nfc, aei_nfd}) {
		for (const auto &x : {ae_nfc, ae_nfd, aei_nfc, aei_nfd}) {
			printf("prefix: %d, exp 1\n", str_startswith(y, x, l_en));
			printf("contains: %d, exp 1\n", str_contains(y, x, l_en));
		}
		for (const auto &x : {ae_nfc, ae_nfd, aei_nfc, aei_nfd,
		    AE_nfc, AE_nfd, AEI_nfc, AEI_nfd})
			printf("icontains: %d, exp 1\n", str_icontains(y, x, l_en));
		for (const auto &x : {ae_nfc, ae_nfd, AE_nfc, AE_nfd})
			printf("iprefix: %d, exp 1\n", str_istartswith(y, x, l_en));
		for (const auto &x : {aei_nfc, aei_nfd, AEI_nfc, AEI_nfd}) {
			printf("iequals: %d, exp 1\n", str_iequals(y, x, l_en));
			printf("icomp-eq: %d, exp 0\n", str_icompare(y, x, l_en));
		}
		for (const auto &x : {aai_nfc, aai_nfd, AAI_nfc, AAI_nfd})
			printf("icomp-gt: %d, exp 1\n", str_icompare(y, x, l_en));
	}

	for (const auto &y : {aai_nfc, aai_nfd}) {
		for (const auto &x : {aei_nfc, aei_nfd, AEI_nfc, AEI_nfd})
			printf("icomp de ä<e: %d, expected -1\n", str_icompare(y, x, l_de));
		for (const auto &x : {aei_nfc, aei_nfd, AEI_nfc, AEI_nfd})
			printf("icomp sv ä>e: %d, expected 1\n", str_icompare(aai_nfc, x, l_sv));
	}
	return 0;
}
