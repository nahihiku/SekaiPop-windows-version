.pragma library

var DAY_NAMES = ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"];
var DAY_LONG = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];

function weekdayLabels(firstDay) {
    var out = [];
    for (var i = 0; i < 7; i++)
        out.push(DAY_NAMES[(firstDay + i) % 7]);
    return out;
}

function pad2(n) {
    return (n < 10 ? "0" : "") + n;
}

function dayKey(y, m, d) {
    return y + "-" + pad2(m + 1) + "-" + pad2(d);
}

function monthStart(today, shift) {
    return new Date(today.getFullYear(), today.getMonth() + shift, 1);
}

function shiftFor(today, year, month) {
    return (year - today.getFullYear()) * 12 + (month - today.getMonth());
}

function monthGrid(viewDate, today, firstDay) {
    var y = viewDate.getFullYear();
    var m = viewDate.getMonth();
    var lead = (new Date(y, m, 1).getDay() - firstDay + 7) % 7;
    var cells = [];
    for (var i = 0; i < 42; i++) {
        var d = new Date(y, m, 1 - lead + i);
        var dow = d.getDay();
        cells.push({
            y: d.getFullYear(),
            m: d.getMonth(),
            d: d.getDate(),
            dow: dow,
            row: Math.floor(i / 7),
            col: i % 7,
            key: dayKey(d.getFullYear(), d.getMonth(), d.getDate()),
            day: d.getDate(),
            inMonth: d.getMonth() === m,
            isToday: d.getFullYear() === today.getFullYear()
                && d.getMonth() === today.getMonth()
                && d.getDate() === today.getDate(),
            isWeekend: dow === 0 || dow === 6
        });
    }
    return cells;
}

var MONTHS = ["January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"];

function monthTitle(viewDate) {
    return MONTHS[viewDate.getMonth()] + " " + viewDate.getFullYear();
}

function monthName(month) {
    return MONTHS[((month % 12) + 12) % 12];
}

function monthShort(month) {
    return monthName(month).slice(0, 3);
}

function weekdayShort(dow) {
    return DAY_LONG[dow].slice(0, 3);
}

function weekdayLong(dow) {
    return DAY_LONG[dow];
}

function daysInMonth(y, m) {
    return new Date(y, m + 1, 0).getDate();
}

function dayDiff(today, y, m, d) {
    var a = Date.UTC(today.getFullYear(), today.getMonth(), today.getDate());
    var b = Date.UTC(y, m, d);
    return Math.round((b - a) / 86400000);
}

function relative(diff) {
    if (diff === 0) return "Today";
    if (diff === 1) return "Tomorrow";
    if (diff === -1) return "Yesterday";
    var n = Math.abs(diff);
    var text;
    if (n < 21)
        text = n + " days";
    else if (n < 120)
        text = Math.round(n / 7) + " weeks";
    else
        text = Math.round(n / 30.44) + " months";
    return diff > 0 ? "in " + text : text + " ago";
}

function isoWeek(y, m, d) {
    var date = new Date(Date.UTC(y, m, d));
    var dayNum = date.getUTCDay() || 7;
    date.setUTCDate(date.getUTCDate() + 4 - dayNum);
    var yearStart = new Date(Date.UTC(date.getUTCFullYear(), 0, 1));
    return Math.ceil(((date - yearStart) / 86400000 + 1) / 7);
}

function longDay(y, m, d) {
    var dow = new Date(y, m, d).getDay();
    return DAY_LONG[dow] + ", " + d + " " + monthShort(m);
}

function parseMarks(text) {
    var out = {};
    if (!text)
        return out;
    var parts = String(text).split(",");
    for (var i = 0; i < parts.length; i++) {
        var m = /^(\d{4}-\d{2}-\d{2}):([1-5])$/.exec(parts[i]);
        if (m)
            out[m[1]] = parseInt(m[2], 10);
    }
    return out;
}

function serializeMarks(marks, today) {
    var keys = Object.keys(marks).filter(function (k) {
        if (!(marks[k] >= 1 && marks[k] <= 5))
            return false;
        var p = k.split("-");
        return dayDiff(today, parseInt(p[0], 10), parseInt(p[1], 10) - 1, parseInt(p[2], 10)) > -400;
    });
    keys.sort();
    if (keys.length > 300)
        keys = keys.slice(keys.length - 300);
    return keys.map(function (k) { return k + ":" + marks[k]; }).join(",");
}

function outBack(t, s) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    var c = (s === undefined ? 1.8 : s);
    var u = t - 1;
    return 1 + (c + 1) * u * u * u + c * u * u;
}
