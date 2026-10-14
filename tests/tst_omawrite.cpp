#include <QtTest>
#include <QFont>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QStandardPaths>
#include <QTextDocument>

#include "backend.h"
#include "markdownhighlighter.h"

// Counts QML "Binding loop detected" warnings while it is alive, forwarding
// everything to the previous handler so test output stays intact.
class BindingLoopCounter {
public:
    BindingLoopCounter() {
        s_count = 0;
        m_previous = qInstallMessageHandler(handle);
        s_previous = m_previous;
    }
    ~BindingLoopCounter() { qInstallMessageHandler(m_previous); }
    int count() const { return s_count; }

private:
    static void handle(QtMsgType type, const QMessageLogContext &context, const QString &message) {
        if (message.contains(QLatin1String("Binding loop detected")))
            ++s_count;
        if (s_previous)
            s_previous(type, context, message);
    }
    static inline int s_count = 0;
    static inline QtMessageHandler s_previous = nullptr;
    QtMessageHandler m_previous = nullptr;
};

class OmawriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好世界")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("中文 iPad AI")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("foo中bar")), 3);
        QCOMPARE(Backend::countWords(QStringLiteral("日本語テスト")), 6);
        QCOMPARE(Backend::countWords(QStringLiteral("你好，世界。")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("コーヒー")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("か\u3099")), 1); // が, decomposed
        QCOMPARE(Backend::countWords(QStringLiteral("𠀀𠀁 foo")), 3);
        QCOMPARE(Backend::countWords(QStringLiteral("안녕하세요 세계")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void printsInPointsNotPixels() {
        QFont editorFont;
        editorFont.setPixelSize(20);
        const QFont printed = Backend::printFont(editorFont, 96.0);
        QCOMPARE(printed.pixelSize(), -1);
        QCOMPARE(printed.pointSizeF(), 15.0);
        QCOMPARE(Backend::printFont(editorFont, 0.0).pointSizeF(), 15.0);

        QFont pointFont;
        pointFont.setPointSizeF(11.0);
        QCOMPARE(Backend::printFont(pointFont, 144.0).pointSizeF(), 11.0);
    }

    void keepsNotoFromCrowdingTheFontList() {
        const QStringList fonts = Backend::selectableFontFamilies({
            QStringLiteral("Noto Sans Devanagari"), QStringLiteral("Liberation Serif"),
            QStringLiteral("Noto Serif"), QStringLiteral("Noto Sans Tamil UI"),
            QStringLiteral("adwaita Sans"), QStringLiteral("IBM Plex Mono"),
            QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans Mono"),
            QStringLiteral("Liberation Serif"), QStringLiteral("Monospace"),
            QStringLiteral("Standard Symbols PS"), QStringLiteral("D050000L"),
            QStringLiteral("Noto Color Emoji"), QStringLiteral("Nimbus Sans [UKWN]"),
            QStringLiteral("Nimbus Sans [URW ]")});
        QCOMPARE(fonts, QStringList({QStringLiteral("IBM Plex Mono"),
                                     QStringLiteral("adwaita Sans"),
                                     QStringLiteral("Liberation Serif"),
                                     QStringLiteral("Nimbus Sans"),
                                     QStringLiteral("Noto Sans"),
                                     QStringLiteral("Noto Sans Mono"),
                                     QStringLiteral("Noto Serif")}));
    }

    void remembersEditorFont() {
        const QString installed = Backend().availableFonts().value(1);
        QVERIFY(!installed.isEmpty());

        {
            Backend backend;
            QCOMPARE(backend.editorFont(), Backend::defaultEditorFont());
            QSignalSpy fontSpy(&backend, &Backend::editorFontChanged);
            backend.setEditorFont(installed);
            QCOMPARE(fontSpy.count(), 1);
        }

        QCOMPARE(Backend().editorFont(), installed);

        // A remembered font that has since been uninstalled falls back.
        QSettings().setValue(QStringLiteral("editor/font"), QStringLiteral("No Such Font"));
        QCOMPARE(Backend().editorFont(), Backend::defaultEditorFont());
        QSettings().remove(QStringLiteral("editor/font"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void ignoresInlineMarkdownInsideCodeSpans() {
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("`The_brown_fox` jumps")).size(), 0);
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("`a **b** [c](d)` and `e`")).size(), 0);

        const auto mixed = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("`code_span` then *real* emphasis"));
        QCOMPARE(mixed.size(), 1);
        QCOMPARE(mixed.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(mixed.at(0).content.start, 18);
    }

    // A code span makes its own contents literal; it does not stop markup that
    // merely encloses one. Only a delimiter landing inside the code span does.
    void keepsMarkupThatEnclosesACodeSpan() {
        const auto spanning = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("_a `b` c_"));
        QCOMPARE(spanning.size(), 1);
        QCOMPARE(spanning.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(spanning.at(0).content.start, 1);
        QCOMPARE(spanning.at(0).content.length, 7);

        const auto linked = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("[see `code`](url)"));
        QCOMPARE(linked.size(), 1);
        QCOMPARE(linked.at(0).kind, MarkdownHighlighter::InlineKind::Link);
        QCOMPARE(linked.at(0).content.start, 1);
        QCOMPARE(linked.at(0).content.length, 10);

        const auto wrapped = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("*`code`*"));
        QCOMPARE(wrapped.size(), 1);
        QCOMPARE(wrapped.at(0).kind, MarkdownHighlighter::InlineKind::Italic);

        const auto bold = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**a `b` c**"));
        QCOMPARE(bold.size(), 1);
        QCOMPARE(bold.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(bold.at(0).content.start, 2);
        QCOMPARE(bold.at(0).content.length, 7);

        // The closing underscore is inside the code span, so it is not a marker.
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("_a `b_ c` d_")).size(), 0);
    }

    // Emphasis may enclose a code span, so the code pass has to run last for the
    // code span to keep its own styling.
    void stylesAnEnclosedCodeSpanAsCode() {
        QTextDocument document;
        MarkdownHighlighter highlighter(&document);
        document.setPlainText(QStringLiteral("_a `b` c_"));
        // The constructor's own rehighlight is queued, so ask for one directly.
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        QVERIFY(block.isValid());
        const auto formatAt = [&block](int index) {
            QTextCharFormat found;
            for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
                if (index >= range.start && index < range.start + range.length)
                    found = range.format;
            }
            return found;
        };

        // "a" is italic and carries no code background.
        QVERIFY(formatAt(1).fontItalic());
        QCOMPARE(formatAt(1).background().style(), Qt::NoBrush);

        // The code span keeps the code background and is not italicised.
        QVERIFY(formatAt(4).background().style() != Qt::NoBrush);
        QVERIFY(!formatAt(4).fontItalic());
    }

    // A stray asterisk next to whitespace is not an emphasis delimiter, so it
    // must not swallow a bullet or the operands of arithmetic.
    void keepsWhitespaceDelimitedAsterisksLiteral() {
        // Only `*important*` is markup; the bullet stays a bullet.
        const auto bullet = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("* first *important* point"));
        QCOMPARE(bullet.size(), 1);
        QCOMPARE(bullet.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(bullet.at(0).content.start, 9);
        QCOMPARE(bullet.at(0).content.length, 9);
        QCOMPARE(bullet.at(0).markers[0].start, 8);
        QCOMPARE(bullet.at(0).markers[1].start, 18);

        // Multiplication stays exactly as typed.
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("2 * 3 * 4")).size(), 0);

        // Real emphasis is unaffected.
        const auto shortWord = MarkdownHighlighter::inlineMarkup(QStringLiteral("*a*"));
        QCOMPARE(shortWord.size(), 1);
        QCOMPARE(shortWord.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(shortWord.at(0).content.start, 1);
        QCOMPARE(shortWord.at(0).content.length, 1);

        const auto longPhrase =
            MarkdownHighlighter::inlineMarkup(QStringLiteral("*longer text*"));
        QCOMPARE(longPhrase.size(), 1);
        QCOMPARE(longPhrase.at(0).content.length, 11);

        const auto mixed = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic*"));
        QCOMPARE(mixed.size(), 2);
        QCOMPARE(mixed.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(mixed.at(1).kind, MarkdownHighlighter::InlineKind::Italic);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "selection_foreground = \"#fafafa\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QCOMPARE(backend.themeSelectionForeground(), QStringLiteral("#fafafa"));
        QVERIFY(!backend.darkMode());

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("selectionColor").value<QColor>(), QColor("#445566"));
        QCOMPARE(editor->property("selectedTextColor").value<QColor>(), QColor("#fafafa"));
    }

    void fallsBackForSelectionColors_data() {
        QTest::addColumn<QByteArray>("palette");
        QTest::addColumn<QString>("selection");
        QTest::addColumn<QString>("selectionForeground");

        QTest::newRow("bright_foreground")
            << QByteArray("foreground = \"#c0c0c0\"\n"
                          "bright_foreground = \"#ffffff\"\n"
                          "selection = \"#333333\"\n")
            << QStringLiteral("#333333") << QStringLiteral("#ffffff");
        QTest::newRow("foreground")
            << QByteArray("foreground = \"#c0c0c0\"\n"
                          "selection = \"#333333\"\n")
            << QStringLiteral("#333333") << QStringLiteral("#c0c0c0");
        QTest::newRow("selection_background")
            << QByteArray("foreground = \"#c0c0c0\"\n"
                          "selection_background = \"#222222\"\n")
            << QStringLiteral("#222222") << QStringLiteral("#c0c0c0");
    }

    void fallsBackForSelectionColors() {
        QFETCH(QByteArray, palette);
        QFETCH(QString, selection);
        QFETCH(QString, selectionForeground);

        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeSelection(), selection);
        QCOMPARE(backend.themeSelectionForeground(), selectionForeground);
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void stepsOverWholeCharactersWhenTypingAndSaving() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        const auto setText = [&](const QString &value) {
            editor->setProperty("text", value);
            editor->setProperty("cursorPosition", 0);
        };
        const auto move = [&](int direction) -> int {
            const bool invoked = QMetaObject::invokeMethod(
                editor, "moveCursorVisibly", Q_ARG(QVariant, direction));
            return invoked ? editor->property("cursorPosition").toInt() : -1;
        };

        // Right steps over the whole emoji, so typing keeps it in the file.
        setText(QStringLiteral("a😀b"));
        editor->setProperty("cursorPosition", 1);
        QCOMPARE(move(1), 3);
        QVERIFY(QMetaObject::invokeMethod(editor, "insert", Q_ARG(int, 3),
                                          Q_ARG(QString, QStringLiteral("x"))));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("a😀xb"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString savedPath = directory.filePath(QStringLiteral("emoji.md"));
        backend.saveAs(QUrl::fromLocalFile(savedPath));
        QFile saved(savedPath);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()), QStringLiteral("a😀xb"));

        // Left steps back over the same cluster.
        setText(QStringLiteral("a😀b"));
        editor->setProperty("cursorPosition", 3);
        QCOMPARE(move(-1), 1);

        // Hidden Markdown markers are still skipped on both sides.
        setText(QStringLiteral("**bold**"));
        editor->setProperty("cursorPosition", 0);
        QCOMPARE(move(1), 2);
        editor->setProperty("cursorPosition", 8);
        QCOMPARE(move(-1), 6);

        // Hidden markers next to an emoji combine correctly.
        setText(QStringLiteral("*😀*x"));
        editor->setProperty("cursorPosition", 0);
        QCOMPARE(move(1), 1);
        QCOMPARE(move(1), 4);

        // Paragraph separators still move one position at a time.
        setText(QStringLiteral("ab\ncd"));
        editor->setProperty("cursorPosition", 2);
        QCOMPARE(move(1), 3);
        QCOMPARE(move(-1), 2);
    }

    void stepsCursorByWholeGraphemeClusters() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        const auto step = [&](const QString &text, int from, int direction) {
            editor->setProperty("text", text);
            return backend.cursorPositionStep(from, direction);
        };

        // Qt 6 treats surrogate pairs, skin tone modifiers, ZWJ sequences,
        // regional indicators and combining marks as single cursor steps.
        QCOMPARE(step(QStringLiteral("a😀b"), 1, 1), 3);
        QCOMPARE(step(QStringLiteral("a😀b"), 3, -1), 1);
        QCOMPARE(step(QStringLiteral("a👍🏽b"), 1, 1), 5);
        QCOMPARE(step(QStringLiteral("a👍🏽b"), 5, -1), 1);
        QCOMPARE(step(QStringLiteral("a👨‍👩‍👧b"), 1, 1), 9);
        QCOMPARE(step(QStringLiteral("a👨‍👩‍👧b"), 9, -1), 1);
        QCOMPARE(step(QStringLiteral("a🇺🇸b"), 1, 1), 5);
        QCOMPARE(step(QStringLiteral("a1\u20e3b"), 1, 1), 3);
        QCOMPARE(step(QStringLiteral("ae\u0301b"), 1, 1), 3);

        // Document limits and paragraph separators are handled explicitly.
        QCOMPARE(step(QStringLiteral("a😀b"), 0, -1), 0);
        QCOMPARE(step(QStringLiteral("a😀b"), 4, 1), 4);
        QCOMPARE(step(QStringLiteral("ab\ncd"), 2, 1), 3);
        QCOMPARE(step(QStringLiteral("ab\ncd"), 3, -1), 2);
        QCOMPARE(step(QStringLiteral("ab\ncd"), 5, 1), 5);
        QCOMPARE(step(QStringLiteral("😀\nb"), 2, 1), 3);
        QCOMPARE(step(QStringLiteral("😀\nb"), 3, -1), 2);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void shortcutsDialogHasNoBindingLoop() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        // Material's Dialog derives implicitWidth from its content item; a bare
        // Label assigned as contentItem feeds back into that binding and QML
        // reports a binding loop for the dialog on every launch.
        BindingLoopCounter bindingLoops;

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *dialog = window->findChild<QObject *>(QStringLiteral("shortcutsDialog"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(dialog->property("implicitWidth").toReal() > 0, true);

        QCOMPARE(bindingLoops.count(), 0);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

    void revertsEmptyDocumentToCleanState() {
        Backend backend;
        QQuickTextDocument *quickDocument = attachTextEdit(&backend);
        QVERIFY(quickDocument);

        QVERIFY(!backend.modified());

        quickDocument->textDocument()->setPlainText(QStringLiteral("hello"));
        QVERIFY(backend.editorTextChanged());
        QVERIFY(backend.modified());

        quickDocument->textDocument()->setPlainText(QString());
        QVERIFY(backend.editorTextChanged());
        QVERIFY(!backend.modified());
    }

    void revertsSavedDocumentToCleanState() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("doc.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(file.write("hello"), qint64(5));
        file.close();

        Backend backend;
        QQuickTextDocument *quickDocument = attachTextEdit(&backend);
        QVERIFY(quickDocument);

        backend.open(QUrl::fromLocalFile(path));
        QVERIFY(!backend.modified());

        quickDocument->textDocument()->setPlainText(QStringLiteral("hello world"));
        QVERIFY(backend.editorTextChanged());
        QVERIFY(backend.modified());

        quickDocument->textDocument()->setPlainText(QStringLiteral("hello"));
        QVERIFY(backend.editorTextChanged());
        QVERIFY(!backend.modified());
    }

    void keepsADocumentUnsavedAfterItsFileDisappears() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("gone.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(file.write("hello"), qint64(5));
        file.close();

        Backend backend;
        QQuickTextDocument *quickDocument = attachTextEdit(&backend);
        QVERIFY(quickDocument);

        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);
        backend.open(QUrl::fromLocalFile(path));
        QVERIFY(!backend.modified());

        QVERIFY(QFile::remove(path));
        QTRY_COMPARE(externalChangeSpy.count(), 1);

        // The writer can dismiss the dialog without picking a version, so the
        // text the deleted file used to hold is not a saved state to return to.
        quickDocument->textDocument()->setPlainText(QStringLiteral("hello world"));
        QVERIFY(backend.editorTextChanged());
        quickDocument->textDocument()->setPlainText(QStringLiteral("hello"));
        QVERIFY(backend.editorTextChanged());
        QVERIFY(backend.modified());

        // Nor is emptying it: an empty document only reads as clean while there
        // is no file behind it.
        quickDocument->textDocument()->setPlainText(QString());
        QVERIFY(backend.editorTextChanged());
        QVERIFY(backend.modified());
    }

private:
    // Loads a bare TextEdit, attaches its document to the backend, and returns
    // the QQuickTextDocument. The engine and TextEdit are parented to the
    // backend so they outlive the helper call.
    QQuickTextDocument *attachTextEdit(Backend *backend) {
        auto *engine = new QQmlEngine(backend);
        auto *component = new QQmlComponent(engine);
        component->setData(R"QML(
            import QtQuick
            TextEdit { }
        )QML", QUrl::fromLocalFile(QStringLiteral("Harness.qml")));
        if (!component->isReady())
            return nullptr;

        QObject *editor = component->create();
        if (!editor)
            return nullptr;
        editor->setParent(backend);

        auto *quickDocument = qobject_cast<QQuickTextDocument *>(
            editor->property("textDocument").value<QObject *>());
        if (!quickDocument)
            return nullptr;

        backend->attachDocument(quickDocument);
        return quickDocument;
    }

    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmawriteTest)
#include "tst_omawrite.moc"
