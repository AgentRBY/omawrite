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
    // Markers inside the text mean the selection spans several runs, as in
    // "**a** and **b**". Stripping the outer pair would leave the inner ones
    // unmatched, so wrap the whole selection instead.
    var spansRuns = function(inner) {
        return inner.indexOf(before) >= 0;
    };

    // Selection includes the markers: strip them.
    var leading = 0;
    while (leading < selected.length && selected.charAt(leading) === mark)
        ++leading;
    var trailing = 0;
    while (trailing < selected.length - leading
           && selected.charAt(selected.length - 1 - trailing) === mark)
        ++trailing;
    if (leading >= size && leading === trailing && selected.length > leading + trailing
            && leading + runBefore(start) === trailing + runAfter(end)
            && holds(leading + runBefore(start))
            && !spansRuns(selected.slice(leading, selected.length - trailing))) {
        var inner = selected.slice(size, selected.length - size);
        replaceRange(editor, start, end, inner, 0, inner.length);
        return;
    }

    // Selection (or caret) sits directly inside the markers: strip them.
    var outside = runBefore(start);
    if (outside === runAfter(end) && holds(outside) && leading === 0 && !spansRuns(selected)) {
        replaceRange(editor, start - size, end + size, selected, 0, selected.length);
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
