#include "markdownhighlighter.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    rebuildFormats();
}

void MarkdownHighlighter::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setColors(const QString &background, const QString &foreground,
                                    const QString &accent, const QString &codeBackground) {
    if (m_customBackground == background && m_customForeground == foreground
            && m_customAccent == accent && m_customCodeBackground == codeBackground)
        return;

    m_customBackground = background;
    m_customForeground = foreground;
    m_customAccent = accent;
    m_customCodeBackground = codeBackground;
    rebuildFormats();
    rehighlight();
}

// The hidden marker spacing is measured from the document's default font, so
// it has to be remeasured whenever the editor switches typeface.
void MarkdownHighlighter::refreshFont() {
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart)
        return;
    const bool sameQuery = m_searchQuery == query;
    const int previousMatchStart = m_currentMatchStart;
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    if (!sameQuery) {
        rehighlight();
        return;
    }

    // Only the current match moved, so restyle just the blocks it left and
    // entered rather than the whole document.
    const QTextBlock left = document()->findBlock(previousMatchStart);
    const QTextBlock entered = document()->findBlock(currentMatchStart);
    if (left.isValid())
        rehighlightBlock(left);
    if (entered.isValid() && entered != left)
        rehighlightBlock(entered);
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor marker = m_darkMode ? QColor(QStringLiteral("#4f525a"))
                                     : QColor(QStringLiteral("#aeb1b5"));
    const QColor background = !m_customBackground.isEmpty() ? QColor(m_customBackground)
        : (m_darkMode ? QColor(QStringLiteral("#101010")) : QColor(QStringLiteral("#ffffff")));
    const QColor text = !m_customForeground.isEmpty() ? QColor(m_customForeground)
        : (m_darkMode ? QColor(QStringLiteral("#eeeeee")) : QColor(QStringLiteral("#222324")));
    const QColor link = !m_customAccent.isEmpty() ? QColor(m_customAccent)
        : (m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")));
    const QColor quote = marker;

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(background);
    m_hiddenMarkerFormat.setFontPointSize(1.0);

    QFont hiddenFont = document() ? document()->defaultFont() : QFont();
    hiddenFont.setPointSizeF(1.0);
    const qreal charWidth = QFontMetricsF(hiddenFont).horizontalAdvance(QLatin1Char('['));

    m_hiddenMarkerFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_hiddenMarkerFormat.setFontLetterSpacing(-charWidth);

    m_headingFormat = QTextCharFormat();
    m_headingFormat.setForeground(text);
    m_headingFormat.setFontWeight(QFont::Bold);

    m_boldFormat = QTextCharFormat();
    m_boldFormat.setFontWeight(QFont::Bold);
    m_boldFormat.setForeground(text);

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);
    m_italicFormat.setForeground(text);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    if (!m_customCodeBackground.isEmpty())
        m_codeFormat.setBackground(QColor(m_customCodeBackground));

    // A fence recedes the way a heading's `#` does, over the panel it opens.
    m_fenceFormat = m_codeFormat;
    m_fenceFormat.setForeground(marker);

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(quote);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    // A fenced run of code is literal from its opening fence through its
    // closing one, so no markup runs inside it. The state rides from block to
    // block, and Qt rehighlights the rest of the document when it changes.
    const int previous = previousBlockState();
    if (previous >= InsideFence) {
        const bool closing = closesFence(text, previous - InsideFence + 3);
        setCurrentBlockState(closing ? Prose : previous);
        setFormat(0, text.length(), closing ? m_fenceFormat : m_codeFormat);
        highlightSearch(text);
        return;
    }

    const int opening = openingFenceLength(text);
    if (opening > 0) {
        setCurrentBlockState(InsideFence + opening - 3);
        setFormat(0, text.length(), m_fenceFormat);
        highlightSearch(text);
        return;
    }
    setCurrentBlockState(Prose);

    if (!text.isEmpty()) {
        highlightMarkers(text);
        if (text.contains(QLatin1Char('`')) || text.contains(QLatin1Char('*'))
            || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('['))
            || text.contains(QLatin1Char('~'))) {
            highlightInline(text);
        }
    }
    highlightSearch(text);
}

// Three or more backticks, indented at most three spaces. An info string may
// follow, but not one with a backtick in it: "```code```" is inline code.
int MarkdownHighlighter::openingFenceLength(const QString &text) {
    static const QRegularExpression openingRe(QStringLiteral("^ {0,3}(`{3,})[^`]*$"));
    const QRegularExpressionMatch match = openingRe.match(text);
    return match.hasMatch() ? int(match.capturedLength(1)) : 0;
}

// At least as many backticks as opened the fence, and nothing after them.
bool MarkdownHighlighter::closesFence(const QString &text, int openingLength) {
    static const QRegularExpression closingRe(QStringLiteral("^ {0,3}(`{3,})\\s*$"));
    const QRegularExpressionMatch match = closingRe.match(text);
    return match.hasMatch() && match.capturedLength(1) >= openingLength;
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    int from = 0;
    while ((from = text.indexOf(m_searchQuery, from, Qt::CaseInsensitive)) >= 0) {
        const int documentStart = currentBlock().position() + from;
        QTextCharFormat format = this->format(from);
        format.setBackground(documentStart == m_currentMatchStart
                                 ? m_currentSearchFormat.background()
                                 : m_searchFormat.background());
        setFormat(from, m_searchQuery.length(), format);
        from += qMax(1, m_searchQuery.length());
    }
}

void MarkdownHighlighter::highlightMarkers(const QString &text) {
    int first = 0;
    while (first < text.length() && text.at(first).isSpace())
        ++first;
    if (first >= text.length())
        return;

    const QChar firstChar = text.at(first);
    if (first == 0 && firstChar == QLatin1Char('#')) {
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        const QRegularExpressionMatch heading = headingRe.match(text);
        if (heading.hasMatch()) {
            setFormat(0, heading.capturedLength(1) + heading.capturedLength(2),
                      m_markerFormat);
            setFormat(heading.capturedStart(3), heading.capturedLength(3),
                      m_headingFormat);
            return;
        }
    }

    if (firstChar == QLatin1Char('>')) {
        static const QRegularExpression quoteRe(QStringLiteral("^(\\s*>+\\s?)(.*)$"));
        const QRegularExpressionMatch quote = quoteRe.match(text);
        if (quote.hasMatch()) {
            setFormat(0, quote.capturedLength(1), m_markerFormat);
            setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('+')
            || firstChar == QLatin1Char('*') || firstChar.isDigit()) {
        static const QRegularExpression listRe(
            QStringLiteral("^(\\s*(?:[-+*]|\\d+[.)])\\s+)(.*)$"));
        const QRegularExpressionMatch list = listRe.match(text);
        if (list.hasMatch())
            setFormat(0, list.capturedLength(1), m_markerFormat);
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('*')
            || firstChar == QLatin1Char('_')) {
        static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
        const QRegularExpressionMatch rule = ruleRe.match(text);
        if (rule.hasMatch())
            setFormat(0, text.length(), m_markerFormat);
    }
}

void MarkdownHighlighter::highlightInline(const QString &text) {
    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        if (item.kind == InlineKind::Strikethrough)
            continue;
        const QTextCharFormat &contentFormat =
            item.kind == InlineKind::Bold ? m_boldFormat
            : item.kind == InlineKind::Italic ? m_italicFormat
                                              : m_linkFormat;
        setFormat(item.content.start, item.content.length, contentFormat);
        for (const Span &marker : item.markers)
            setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
    }

    // setFormat replaces rather than merges. Code keeps its own styling even
    // when emphasis or a link surrounds it.
    for (const Span &code : codeSpans(text))
        setFormat(code.start, code.length, m_codeFormat);

    // Strikethrough draws over whatever it encloses instead of replacing it.
    for (const InlineMarkup &item : markup) {
        if (item.kind != InlineKind::Strikethrough)
            continue;
        for (int i = item.content.start; i < item.content.start + item.content.length; ++i) {
            QTextCharFormat struck = format(i);
            struck.setFontStrikeOut(true);
            setFormat(i, 1, struck);
        }
        for (const Span &marker : item.markers)
            setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
    }
}

QList<MarkdownHighlighter::Span> MarkdownHighlighter::codeSpans(const QString &text) {
    QList<Span> spans;
    if (!text.contains(QLatin1Char('`')))
        return spans;

    static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
    int from = 0;
    while ((from = text.indexOf(QLatin1Char('`'), from)) >= 0) {
        int backslashes = 0;
        for (int i = from - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
            ++backslashes;
        if (backslashes % 2 != 0) {
            ++from;
            continue;
        }

        // Backslashes escape opening backticks in prose, but are literal once
        // inside code, including immediately before the closing backtick.
        const auto match = codeRe.match(text, from, QRegularExpression::NormalMatch,
                                        QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch()) {
            ++from;
            continue;
        }
        spans.append({from, int(match.capturedLength(0))});
        from = match.capturedEnd(0);
    }
    return spans;
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('_'))
            && !text.contains(QLatin1Char('[')) && !text.contains(QLatin1Char('~'))) {
        return markup;
    }

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };

    // Inline code is literal, so a `*`, `_` or `[` inside backticks is not a
    // marker: `default_line_height` keeps its underscores. Markup whose
    // markers land in a code span is dropped; markup that merely wraps one
    // (**bold with `code` inside**) still applies.
    const QList<Span> code = codeSpans(text);
    const auto append = [&](const InlineMarkup &item) {
        for (const Span &span : code) {
            for (const Span &marker : item.markers) {
                if (marker.start < span.start + span.length
                        && span.start < marker.start + marker.length)
                    return;
            }
        }
        markup.append(item);
    };

    // Underscores only delimit emphasis at a word boundary, so identifiers such
    // as snake_case_name read as themselves. Asterisks delimit anywhere.
    static const QRegularExpression boldRe(
        QStringLiteral("\\*\\*(.+?)\\*\\*|(?<!\\w)__(.+?)__(?!\\w)"),
        QRegularExpression::UseUnicodePropertiesOption);
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(text);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        append({InlineKind::Bold, span(match, contentIndex),
                {{whole.start, 2}, {whole.start + whole.length - 2, 2}}});
    }

    // An asterisk delimiter may not be followed (opening) or preceded
    // (closing) by whitespace, so `* first *important* point` keeps its bullet
    // and `2 * 3 * 4` stays literal instead of pairing stray asterisks.
    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*(?![\\s*])([^*\\n]*[^\\s*])\\*(?!\\*)|(?<!\\w)_([^_\\n]+)_(?!\\w)"),
        QRegularExpression::UseUnicodePropertiesOption);
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(text);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        append({InlineKind::Italic, span(match, contentIndex),
                {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
    }

    // Exactly two tildes, opening before text and closing after it.
    static const QRegularExpression strikeRe(
        QStringLiteral("(?<!~)~~(?![\\s~])(.*?[^\\s~])~~(?!~)"));
    QRegularExpressionMatchIterator strikeMatches = strikeRe.globalMatch(text);
    while (strikeMatches.hasNext()) {
        const QRegularExpressionMatch match = strikeMatches.next();
        const Span whole = span(match, 0);
        append({InlineKind::Strikethrough, span(match, 1),
                {{whole.start, 2}, {whole.start + whole.length - 2, 2}}});
    }

    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^\\]]+)\\]\\(((?:\\\\.|[^)])+)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(text);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        append({InlineKind::Link, content,
                {{whole.start, 1},
                 {contentEnd, whole.start + whole.length - contentEnd}},
                span(match, 2)});
    }

    return markup;
}

QString MarkdownHighlighter::linkUrlAt(const QString &text, int position) {
    if (position < 0 || position >= text.length())
        return {};

    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        if (item.kind != InlineKind::Link
                || position < item.content.start
                || position >= item.content.start + item.content.length) {
            continue;
        }
        // Markdown escapes punctuation in a destination, as the editor does
        // for parentheses; show the URL itself.
        static const QRegularExpression escapedPunctuation(
            QStringLiteral(R"(\\([!-/:-@\[-`{-~]))"));
        return text.mid(item.destination.start, item.destination.length)
            .replace(escapedPunctuation, QStringLiteral("\\1"));
    }
    return {};
}
