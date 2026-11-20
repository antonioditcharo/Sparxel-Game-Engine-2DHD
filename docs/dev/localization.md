# Localization

> **Status: v0.1.** Games in other languages: the texts a game shows collected into one
> translations table with a key each, filled in by translators in the Table window or a
> spreadsheet, and shown in the language the project or the player chooses, with a pseudo
> language for checking. Tested on its own, in a running game, and in the editor.
>
> Code: [`engine/game/include/engine/game/localization.h`](../../engine/game/include/engine/game/localization.h)
> and [`src/localization.cpp`](../../engine/game/src/localization.cpp); collecting in
> [`editor/core/include/editor/core/document.h`](../../editor/core/include/editor/core/document.h)
> (`texts_to_translate()`, `collect_translations()`). Tests:
> [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp) and
> [`tests/editor/document_test.cpp`](../../tests/editor/document_test.cpp).

## 1. What it is for

A game written once can be played in many languages (`plan/02` G7). Creators write their
words as usual, in Rules and conversations; translators get every one of them in a single
table, and nothing in the game's logic changes.

## 2. Translating a game

1. In the Project view's **Languages** group, **Translate into** lists the languages, as
   codes separated by commas (*fr, de, ja*; `languages` in the project file).
2. **Game ▸ Collect texts to translate** puts every text the game shows in
   `assets/data/translations.dtable` (made if missing) and opens it in the Table window
   ([tables.md](tables.md)): a row per text, named by a key made from its words
   (*t_1a2b3c4d*), the words as written in **source**, and a column per language. Run it
   again whenever words are added: new texts get rows, and translations already written
   stay. A **notes** column, if added, is for translators and is never shown.
3. Translators fill in the language columns there, or in a spreadsheet: the Table window
   exports CSV and reads it back. **Translation tools and services** take XLIFF 1.2: the
   Table window's *Export for translators (XLIFF)…* writes one language's texts
   (`translations_to_xliff()`: a `<trans-unit>` per text, its id the text's key, its
   `<source>` the words as written, its `<target>` the translation so far, and the
   **notes** a `<note>`), and *Bring in translations (XLIFF)…* reads what comes back into
   that language's column by key, as one undo step (`translations_from_xliff()`: entities,
   character references and CDATA read; a language not yet a column made one, `pt-BR` as
   `pt_BR`; empty targets leave the cell as it was; texts the table no longer has left out
   and counted in the status bar). Tested: a table out with its markup escaped, a
   translation and a note, the untranslated text with no target; back in, two
   translations filled, one unknown key counted, the words as written untouched; a new
   language made a column; not XLIFF, or no target language, refused with the table
   unchanged; in the Table editor, one step undone together.
4. **Shown in** (`language`) is the language the game starts in: a language column, or
   empty for the words as written. Players choose another on the pause menu's Options page
   (*Language*: *The game's own*, then each language column, kept with their options:
   [menus.md](menus.md)).

What is collected, each once, in the order found:

| From | Texts |
|---|---|
| Conversations ([story.md](story.md)) | Speakers' names, each line, each choice |
| Rules, Sequences and Brains | What *Say*, *Ask*, *{speaker} says* and *{speaker} asks* show, when written as text, and each of the answers (separated by `|`) |
| The menus | The title screen's and the pause menu's choices |

## 3. Playing in another language

**Fonts by language** (`language_fonts`, the Project view's *Fonts by language*): for
letters the game's font lacks, a font picture per language, *ja: fonts/ja.png, ko:
fonts/ko.png* (`parse_language_fonts()`). Each picture's letters, in order, are in the
text file of the same name beside it (`fonts/ja.txt`); without one, printable ASCII. While
that language is shown, from the start or picked on the Options page, the game draws its
words in that font; a language without one uses the game's font. A picture that cannot be
used is said once in the game's problems, and the game's font is shown. Tested: entries
parsed with spaces trimmed and broken ones left out; in Japanese, the font's letters drawn
in the words' colour; in the game's own language, the built-in font, drawn differently; a
missing picture, the game's font and one problem naming it.

Messages and questions are shown translated as they come; each answer is translated on its
own; speakers' name plates and the menus' choices are translated where they are drawn. A
text without a translation shows as written, so a half-translated game still plays.
Logic is not changed: *The answer's words* is the answer as written (*Yes*, not *Oui*), so
Rules that compare it work in every language, and the log of what was said keeps the words
as written.

**The pseudo language.** With **Shown in** set to *pseudo*, every text shows with its
vowels doubled, in brackets (*[Heelloo, traaveelleer!]*): text without brackets was not
collected (built from parts, say), and text that no longer fits shows where a longer
language would overflow. `[tags]` of rich text and `{names}` stay as they are.

## 4. Next increments

1. **Everything else translated**: item, skill and enemy names from the data tables, the
   battle's messages, and the rest of the menus' words.
2. **An in-editor view** of each text where it is used.
