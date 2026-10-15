.pragma library

function normalizePlainText(text) {
    return text.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
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

// Unwraps when the marker is already there, selected or just outside the
// selection, and wraps otherwise. A marker only counts as a whole run, so the
// "*" of "**bold**" is not taken for italic; "***" counts as both.
function toggleWrap(editor, marker) {
    var start = Math.min(editor.selectionStart, editor.selectionEnd);
    var end = Math.max(editor.selectionStart, editor.selectionEnd);
    var text = editor.text;
    var size = marker.length;
    var mark = marker.charAt(0);
    var runFrom = function(position, step, limit) {
        var run = 0;
        while (position + run * step !== limit && text.charAt(position + run * step) === mark)
            ++run;
        return run;
    };
    var holds = function(run) {
        return run === size || (mark === "*" && run === 3);
    };
    var hasMarker = function(part) {
        var runs = part.match(new RegExp("\\" + mark + "+", "g")) || [];
        return runs.some(function(run) { return holds(run.length); });
    };
    var selected = text.slice(start, end);
    var inner = selected.slice(size, selected.length - size);

    // A marker in the middle means separate spans, as in "**a** and **b**":
    // unwrapping would break them, so wrap instead.
    if (holds(runFrom(start - 1, -1, -1)) && holds(runFrom(end, 1, text.length))
            && !hasMarker(selected)) {
        replaceRange(editor, start - size, end + size, selected, 0, selected.length);
    } else if (selected.length >= 2 * size && holds(runFrom(start, 1, end))
               && holds(runFrom(end - 1, -1, start - 1)) && !hasMarker(inner)
               && text.charAt(start - 1) !== mark && text.charAt(end) !== mark) {
        replaceRange(editor, start, end, inner, 0, inner.length);
    } else {
        replaceRange(editor, start, end, marker + selected + marker,
                     size, size + selected.length);
    }
}
