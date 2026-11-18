#include "editor/core/versions.h"

#include <cstddef>

namespace editor {

std::vector<VersionChange> parse_git_status(std::string_view porcelain) {
    std::vector<VersionChange> changes;
    std::size_t at = 0;
    // Entries end with NUL: "XY path", and a rename's or copy's old path as its own entry.
    while (at < porcelain.size()) {
        const std::size_t end = std::min(porcelain.find('\0', at), porcelain.size());
        const std::string_view entry = porcelain.substr(at, end - at);
        at = end + 1;
        if (entry.size() < 4 || entry[2] != ' ') {
            continue;
        }
        const char x = entry[0];
        const char y = entry[1];
        VersionChange change;
        change.path = std::string(entry.substr(3));
        const bool conflict =
            x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
        if (conflict) {
            change.kind = VersionChange::Kind::Conflict;
        } else if (x == '?' || x == 'A' || y == 'A') {
            change.kind = VersionChange::Kind::New;
        } else if (x == 'R' || x == 'C') {
            change.kind = VersionChange::Kind::Renamed;
        } else if (x == 'D' || y == 'D') {
            change.kind = VersionChange::Kind::Deleted;
        } else {
            change.kind = VersionChange::Kind::Changed;
        }
        if (x == 'R' || x == 'C') {
            // The old name follows; it is not a change of its own.
            const std::size_t old_end = std::min(porcelain.find('\0', at), porcelain.size());
            at = old_end + 1;
        }
        changes.push_back(std::move(change));
    }
    return changes;
}

std::vector<Version> parse_git_log(std::string_view text) {
    std::vector<Version> versions;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = std::min(text.find('\x1e', at), text.size());
        std::string_view record = text.substr(at, end - at);
        at = end + 1;
        // Between records git puts a line break.
        while (!record.empty() && (record.front() == '\n' || record.front() == '\r')) {
            record.remove_prefix(1);
        }
        std::vector<std::string_view> fields;
        std::size_t from = 0;
        while (fields.size() < 3) {
            const std::size_t unit = record.find('\x1f', from);
            if (unit == std::string_view::npos) {
                break;
            }
            fields.push_back(record.substr(from, unit - from));
            from = unit + 1;
        }
        if (fields.size() != 3 || fields[0].empty()) {
            continue;
        }
        versions.push_back(Version{std::string(fields[0]), std::string(fields[1]),
                                   std::string(fields[2]), std::string(record.substr(from))});
    }
    return versions;
}

std::string project_gitignore() {
    return "# Kept out of the project's versions by Sparxel: the editor's own working files\n"
           "# (autosaves, snapshots, saves made while testing) and exported games.\n"
           ".sparxel/\n"
           "export/\n";
}

std::string_view change_word(VersionChange::Kind kind) noexcept {
    switch (kind) {
    case VersionChange::Kind::New:
        return "New";
    case VersionChange::Kind::Changed:
        return "Changed";
    case VersionChange::Kind::Deleted:
        return "Deleted";
    case VersionChange::Kind::Renamed:
        return "Renamed";
    case VersionChange::Kind::Conflict:
        return "In conflict";
    }
    return "Changed";
}

} // namespace editor
