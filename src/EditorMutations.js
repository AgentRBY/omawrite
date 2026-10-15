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
    // Markers of this emphasis inside the text are fine when they pair up
    // among themselves, as in "**a **b** c**", but one that closes before it
    // opens means the selection spans separate runs, as in "**a** and **b**".
    // Stripping the outer pair there would leave the inner ones unmatched.
    // A run reads as closing when text sits before it and space after it.
    var spansRuns = function(inner) {
        var depth = 0;
        var i = 0;
        while (i < inner.length) {
            if (inner.charAt(i) !== mark) {
                ++i;
                continue;
            }
            var runStart = i;
            while (i < inner.length && inner.charAt(i) === mark)
                ++i;
            if (!holds(i - runStart))
                continue;
            var spaceBefore = runStart === 0 || /\s/.test(inner.charAt(runStart - 1));
            var spaceAfter = i === inner.length || /\s/.test(inner.charAt(i));
            var closing = !spaceBefore && (spaceAfter || depth > 0);
            if (!closing)
                ++depth;
            else if (depth === 0)
                return true;
            else
                --depth;
        }
        return depth !== 0;
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
    // empty pair standing on its own is ours to remove.
    var standsAlone = content.length > 0
        || (!/\w/.test(text.charAt(start - outsideBefore - 1))
            && !/\w/.test(text.charAt(end + outsideAfter)));
    if (left === right && holds(left) && standsAlone && !spansRuns(content)) {
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
