#include "editor/core/xliff.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace editor {

namespace game = engine::game;

namespace {

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

/// A code point as UTF-8.
void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80U) {
        out += static_cast<char>(code);
    } else if (code < 0x800U) {
        out += static_cast<char>(0xC0U | (code >> 6U));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else if (code < 0x10000U) {
        out += static_cast<char>(0xE0U | (code >> 12U));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else if (code < 0x110000U) {
        out += static_cast<char>(0xF0U | (code >> 18U));
        out += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    }
}

/// XML text with its entities and character references read, and CDATA kept as written.
std::string unescape(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text.substr(i, 9) == "<![CDATA[") {
            const std::size_t end = text.find("]]>", i + 9);
            const std::size_t stop = end == std::string_view::npos ? text.size() : end;
            out.append(text.substr(i + 9, stop - i - 9));
            i = end == std::string_view::npos ? text.size() : end + 3;
            continue;
        }
        if (text[i] != '&') {
            out += text[i++];
            continue;
        }
        const std::size_t semi = text.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += text[i++];
            continue;
        }
        const std::string_view name = text.substr(i + 1, semi - i - 1);
        if (name == "amp") {
            out += '&';
        } else if (name == "lt") {
            out += '<';
        } else if (name == "gt") {
            out += '>';
        } else if (name == "quot") {
            out += '"';
        } else if (name == "apos") {
            out += '\'';
        } else if (!name.empty() && name.front() == '#') {
            const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            std::uint32_t code = 0;
            for (const char c : name.substr(hex ? 2 : 1)) {
                const int digit = c >= '0' && c <= '9'          ? c - '0'
                                  : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                : -1;
                if (digit < 0 || code > 0x10FFFFU) {
                    code = 0x3FU; // '?'
                    break;
                }
                code = (code * (hex ? 16U : 10U)) + static_cast<std::uint32_t>(digit);
            }
            append_utf8(out, code);
        } else {
            out.append(text.substr(i, semi - i + 1)); // an entity it does not know
        }
        i = semi + 1;
    }
    return out;
}

/// The value of attribute `name` in the start tag `tag`, if it has it.
std::optional<std::string> attribute(std::string_view tag, std::string_view name) {
    for (std::size_t at = tag.find(name); at != std::string_view::npos;
         at = tag.find(name, at + 1)) {
        const bool starts = at > 0 && (tag[at - 1] == ' ' || tag[at - 1] == '\n' ||
                                       tag[at - 1] == '\t' || tag[at - 1] == '\r');
        std::size_t eq = at + name.size();
        while (eq < tag.size() && tag[eq] == ' ') {
            ++eq;
        }
        if (!starts || eq >= tag.size() || tag[eq] != '=') {
            continue;
        }
        std::size_t open = eq + 1;
        while (open < tag.size() && tag[open] == ' ') {
            ++open;
        }
        if (open >= tag.size() || (tag[open] != '"' && tag[open] != '\'')) {
            return std::nullopt;
        }
        const std::size_t close = tag.find(tag[open], open + 1);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        return unescape(tag.substr(open + 1, close - open - 1));
    }
    return std::nullopt;
}

/// The text inside the first <name>...</name> of `xml`; nothing if there is none (an
/// empty <name/> is empty text).
std::optional<std::string> element(std::string_view xml, std::string_view name) {
    const std::string open = "<" + std::string(name);
    for (std::size_t at = xml.find(open); at != std::string_view::npos;
         at = xml.find(open, at + 1)) {
        const std::size_t after = at + open.size();
        if (after >= xml.size() || (xml[after] != '>' && xml[after] != ' ' && xml[after] != '/')) {
            continue;
        }
        const std::size_t end = xml.find('>', after);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        if (xml[end - 1] == '/') {
            return std::string();
        }
        const std::string close = "</" + std::string(name) + ">";
        const std::size_t stop = xml.find(close, end + 1);
        if (stop == std::string_view::npos) {
            return std::nullopt;
        }
        return unescape(xml.substr(end + 1, stop - end - 1));
    }
    return std::nullopt;
}

} // namespace

std::string translations_to_xliff(const game::Table& table, std::string_view language,
                                  std::string_view source_language) {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<xliff version=\"1.2\" xmlns=\"urn:oasis:names:tc:xliff:document:1.2\">\n";
    out += "  <file original=\"data/translations\" datatype=\"plaintext\" source-language=\"" +
           escape(source_language) + "\" target-language=\"" + escape(language) + "\">\n";
    out += "    <body>\n";
    for (const game::TableRow& row : table.rows) {
        const auto cell = [&row](std::string_view column) -> std::string {
            const auto found = row.cells.find(column);
            return found == row.cells.end() ? std::string() : found->second;
        };
        const std::string source = cell("source");
        if (source.empty()) {
            continue;
        }
        out += "      <trans-unit id=\"" + escape(row.id) + "\" xml:space=\"preserve\">\n";
        out += "        <source>" + escape(source) + "</source>\n";
        if (const std::string target = cell(language); !target.empty()) {
            out += "        <target state=\"translated\">" + escape(target) + "</target>\n";
        }
        if (const std::string note = cell("notes"); !note.empty()) {
            out += "        <note>" + escape(note) + "</note>\n";
        }
        out += "      </trans-unit>\n";
    }
    out += "    </body>\n  </file>\n</xliff>\n";
    return out;
}

bool translations_from_xliff(game::Table& table, std::string_view xliff, XliffRead& read,
                             std::string& error) {
    read = XliffRead{};
    const std::size_t root = xliff.find("<xliff");
    const std::size_t file = xliff.find("<file", root == std::string_view::npos ? 0 : root);
    if (root == std::string_view::npos || file == std::string_view::npos) {
        error = "It is not an XLIFF file.";
        return false;
    }
    const std::size_t file_end = xliff.find('>', file);
    const std::optional<std::string> language =
        file_end == std::string_view::npos
            ? std::nullopt
            : attribute(xliff.substr(file, file_end - file), "target-language");
    const bool usable =
        language && !language->empty() && std::ranges::all_of(*language, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '_' || c == '-';
        });
    if (!usable) {
        error = "The XLIFF file names no target language to read into.";
        return false;
    }
    // The column is named as the language is, with - as _ (columns are letters, digits
    // and _).
    std::string column = *language;
    std::replace(column.begin(), column.end(), '-', '_');
    game::Table changed = table;
    if (changed.column(column) == nullptr) {
        game::TableColumn added;
        added.id = column;
        added.kind = game::ColumnKind::Text;
        changed.columns.push_back(added);
    }
    for (std::size_t at = xliff.find("<trans-unit", file_end); at != std::string_view::npos;
         at = xliff.find("<trans-unit", at + 1)) {
        const std::size_t tag_end = xliff.find('>', at);
        const std::size_t unit_end = xliff.find("</trans-unit>", at);
        if (tag_end == std::string_view::npos || unit_end == std::string_view::npos) {
            break;
        }
        const std::optional<std::string> id = attribute(xliff.substr(at, tag_end - at), "id");
        const std::string_view unit = xliff.substr(tag_end + 1, unit_end - tag_end - 1);
        const std::optional<std::string> target = element(unit, "target");
        if (!id || !target || target->empty()) {
            continue;
        }
        const auto row = std::ranges::find_if(
            changed.rows, [&id](const game::TableRow& r) { return r.id == *id; });
        if (row == changed.rows.end()) {
            ++read.unknown;
            continue;
        }
        row->cells[column] = *target;
        ++read.filled;
    }
    read.language = column;
    table = std::move(changed);
    return true;
}

} // namespace editor
