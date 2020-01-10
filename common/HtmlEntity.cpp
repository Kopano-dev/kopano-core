/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <string>
#include "HtmlEntity.h"
#include <kopano/charset/convert.h>

using namespace std::string_literals;

namespace KC {

static const struct HTMLEntity_t {
	const wchar_t *s;
	wchar_t c;
} HTMLEntity[] = {
	{L"AElig", 198},
	{L"Aacute", 193},
	{L"Acirc", 194},
	{L"Agrave", 192},
	{L"Alpha", 913},
	{L"Aring", 197},
	{L"Atilde", 195},
	{L"Auml", 196},
	{L"Beta", 914},
	{L"Ccedil", 199},
	{L"Chi", 935},
	{L"Dagger", 8225},
	{L"Delta", 916},
	{L"ETH", 208},
	{L"Eacute", 201},
	{L"Ecirc", 202},
	{L"Egrave", 200},
	{L"Epsilon", 917},
	{L"Eta", 919},
	{L"Euml", 203},
	{L"Gamma", 915},
	{L"Iacute", 205},
	{L"Icirc", 206},
	{L"Igrave", 204},
	{L"Iota", 921},
	{L"Iuml", 207},
	{L"Kappa", 922},
	{L"Lambda", 923},
	{L"Mu", 924},
	{L"Ntilde", 209},
	{L"Nu", 925},
	{L"OElig", 338},
	{L"Oacute", 211},
	{L"Ocirc", 212},
	{L"Ograve", 210},
	{L"Omega", 937},
	{L"Omicron", 927},
	{L"Oslash", 216},
	{L"Otilde", 213},
	{L"Ouml", 214},
	{L"Phi", 934},
	{L"Pi", 928},
	{L"Prime", 8243},
	{L"Psi", 936},
	{L"Rho", 929},
	{L"Scaron", 352},
	{L"Sigma", 931},
	{L"THORN", 222},
	{L"Tau", 932},
	{L"Theta", 920},
	{L"Uacute", 218},
	{L"Ucirc", 219},
	{L"Ugrave", 217},
	{L"Upsilon", 933},
	{L"Uuml", 220},
	{L"Xi", 926},
	{L"Yacute", 221},
	{L"Yuml", 376},
	{L"Zeta", 918},
	{L"aacute", 225},
	{L"acirc", 226},
	{L"acute", 180},
	{L"aelig", 230},
	{L"agrave", 224},
	{L"alpha", 945},
	{L"amp", 38},
	{L"and", 8743},
	{L"ang", 8736},
	{L"aring", 229},
	{L"asymp", 8776},
	{L"atilde", 227},
	{L"auml", 228},
	{L"bdquo", 8222},
	{L"beta", 946},
	{L"brvbar", 166},
	{L"bull", 8226},
	{L"cap", 8745},
	{L"ccedil", 231},
	{L"cedil", 184},
	{L"cent", 162},
	{L"chi", 967},
	{L"chi", 967},
	{L"circ", 710},
	{L"clubs", 9827},
	{L"cong", 8773},
	{L"copy", 169},
	{L"crarr", 8629},
	{L"cup", 8746},
	{L"curren", 164},
	{L"dagger", 8224},
	{L"darr", 8595},
	{L"deg", 176},
	{L"delta", 948},
	{L"diams", 9830},
	{L"divide", 247},
	{L"eacute", 233},
	{L"ecirc", 234},
	{L"egrave", 232},
	{L"empty", 8709},
	{L"emsp", 8195},
	{L"ensp", 8194},
	{L"epsilon", 949},
	{L"equiv", 8801},
	{L"eta", 951},
	{L"eth", 240},
	{L"euml", 235},
	{L"euro", 8364},
	{L"exist", 8707},
	{L"fnof", 402},
	{L"forall", 8704},
	{L"frac12", 189},
	{L"frac14", 188},
	{L"frac34", 190},
	{L"gamma", 947},
	{L"ge", 8805},
	{L"gt", 62},
	{L"harr", 8596},
	{L"hearts", 9829},
	{L"hellip", 8230},
	{L"iacute", 237},
	{L"icirc", 238},
	{L"iexcl", 161},
	{L"igrave", 236},
	{L"infin", 8734},
	{L"int", 8747},
	{L"iota", 953},
	{L"iquest", 191},
	{L"isin", 8712},
	{L"iuml", 239},
	{L"kappa", 954},
	{L"lambda", 955},
	{L"laquo", 171},
	{L"larr", 8592},
	{L"lceil", 8968},
	{L"ldquo", 8220},
	{L"le", 8804},
	{L"lfloor", 8970},
	{L"lowast", 8727},
	{L"loz", 9674},
	{L"lrm", 8206},
	{L"lsaquo", 8249},
	{L"lsquo", 8216},
	{L"lt", 60},
	{L"macr", 175},
	{L"mdash", 8212},
	{L"micro", 181},
	{L"middot", 183},
	{L"minus", 8722},
	{L"mu", 956},
	{L"nabla", 8711},
	{L"nbsp", 160},
	{L"ndash", 8211},
	{L"ne", 8800},
	{L"ni", 8715},
	{L"not", 172},
	{L"notin", 8713},
	{L"nsub", 8836},
	{L"ntilde", 241},
	{L"nu", 957},
	{L"oacute", 243},
	{L"ocirc", 244},
	{L"oelig", 339},
	{L"ograve", 242},
	{L"oline", 8254},
	{L"omega", 969},
	{L"omicron", 959},
	{L"oplus", 8853},
	{L"or", 8744},
	{L"ordf", 170},
	{L"ordm", 186},
	{L"oslash", 248},
	{L"otilde", 245},
	{L"otimes", 8855},
	{L"ouml", 246},
	{L"para", 182},
	{L"part", 8706},
	{L"permil", 8240},
	{L"perp", 8869},
	{L"phi", 966},
	{L"pi", 960},
	{L"piv", 982},
	{L"plusmn", 177},
	{L"pound", 163},
	{L"prime", 8242},
	{L"prod", 8719},
	{L"prop", 8733},
	{L"psi", 968},
	{L"quot", 34},
	{L"radic", 8730},
	{L"raquo", 187},
	{L"rarr", 8594},
	{L"rceil", 8969},
	{L"rdquo", 8221},
	{L"reg", 174},
	{L"rfloor", 8971},
	{L"rho", 961},
	{L"rlm", 8207},
	{L"rsaquo", 8250},
	{L"rsquo", 8217},
	{L"sbquo", 8218},
	{L"scaron", 353},
	{L"sdot", 8901},
	{L"sect", 167},
	{L"shy", 173},
	{L"sigma", 963},
	{L"sigmaf", 962},
	{L"sim", 8764},
	{L"spades", 9824},
	{L"sub", 8834},
	{L"sube", 8838},
	{L"sum", 8721},
	{L"sup", 8835},
	{L"sup1", 185},
	{L"sup2", 178},
	{L"sup3", 179},
	{L"supe", 8839},
	{L"szlig", 223},
	{L"tau", 964},
	{L"there4", 8756},
	{L"theta", 952},
	{L"thetasym", 977},
	{L"thinsp", 8201},
	{L"thorn", 254},
	{L"tilde", 732},
	{L"times", 215},
	{L"trade", 8482},
	{L"uacute", 250},
	{L"uarr", 8593},
	{L"ucirc", 251},
	{L"ugrave", 249},
	{L"uml", 168},
	{L"upsih", 978},
	{L"upsilon", 965},
	{L"uuml", 252},
	{L"xi", 958},
	{L"yacute", 253},
	{L"yen", 165},
	{L"yuml", 255},
	{L"zeta", 950},
	{L"zwj", 8205},
	{L"zwnj", 8204}
};

static const size_t cHTMLEntity = ARRAY_SIZE(HTMLEntity);

static const struct HTMLEntityToName_t {
	wchar_t c;
	const wchar_t *s;
} HTMLEntityToName[] = {
	{34, L"quot"},
	{38, L"amp"},
	{60, L"lt"},
	{62, L"gt"},
	{160, L"nbsp"},
	{161, L"iexcl"},
	{162, L"cent"},
	{163, L"pound"},
	{164, L"curren"},
	{165, L"yen"},
	{166, L"brvbar"},
	{167, L"sect"},
	{168, L"uml"},
	{169, L"copy"},
	{170, L"ordf"},
	{171, L"laquo"},
	{172, L"not"},
	{173, L"shy"},
	{174, L"reg"},
	{175, L"macr"},
	{176, L"deg"},
	{177, L"plusmn"},
	{178, L"sup2"},
	{179, L"sup3"},
	{180, L"acute"},
	{181, L"micro"},
	{182, L"para"},
	{183, L"middot"},
	{184, L"cedil"},
	{185, L"sup1"},
	{186, L"ordm"},
	{187, L"raquo"},
	{188, L"frac14"},
	{189, L"frac12"},
	{190, L"frac34"},
	{191, L"iquest"},
	{192, L"Agrave"},
	{193, L"Aacute"},
	{194, L"Acirc"},
	{195, L"Atilde"},
	{196, L"Auml"},
	{197, L"Aring"},
	{198, L"AElig"},
	{199, L"Ccedil"},
	{200, L"Egrave"},
	{201, L"Eacute"},
	{202, L"Ecirc"},
	{203, L"Euml"},
	{204, L"Igrave"},
	{205, L"Iacute"},
	{206, L"Icirc"},
	{207, L"Iuml"},
	{208, L"ETH"},
	{209, L"Ntilde"},
	{210, L"Ograve"},
	{211, L"Oacute"},
	{212, L"Ocirc"},
	{213, L"Otilde"},
	{214, L"Ouml"},
	{215, L"times"},
	{216, L"Oslash"},
	{217, L"Ugrave"},
	{218, L"Uacute"},
	{219, L"Ucirc"},
	{220, L"Uuml"},
	{221, L"Yacute"},
	{222, L"THORN"},
	{223, L"szlig"},
	{224, L"agrave"},
	{225, L"aacute"},
	{226, L"acirc"},
	{227, L"atilde"},
	{228, L"auml"},
	{229, L"aring"},
	{230, L"aelig"},
	{231, L"ccedil"},
	{232, L"egrave"},
	{233, L"eacute"},
	{234, L"ecirc"},
	{235, L"euml"},
	{236, L"igrave"},
	{237, L"iacute"},
	{238, L"icirc"},
	{239, L"iuml"},
	{240, L"eth"},
	{241, L"ntilde"},
	{242, L"ograve"},
	{243, L"oacute"},
	{244, L"ocirc"},
	{245, L"otilde"},
	{246, L"ouml"},
	{247, L"divide"},
	{248, L"oslash"},
	{249, L"ugrave"},
	{250, L"uacute"},
	{251, L"ucirc"},
	{252, L"uuml"},
	{253, L"yacute"},
	{254, L"thorn"},
	{255, L"yuml"},
	{338, L"OElig"},
	{339, L"oelig"},
	{352, L"Scaron"},
	{353, L"scaron"},
	{376, L"Yuml"},
	{402, L"fnof"},
	{710, L"circ"},
	{732, L"tilde"},
	{913, L"Alpha"},
	{914, L"Beta"},
	{915, L"Gamma"},
	{916, L"Delta"},
	{917, L"Epsilon"},
	{918, L"Zeta"},
	{919, L"Eta"},
	{920, L"Theta"},
	{921, L"Iota"},
	{922, L"Kappa"},
	{923, L"Lambda"},
	{924, L"Mu"},
	{925, L"Nu"},
	{926, L"Xi"},
	{927, L"Omicron"},
	{928, L"Pi"},
	{929, L"Rho"},
	{931, L"Sigma"},
	{932, L"Tau"},
	{933, L"Upsilon"},
	{934, L"Phi"},
	{935, L"Chi"},
	{936, L"Psi"},
	{937, L"Omega"},
	{945, L"alpha"},
	{946, L"beta"},
	{947, L"gamma"},
	{948, L"delta"},
	{949, L"epsilon"},
	{950, L"zeta"},
	{951, L"eta"},
	{952, L"theta"},
	{953, L"iota"},
	{954, L"kappa"},
	{955, L"lambda"},
	{956, L"mu"},
	{957, L"nu"},
	{958, L"xi"},
	{959, L"omicron"},
	{960, L"pi"},
	{961, L"rho"},
	{962, L"sigmaf"},
	{963, L"sigma"},
	{964, L"tau"},
	{965, L"upsilon"},
	{966, L"phi"},
	{967, L"chi"},
	{967, L"chi"},
	{968, L"psi"},
	{969, L"omega"},
	{977, L"thetasym"},
	{978, L"upsih"},
	{982, L"piv"},
	{8194, L"ensp"},
	{8195, L"emsp"},
	{8201, L"thinsp"},
	{8204, L"zwnj"},
	{8205, L"zwj"},
	{8206, L"lrm"},
	{8207, L"rlm"},
	{8211, L"ndash"},
	{8212, L"mdash"},
	{8216, L"lsquo"},
	{8217, L"rsquo"},
	{8218, L"sbquo"},
	{8220, L"ldquo"},
	{8221, L"rdquo"},
	{8222, L"bdquo"},
	{8224, L"dagger"},
	{8225, L"Dagger"},
	{8226, L"bull"},
	{8230, L"hellip"},
	{8240, L"permil"},
	{8242, L"prime"},
	{8243, L"Prime"},
	{8249, L"lsaquo"},
	{8250, L"rsaquo"},
	{8254, L"oline"},
	{8364, L"euro"},
	{8482, L"trade"},
	{8592, L"larr"},
	{8593, L"uarr"},
	{8594, L"rarr"},
	{8595, L"darr"},
	{8596, L"harr"},
	{8629, L"crarr"},
	{8704, L"forall"},
	{8706, L"part"},
	{8707, L"exist"},
	{8709, L"empty"},
	{8711, L"nabla"},
	{8712, L"isin"},
	{8713, L"notin"},
	{8715, L"ni"},
	{8719, L"prod"},
	{8721, L"sum"},
	{8722, L"minus"},
	{8727, L"lowast"},
	{8730, L"radic"},
	{8733, L"prop"},
	{8734, L"infin"},
	{8736, L"ang"},
	{8743, L"and"},
	{8744, L"or"},
	{8745, L"cap"},
	{8746, L"cup"},
	{8747, L"int"},
	{8756, L"there4"},
	{8764, L"sim"},
	{8773, L"cong"},
	{8776, L"asymp"},
	{8800, L"ne"},
	{8801, L"equiv"},
	{8804, L"le"},
	{8805, L"ge"},
	{8834, L"sub"},
	{8835, L"sup"},
	{8836, L"nsub"},
	{8838, L"sube"},
	{8839, L"supe"},
	{8853, L"oplus"},
	{8855, L"otimes"},
	{8869, L"perp"},
	{8901, L"sdot"},
	{8968, L"lceil"},
	{8969, L"rceil"},
	{8970, L"lfloor"},
	{8971, L"rfloor"},
	{9674, L"loz"},
	{9824, L"spades"},
	{9827, L"clubs"},
	{9829, L"hearts"},
	{9830, L"diams"}
};
static const size_t cHTMLEntityToName = ARRAY_SIZE(HTMLEntityToName);

static int compareHTMLEntityToChar(const void *m1, const void *m2)
{
	auto e1 = static_cast<const HTMLEntity_t *>(m1);
	auto e2 = static_cast<const HTMLEntity_t *>(m2);
	return wcscmp( e1->s, e2->s );
}

static int compareHTMLEntityToName(const void *m1, const void *m2)
{
	auto e1 = static_cast<const HTMLEntityToName_t *>(m1);
	auto e2 = static_cast<const HTMLEntityToName_t *>(m2);
	return (e1->c < e2->c) ? -1 : (e1->c == e2->c) ? 0 : 1;
}

wchar_t CHtmlEntity::toChar(const wchar_t *name)
{
	HTMLEntity_t key{};
	key.s = name;
	auto result = static_cast<HTMLEntity_t *>(bsearch(&key, &HTMLEntity, cHTMLEntity, sizeof(HTMLEntity_t), compareHTMLEntityToChar));
	return result != nullptr ? result->c : 0;
}

const wchar_t *CHtmlEntity::toName(wchar_t c)
{
	HTMLEntityToName_t key{};
	key.c = c;
	auto result = static_cast<HTMLEntityToName_t *>(bsearch(&key, &HTMLEntityToName, cHTMLEntityToName, sizeof(HTMLEntityToName_t), compareHTMLEntityToName));
	return result != nullptr ? result->s : nullptr;
}

/**
 * Convert a character to HTML entity. when no entity is needed, false
 * is returned. Output parameter will always contain correct
 * representation of input.
 *
 * @param[in] c wide character to convert into HTML entity
 * @param[out] strHTML HTML version of input
 *
 * @return false if no conversion took place, true if it did.
 */
bool CHtmlEntity::CharToHtmlEntity(wchar_t c, std::wstring &strHTML)
{
	bool bHTML = false;

	switch(c) {
	case '\r':
		bHTML = true;		// but no output
		break;
	case '\n':
		strHTML = L"<br>\n";
		bHTML = true;
		break;
	case '\t':
		strHTML = L"&nbsp;&nbsp;&nbsp; ";
		bHTML = true;
		break;
	case ' ':
		strHTML = L"&nbsp;";
		bHTML = true;
		break;
	default:
		auto lpChar = CHtmlEntity::toName(c);
		if (lpChar == nullptr)
			break;
		strHTML = L"&"s + lpChar + L";";
		bHTML = true;
		break;
	}
	if (!bHTML)
		strHTML = c;
	return bHTML;
}

/**
 * Validate HTML entity
 *
 * Valid:
 *   &{#100 | #x64 | amp};test
 *
 * @param[in] strEntity a string part to test if this is a HTML entity, which could be a single wide character
 *
 * @return true if input is HTML, false if it is a normal string
 */
bool CHtmlEntity::validateHtmlEntity(const std::wstring &strEntity)
{
	if(strEntity.size() < 3 || strEntity[0] != '&')
		return false;

	size_t pos = strEntity.find(';');
	if (pos == std::wstring::npos || pos < 3)
		return false;
	if (strEntity[1] == '#') {
		auto str = strEntity.substr(2, pos - 2);
		auto base = (str[0] == 'x') ? 16 : 10;
		return wcstoul(str.c_str() + 1, NULL, base) != 0;
	}
	auto str = strEntity.substr(1, pos - 2);
	return CHtmlEntity::toChar(str.c_str()) > 0;
}

/**
 * Convert HTML entity to a single wide character.
 *
 * @param[in] strEntity valid HTML entity to convert
 *
 * @return wide character for entity, or ? if conversion failed.
 */
wchar_t CHtmlEntity::HtmlEntityToChar(const std::wstring &strEntity)
{
	if (strEntity[0] != '#') {
		unsigned int ulCode = toChar(strEntity.c_str());
		return (ulCode > 0) ? ulCode : L'?';
	}
	// We have a unicode number, use iconv to get the WCHAR
	std::string strUnicode;
	int base = 10;
	auto pNum = strEntity.c_str() + 1;
	if (strEntity.size() > 2 && strEntity[1] == 'x') {
		base = 16;
		++pNum;
	}

	unsigned int ulCode = wcstoul(pNum, nullptr, base);
	if (ulCode <= 0xFFFF /*USHRT_MAX*/)
		return ulCode;
	strUnicode.append(1, (ulCode & 0xff));
	strUnicode.append(1, (ulCode >> 8) & 0xff);
	strUnicode.append(1, (ulCode >> 16) & 0xff);
	strUnicode.append(1, (ulCode >> 24) & 0xff);
	try {
		return convert_to<std::wstring>(CHARSET_WCHAR, strUnicode, 4, "UCS-4LE")[0];
	} catch (const illegal_sequence_exception &) {
		// iconv doesn't seem to like certain sequences. one of them is 0x92000000 (LE).
		return L'?';
	}
	return '?';
}

} /* namespace */
