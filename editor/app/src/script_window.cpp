#include "script_window.h"

#include "engine/logic/vm.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFont>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStatusBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>

#include "editor/core/document.h"

namespace editor {

namespace fs = std::filesystem;

namespace {

QString text_of(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

/// Luau coloured as code: keywords, the game's sx functions, words, numbers and comments.
class LuauHighlighter : public QSyntaxHighlighter {
public:
    explicit LuauHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
        QTextCharFormat keyword;
        keyword.setForeground(QColor(86, 110, 214));
        keyword.setFontWeight(QFont::Bold);
        const QStringList words{
            QStringLiteral("and"),     QStringLiteral("break"),  QStringLiteral("do"),
            QStringLiteral("else"),    QStringLiteral("elseif"), QStringLiteral("end"),
            QStringLiteral("false"),   QStringLiteral("for"),    QStringLiteral("function"),
            QStringLiteral("if"),      QStringLiteral("in"),     QStringLiteral("local"),
            QStringLiteral("nil"),     QStringLiteral("not"),    QStringLiteral("or"),
            QStringLiteral("repeat"),  QStringLiteral("return"), QStringLiteral("then"),
            QStringLiteral("true"),    QStringLiteral("until"),  QStringLiteral("while"),
            QStringLiteral("continue")};
        for (const QString& word : words) {
            rules_.push_back({QRegularExpression(QStringLiteral("\\b%1\\b").arg(word)), keyword});
        }
        QTextCharFormat api;
        api.setForeground(QColor(170, 90, 20));
        rules_.push_back({QRegularExpression(QStringLiteral("\\bsx\\.[a-z_]+")), api});
        QTextCharFormat number;
        number.setForeground(QColor(30, 140, 120));
        rules_.push_back({QRegularExpression(QStringLiteral("\\b[0-9]+(\\.[0-9]+)?\\b")), number});
        QTextCharFormat words_format;
        words_format.setForeground(QColor(160, 50, 60));
        rules_.push_back({QRegularExpression(QStringLiteral("\"[^\"]*\"|'[^']*'")), words_format});
        comment_.setForeground(QColor(120, 128, 136));
        comment_.setFontItalic(true);
    }

protected:
    void highlightBlock(const QString& line) override {
        for (const Rule& rule : rules_) {
            QRegularExpressionMatchIterator found = rule.pattern.globalMatch(line);
            while (found.hasNext()) {
                const QRegularExpressionMatch match = found.next();
                setFormat(static_cast<int>(match.capturedStart()),
                          static_cast<int>(match.capturedLength()), rule.format);
            }
        }
        // A comment runs to the end of its line (outside words in quotes: good enough).
        const qsizetype dashes = line.indexOf(QStringLiteral("--"));
        if (dashes >= 0) {
            setFormat(static_cast<int>(dashes), static_cast<int>(line.size() - dashes), comment_);
        }
    }

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    std::vector<Rule> rules_;
    QTextCharFormat comment_;
};

} // namespace

ScriptWindow::ScriptWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("script_window"));
    resize(900, 640);
    auto* middle = new QWidget(this);
    auto* column = new QVBoxLayout(middle);
    text_ = new QPlainTextEdit(middle);
    text_->setObjectName(QStringLiteral("script_text"));
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    text_->setFont(mono);
    text_->setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')));
    new LuauHighlighter(text_->document());
    column->addWidget(text_, 1);
    status_ = new QLabel(middle);
    status_->setObjectName(QStringLiteral("script_status"));
    status_->setWordWrap(true);
    column->addWidget(status_);
    setCentralWidget(middle);

    auto* dock = new QDockWidget(tr("What it can use"), this);
    dock->setObjectName(QStringLiteral("script_objects_dock"));
    objects_ = new QTreeWidget(dock);
    objects_->setObjectName(QStringLiteral("script_objects"));
    objects_->setHeaderHidden(true);
    objects_->setToolTip(tr("Double-click to write the call that uses it"));
    dock->setWidget(objects_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(objects_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const QString snippet = item->data(0, Qt::UserRole).toString();
        if (!snippet.isEmpty()) {
            insert_from(snippet);
        }
    });

    checking_ = new QTimer(this);
    checking_->setSingleShot(true);
    checking_->setInterval(300);
    connect(checking_, &QTimer::timeout, this, [this] { check(); });
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        dirty_ = true;
        checking_->start();
    });

    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Save"), this, [this] { save(); })->setShortcut(QKeySequence::Save);
    file->addAction(tr("&Close"), this, [this] { close(); })->setShortcut(QKeySequence::Close);
    QMenu* script = menuBar()->addMenu(tr("&Script"));
    script->addAction(tr("&Check"), this, [this] { check(); })
        ->setShortcut(QKeySequence(QStringLiteral("F7")));
}

bool ScriptWindow::open(const fs::path& file, const std::string& name, const Document* document,
                        std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "The script cannot be read.";
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    file_ = file;
    name_ = name;
    setWindowTitle(tr("%1 - Script").arg(text_of(name)));
    text_->setPlainText(text_of(source));
    dirty_ = false;
    fill_objects(document);
    check();
    return true;
}

// The tree owns every item made with it as their parent; the analyzer cannot see Qt's
// ownership.
// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
void ScriptWindow::fill_objects(const Document* document) {
    objects_->clear();
    auto* api = new QTreeWidgetItem(objects_, {tr("The game's script functions")});
    const std::array<std::pair<const char*, const char*>, 6> functions{{
        {"sx.call(object, action, ...)", "sx.call(\"\", \"\")"},
        {"sx.get(object, property)", "sx.get(\"\", \"\")"},
        {"sx.set(object, property, value)", "sx.set(\"\", \"\", )"},
        {"sx.wait(seconds)", "sx.wait(1)"},
        {"sx.log(...)", "sx.log(\"\")"},
        {"sx.on(event, function)", "sx.on(\".\", function()\n\nend)"},
    }};
    for (const auto& [shown, written] : functions) {
        auto* item = new QTreeWidgetItem(api, {QString::fromUtf8(shown)});
        item->setData(0, Qt::UserRole, QString::fromUtf8(written));
    }
    if (document == nullptr) {
        return;
    }
    // Each object: the actions it does, the events it has.
    std::map<std::string, QTreeWidgetItem*> by_object;
    const auto object_item = [&](const std::string& object) {
        auto& item = by_object[object];
        if (item == nullptr) {
            item = new QTreeWidgetItem(objects_, {text_of(object)});
        }
        return item;
    };
    for (const LogicAction& action : document->logic_actions()) {
        if (action.object.empty()) {
            continue; // the no-code steps (Wait, Log, Run)
        }
        auto* item = new QTreeWidgetItem(object_item(action.object), {text_of(action.text)});
        item->setData(0, Qt::UserRole,
                      text_of("sx.call(\"" + action.object + "\", \"" + action.member + "\")"));
    }
    for (const LogicEvent& event : document->logic_events()) {
        if (event.object.empty()) {
            continue;
        }
        auto* item = new QTreeWidgetItem(object_item(event.object), {text_of(event.text)});
        item->setData(
            0, Qt::UserRole,
            text_of("sx.on(\"" + event.object + "." + event.event + "\", function(...)\n\nend)"));
    }
}

// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

void ScriptWindow::insert_from(const QString& snippet) {
    text_->insertPlainText(snippet);
    text_->setFocus();
}

bool ScriptWindow::check() {
    checking_->stop();
    const std::string source = text_->toPlainText().toStdString();
    const std::optional<engine::logic::ScriptError> error =
        engine::logic::check_script(name_.empty() ? std::string("script") : name_, source);
    QList<QTextEdit::ExtraSelection> marks;
    if (!error) {
        status_->setText(tr("Runs: no problems."));
        text_->setExtraSelections(marks);
        return true;
    }
    status_->setText(error->line > 0
                         ? tr("Line %1: %2").arg(error->line).arg(text_of(error->message))
                         : text_of(error->message));
    if (error->line > 0) {
        QTextEdit::ExtraSelection mark;
        mark.format.setBackground(QColor(255, 220, 220));
        mark.format.setProperty(QTextFormat::FullWidthSelection, true);
        mark.cursor = QTextCursor(text_->document()->findBlockByNumber(error->line - 1));
        marks.append(mark);
    }
    text_->setExtraSelections(marks);
    return false;
}

bool ScriptWindow::save() {
    const std::string source = text_->toPlainText().toStdString();
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    out.write(source.data(), static_cast<std::streamsize>(source.size()));
    if (!out.good()) {
        statusBar()->showMessage(tr("The script could not be written."), 8000);
        return false;
    }
    out.close();
    dirty_ = false;
    statusBar()->showMessage(tr("Saved."), 4000);
    Q_EMIT saved(file_);
    return true;
}

void ScriptWindow::closeEvent(QCloseEvent* event) {
    if (dirty_ && !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        const auto answer = QMessageBox::question(
            this, tr("Script"), tr("Save the changes to %1?").arg(text_of(name_)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

} // namespace editor
