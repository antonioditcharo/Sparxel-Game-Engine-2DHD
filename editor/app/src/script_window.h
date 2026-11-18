#pragma once

// The Script editor (plan/02 E11, as Roblox Studio's): a Luau script of the game (an
// assets/*.luau file) written as text, coloured as code, checked as it is typed (the line
// that is wrong marked, and why), with the project's objects beside it: double-clicking an
// action, a property or an event writes the call that uses it. The game loads every script
// as it starts, alongside the no-code logic (game.md, *Scripts*).
//
// See docs/dev/editor.md (Scripts).

#include <QMainWindow>
#include <filesystem>
#include <string>

class QLabel;
class QPlainTextEdit;
class QTimer;
class QTreeWidget;

namespace editor {

class Document;

class ScriptWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ScriptWindow(QWidget* parent = nullptr);

    /// Edits the script `file`, whose name in the game is `name` (its path in assets/);
    /// `document` lists what it can use (null lists nothing). False, with `error`, if the
    /// file cannot be read.
    bool open(const std::filesystem::path& file, const std::string& name, const Document* document,
              std::string& error);
    /// Writes the script. False if it cannot be written (said in the status bar).
    bool save();
    /// Checks the script now: true if it compiles. Otherwise the line is marked and the
    /// mistake said under the text.
    bool check();

    [[nodiscard]] QPlainTextEdit* text() const noexcept { return text_; }
    [[nodiscard]] QTreeWidget* objects() const noexcept { return objects_; }
    [[nodiscard]] QLabel* status() const noexcept { return status_; }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }
    /// Writes at the cursor what the line of the objects list stands for.
    void insert_from(const QString& snippet);

Q_SIGNALS:
    void saved(const std::filesystem::path& file);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void fill_objects(const Document* document);

    std::filesystem::path file_;
    std::string name_;
    bool dirty_ = false;
    QPlainTextEdit* text_ = nullptr;
    QTreeWidget* objects_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* checking_ = nullptr;
};

} // namespace editor
