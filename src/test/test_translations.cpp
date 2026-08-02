// Minetest
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "translation.h"
#include "catch.h"

#define CONTEXT L"context"
#define TEXTDOMAIN_PO L"translation_po"
#define TEST_PO_NAME "translation_po.de.po"

// The parser is fed from here rather than from a file of some game, so that
// the test states what it expects instead of pointing at content elsewhere.
static const std::string TEST_PO_CONTENT =
R"(msgid ""
msgstr ""
"Plural-Forms: nplurals=2; plural=n != 1;\n"

msgid "foo"
msgstr "bar"

msgid "Untranslated"
msgstr ""

#, fuzzy
msgid "Fuzzy"
msgstr "Fuzzy result"

msgid "Multi\\line\nstring"
msgstr "Multi\\\"li\\ne\nresult"

msgctxt "context"
msgid "With context"
msgstr "Has context"

msgid "Singular form"
msgid_plural "Plural form"
msgstr[0] "Singular result"
msgstr[1] "Plural result"
)";

TEST_CASE("test translations")
{
	SECTION("File type detection")
	{
		CHECK(Translations::getFileBaseName(TEST_PO_NAME) == "translation_po.de");
		CHECK(Translations::getFileBaseName("de.po") == "de");
		CHECK(Translations::getFileBaseName("blank.png") == "");

		CHECK(Translations::getFileLanguage(TEST_PO_NAME) == "de");
		CHECK(Translations::getFileLanguage("de.po") == "");
		CHECK(Translations::getFileLanguage("blank.png") == "");

		CHECK(Translations::isTranslationFileType(TEST_PO_NAME));
		CHECK(Translations::isTranslationFileType("de.po"));
		CHECK(!Translations::isTranslationFileType("blank.png"));

		CHECK(Translations::isTranslationFile(TEST_PO_NAME));
		CHECK(!Translations::isTranslationFile("de.po"));
		CHECK(!Translations::isTranslationFile("blank.png"));
	}

	SECTION("Plural-Forms function for translations")
	{
#define REQUIRE_FORM_SIZE(x) {REQUIRE(form); REQUIRE(form.size() == (x));}
		// Basic test cases
		auto form = GettextPluralForm(L"Plural-Forms: nplurals=2; plural=1;");
		REQUIRE_FORM_SIZE(2);
		CHECK(form(0) == 1);

		form = GettextPluralForm(L"");
		REQUIRE(form.size() == 0);
		CHECK(form(0) == 0);

		// Test cases from https://www.gnu.org/software/gettext/manual/html_node/Plural-forms.html
		form = GettextPluralForm(L"Plural-Forms: nplurals=2; plural=n != 1;");
		REQUIRE_FORM_SIZE(2);
		CHECK(form(0) == 1);
		CHECK(form(1) == 0);
		CHECK(form(2) == 1);

		form = GettextPluralForm(L"Plural-Forms: nplurals=3; plural=n%10==1 && n%100!=11 ? 0 : n != 0 ? 1 : 2;");
		REQUIRE_FORM_SIZE(3);
		CHECK(form(0) == 2);
		CHECK(form(1) == 0);
		CHECK(form(102) == 1);
		CHECK(form(111) == 1);

		form = GettextPluralForm(L"Plural-Forms: nplurals=3; "
				"plural=n%10==1 && n%100!=11 ? 0 : "
				"n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2;");
		REQUIRE_FORM_SIZE(3);
		CHECK(form(0) == 2);
		CHECK(form(1) == 0);
		CHECK(form(102) == 1);
		CHECK(form(104) == 1);
		CHECK(form(111) == 2);
		CHECK(form(112) == 2);
		CHECK(form(121) == 0);
		CHECK(form(122) == 1);

		// Edge cases
		form = GettextPluralForm(L"Plural-Forms: nplurals=3; plural= (n-1+1)<=1 ? n||1?0:1 : 1?(!!2):2;");
		REQUIRE_FORM_SIZE(3);
		CHECK(form(0) == 0);
		CHECK(form(1) == 0);
		CHECK(form(2) == 1);

		form = GettextPluralForm(L"Plural-Forms: nplurals=2; plural=4/n;");
		REQUIRE_FORM_SIZE(2);
		CHECK(form(1) == 4);
		CHECK(form(0) == 0);

		form = GettextPluralForm(L"Plural-Forms: nplurals=2; plural=7%n;");
		REQUIRE_FORM_SIZE(2);
		CHECK(form(3) == 1);
		CHECK(form(0) == 0);
#undef REQUIRE_FORM_SIZE
	}

	SECTION("PO file parser")
	{
		Translations translations;
		translations.loadTranslation(TEST_PO_NAME, TEST_PO_CONTENT);

		// The textdomain of an entry is the file name up to the first dot,
		// unless the entry states a context of its own
		CHECK(translations.getTranslation(TEXTDOMAIN_PO, L"foo") == L"bar");
		CHECK(translations.getTranslation(CONTEXT, L"With context") == L"Has context");
		CHECK(translations.getTranslation(TEXTDOMAIN_PO, L"With context") == L"With context");

		// An entry without a translation falls back to the original
		CHECK(translations.getTranslation(TEXTDOMAIN_PO, L"Untranslated") == L"Untranslated");
		// So does one that is still marked fuzzy
		CHECK(translations.getTranslation(TEXTDOMAIN_PO, L"Fuzzy") == L"Fuzzy");
		// And one that is not in the file at all
		CHECK(translations.getTranslation(TEXTDOMAIN_PO, L"Absent") == L"Absent");

		// Escape sequences survive the round trip
		CHECK(translations.getTranslation(TEXTDOMAIN_PO, L"Multi\\line\nstring") == L"Multi\\\"li\\ne\nresult");

		// A plural entry is reachable through both of its forms, and the
		// header of the file decides which translation a number selects
		CHECK(translations.getPluralTranslation(TEXTDOMAIN_PO, L"Singular form", 1) == L"Singular result");
		CHECK(translations.getPluralTranslation(TEXTDOMAIN_PO, L"Plural form", 1) == L"Singular result");
		CHECK(translations.getPluralTranslation(TEXTDOMAIN_PO, L"Singular form", 0) == L"Plural result");
		CHECK(translations.getPluralTranslation(TEXTDOMAIN_PO, L"Plural form", 2) == L"Plural result");
	}
}
