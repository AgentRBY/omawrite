.pragma library

function normalizePlainText(text) {
    return text.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
}

function toggleWrap(editor, before, after) {
    var start = Math.min(editor.selectionStart, editor.selectionEnd);
    var end = Math.max(editor.selectionStart, editor.selectionEnd);
    var text = editor.text;
    var selected = text.slice(start, end);
    var mark = before.charAt(0);
    var size = before.length;

    // "*" is also half of "**", so measure the whole run of marker characters.
    // A run holds this emphasis when it is exactly the marker, or, for
    // asterisks, three of them: "***" is bold and italic at once.
    var holds = function(run) {
        return run === size || (mark === "*" && run === 3);
    };
    var runBefore = function(position) {
        var i = position;
        while (i > 0 && text.charAt(i - 1) === mark)
            --i;
        return position - i;
    };
    var runAfter = function(position) {
        var i = position;
        while (i < text.length && text.charAt(i) === mark)
            ++i;
        return i - position;
    };
    // Markers of this emphasis inside the text mean the selection may span
    // separate runs, as in "**a** and **b**", where stripping the outer pair
    // would leave the inner ones unmatched. Telling that apart from a wrapper
    // around nested emphasis needs Markdown's full delimiter rules, so wrap
    // instead: an extra pair is one undo away, a broken pair is not.
    // Other emphasis nested inside, like the bold in "*a **b** c*", is fine.
    var spansRuns = function(inner) {
        var i = 0;
        while (i < inner.length) {
            if (inner.charAt(i) !== mark) {
                ++i;
                continue;
            }
            var runStart = i;
            while (i < inner.length && inner.charAt(i) === mark)
                ++i;
            if (holds(i - runStart))
                return true;
        }
        return false;
    };

    // The run on each side counts the markers inside the selection and those
    // just outside it, so the toggle undoes itself whether or not the markers
    // were selected: Ctrl+B twice on "*word*" leaves "*word*".
    var leading = 0;
    while (leading < selected.length && selected.charAt(leading) === mark)
        ++leading;
    var trailing = 0;
    while (trailing < selected.length - leading
           && selected.charAt(selected.length - 1 - trailing) === mark)
        ++trailing;
    var outsideBefore = runBefore(start);
    var outsideAfter = runAfter(end);
    var left = outsideBefore + leading;
    var right = outsideAfter + trailing;
    var content = selected.slice(leading, selected.length - trailing);
    // With nothing between them, the two sides may just be halves of one
    // longer delimiter, like the caret inside the "**" of "**word**". Only an
    // empty pair with space or the line's end on both sides is ours to remove.
    var bounded = function(character) {
        return character === "" || /\s/.test(character);
    };
    var standsAlone = content.length > 0
        || (bounded(text.charAt(start - outsideBefore - 1))
            && bounded(text.charAt(end + outsideAfter)));
    // Markers beside space are literal, as in "2 * 3 * 4", not emphasis.
    var hugsContent = content.length === 0
        || (!/\s/.test(content.charAt(0)) && !/\s/.test(content.charAt(content.length - 1)));
    // A backslash before either run makes it literal text.
    var escapedAt = function(position) {
        var backslashes = 0;
        for (var b = position - 1; b >= 0 && text.charAt(b) === "\\"; --b)
            ++backslashes;
        return backslashes % 2 === 1;
    };
    var escaped = escapedAt(start - outsideBefore) || escapedAt(end - trailing);
    if (left === right && holds(left) && standsAlone && hugsContent && !escaped
            && !spansRuns(content)) {
        var keep = Array(left - size + 1).join(mark);
        var rangeStart = start - outsideBefore;
        replaceRange(editor, rangeStart, end + outsideAfter, keep + content + keep,
                     keep.length, keep.length + content.length);
        return;
    }

    replaceRange(editor, start, end, before + selected + after,
                 before.length, before.length + selected.length);
}

function replaceRange(editor, rangeStart, rangeEnd, replacement,
                      selectionStartOffset, selectionEndOffset) {
    var start = Math.max(0, Math.min(editor.text.length, rangeStart));
    var end = Math.max(start, Math.min(editor.text.length, rangeEnd));
    var insertedText = normalizePlainText(replacement);

    if (start !== end)
        editor.remove(start, end);

    editor.cursorPosition = start;
    editor.insert(start, insertedText);

    // TextEdit.insert() already leaves the caret after the inserted text. Only
    // move it again when the caller deliberately requests a selection/caret
    // within the replacement.
    if (selectionStartOffset !== undefined && selectionEndOffset !== undefined) {
        var insertedEnd = editor.cursorPosition;
        var selectionStart = Math.max(start,
                                      Math.min(insertedEnd, start + selectionStartOffset));
        var selectionEnd = Math.max(start,
                                    Math.min(insertedEnd, start + selectionEndOffset));
        if (selectionStart === selectionEnd)
            editor.cursorPosition = selectionStart;
        else
            editor.select(selectionStart, selectionEnd);
    }

    return insertedText;
}
