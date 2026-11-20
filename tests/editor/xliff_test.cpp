// XLIFF: the translations table for one language out to translation tools, and what they
// send back read into its column.

#include <doctest/doctest.h>

#include <string>

#include "editor/core/table_edit.h"
#include "editor/core/xliff.h"

namespace game = engine::game;

namespace {

/// A translations table: two texts, one already in French, one with a note.
game::Table translations() {
    game::Table table;
    for (const char* id : {"source", "notes", "fr"}) {
        game::TableColumn column;
        column.id = id;
        column.kind = game::ColumnKind::Text;
        table.columns.push_back(column);
    }
    game::TableRow hello;
    hello.id = "t_hello";
    hello.cells["source"] = "Hello, <traveller> & friend!";
    hello.cells["fr"] = "Bonjour, voyageur !";
    game::TableRow bye;
    bye.id = "t_bye";
    bye.cells["source"] = "See you \"soon\".";
    bye.cells["notes"] = "Said at the door";
    table.rows = {hello, bye};
    return table;
}

} // namespace

TEST_CASE("XLIFF out: a unit for each text, escaped, with its translation and its note") {
    const std::string xliff = editor::translations_to_xliff(translations(), "fr");
    CHECK(xliff.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
    CHECK(xliff.find("<xliff version=\"1.2\"") != std::string::npos);
    CHECK(xliff.find("source-language=\"en\" target-language=\"fr\"") != std::string::npos);
    CHECK(xliff.find("<trans-unit id=\"t_hello\"") != std::string::npos);
    CHECK(xliff.find("<source>Hello, &lt;traveller&gt; &amp; friend!</source>") !=
          std::string::npos);
    CHECK(xliff.find("<target state=\"translated\">Bonjour, voyageur !</target>") !=
          std::string::npos);
    CHECK(xliff.find("<source>See you &quot;soon&quot;.</source>") != std::string::npos);
    CHECK(xliff.find("<note>Said at the door</note>") != std::string::npos);
    // The untranslated text has no target.
    const std::size_t bye = xliff.find("t_bye");
    CHECK(xliff.find("<target", bye) == std::string::npos);
}

TEST_CASE("XLIFF in: translations read back by key, into the language's column") {
    game::Table table = translations();
    // What a tool sends back: the second text translated (with entities, a character
    // reference and CDATA), the first changed, an empty target, and a key not in the table.
    const std::string back = R"(<?xml version="1.0" encoding="UTF-8"?>
<xliff version="1.2" xmlns="urn:oasis:names:tc:xliff:document:1.2">
  <file original="data/translations" datatype="plaintext" source-language="en" target-language="fr">
    <body>
      <trans-unit id="t_hello"><source>Hello</source><target>Salut &amp; bienvenue&#33;</target></trans-unit>
      <trans-unit id="t_bye"><source>Bye</source><target><![CDATA[À <bientôt>]]></target></trans-unit>
      <trans-unit id="t_gone"><source>Old</source><target>Vieux</target></trans-unit>
      <trans-unit id="t_empty"><source>x</source><target/></trans-unit>
    </body>
  </file>
</xliff>)";
    editor::XliffRead read;
    std::string error;
    REQUIRE(editor::translations_from_xliff(table, back, read, error));
    CHECK(read.language == "fr");
    CHECK(read.filled == 2);
    CHECK(read.unknown == 1);
    CHECK(table.rows[0].cells["fr"] == "Salut & bienvenue!");
    CHECK(table.rows[1].cells["fr"] == "À <bientôt>");
    CHECK(table.rows[0].cells["source"] == "Hello, <traveller> & friend!"); // as written

    // A new language becomes a column (pt-BR as pt_BR); a round trip keeps every text.
    const std::string out = editor::translations_to_xliff(table, "fr");
    std::string brazil = out;
    brazil.replace(brazil.find("target-language=\"fr\""), 20, "target-language=\"pt-BR\"");
    REQUIRE(editor::translations_from_xliff(table, brazil, read, error));
    CHECK(read.language == "pt_BR");
    REQUIRE(table.column("pt_BR") != nullptr);
    CHECK(table.rows[1].cells["pt_BR"] == "À <bientôt>");

    // Refused, the table unchanged: not XLIFF, and no target language.
    const game::Table before = table;
    CHECK_FALSE(editor::translations_from_xliff(table, "id,source\n", read, error));
    CHECK(error == "It is not an XLIFF file.");
    CHECK_FALSE(editor::translations_from_xliff(
        table, R"(<xliff version="1.2"><file source-language="en"><body/></file></xliff>)", read,
        error));
    CHECK(error == "The XLIFF file names no target language to read into.");
    CHECK(table == before);

    // In the Table editor: one step, undone together.
    editor::TableEditor tables(translations());
    REQUIRE(tables.import_xliff(back, read, error));
    CHECK(tables.table().rows[1].cells.at("fr") == "À <bientôt>");
    REQUIRE(tables.history().undo());
    CHECK(tables.table() == translations());
}
