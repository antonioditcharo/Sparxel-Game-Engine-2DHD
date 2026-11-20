#pragma once

// XLIFF (plan/02 G7): the translations table (data/translations, localization.md) for one
// language as an XLIFF 1.2 file, the format translation tools and services read, and the
// translations they send back read into its column.
//
// A row is a <trans-unit>: its id the row's key, its <source> the words as written, its
// <target> the language's words (left out while there are none), and the "notes" column a
// <note> for the translator.

#include "engine/game/tables.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace editor {

/// The table's texts for `language` (a column of it) as XLIFF 1.2, `source_language` the
/// language the game is written in.
[[nodiscard]] std::string translations_to_xliff(const engine::game::Table& table,
                                                std::string_view language,
                                                std::string_view source_language = "en");

/// What reading translations back did.
struct XliffRead {
    std::string language;    ///< The file's target language.
    std::size_t filled = 0;  ///< Targets written into the table.
    std::size_t unknown = 0; ///< Units whose key the table does not have, left out.
};

/// Reads an XLIFF file's targets into `table`'s column for its target language (made if
/// the table has none), by each unit's id; empty targets leave the cell as it was. False,
/// with `error` in plain language and the table unchanged, if it is not XLIFF or names no
/// target language.
bool translations_from_xliff(engine::game::Table& table, std::string_view xliff, XliffRead& read,
                             std::string& error);

} // namespace editor
