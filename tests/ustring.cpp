#include <cstdio>
#include <kopano/ustringutil.h>

using namespace KC;

static void as(const char *label, int actual, int exp)
{
	if (actual != exp)
		printf("%s: %d, but expected %d\n", label, actual, exp);
}

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
	as("eq", str_equals(aei_nfc, aei_nfd, l_en), 1);

	for (const auto &y : {aei_nfc, aei_nfd}) {
		for (const auto &x : {ae_nfc, ae_nfd, aei_nfc, aei_nfd}) {
			as("prefix", str_startswith(y, x, l_en), 1);
			as("contains", str_contains(y, x, l_en), 1);
		}
		for (const auto &x : {ae_nfc, ae_nfd, aei_nfc, aei_nfd,
		    AE_nfc, AE_nfd, AEI_nfc, AEI_nfd})
			as("icontains", str_icontains(y, x, l_en), 1);
		for (const auto &x : {ae_nfc, ae_nfd, AE_nfc, AE_nfd})
			as("iprefix", str_istartswith(y, x, l_en), 1);
		for (const auto &x : {aei_nfc, aei_nfd, AEI_nfc, AEI_nfd}) {
			as("iequals", str_iequals(y, x, l_en), 1);
			as("icomp-eq", str_icompare(y, x, l_en), 0);
		}
		for (const auto &x : {aai_nfc, aai_nfd, AAI_nfc, AAI_nfd})
			as("icomp-gt", str_icompare(y, x, l_en), 1);
	}

	for (const auto &y : {aai_nfc, aai_nfd}) {
		for (const auto &x : {aei_nfc, aei_nfd, AEI_nfc, AEI_nfd})
			as("icomp de ä<e", str_icompare(y, x, l_de), -1);
		for (const auto &x : {aei_nfc, aei_nfd, AEI_nfc, AEI_nfd})
			as("icomp sv ä>e", str_icompare(aai_nfc, x, l_sv), 1);
	}
	return 0;
}
