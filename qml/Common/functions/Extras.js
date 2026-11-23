.pragma library
.import "../../Sekai/launcher/Calc.js" as Calc

var LONG = {
    calc: "calc", calculator: "calc", calculate: "calc",
    color: "color", colour: "color", colors: "color", colours: "color",
    web: "web", search: "web", google: "web:google", duckduckgo: "web:ddg",
    youtube: "web:youtube", github: "web:github", wikipedia: "web:wiki"
};
var SHORT = {
    col: "color",
    g: "web:google", ddg: "web:ddg", duck: "web:ddg", yt: "web:youtube",
    gh: "web:github", wiki: "web:wiki"
};

var ENGINES = [
    { id: "ddg",     name: "DuckDuckGo", host: "duckduckgo.com",     url: "https://duckduckgo.com/?q=%s",                    home: "https://duckduckgo.com" },
    { id: "google",  name: "Google",     host: "google.com",         url: "https://www.google.com/search?q=%s",              home: "https://www.google.com" },
    { id: "youtube", name: "YouTube",    host: "youtube.com",        url: "https://www.youtube.com/results?search_query=%s", home: "https://www.youtube.com" },
    { id: "github",  name: "GitHub",     host: "github.com",         url: "https://github.com/search?q=%s",                  home: "https://github.com" },
    { id: "wiki",    name: "Wikipedia",  host: "wikipedia.org",      url: "https://en.wikipedia.org/w/index.php?search=%s",  home: "https://en.wikipedia.org" }
];

function engineIndex(id) {
    for (var i = 0; i < ENGINES.length; i++)
        if (ENGINES[i].id === id) return i;
    return 0;
}

function searchUrl(engine, query) {
    return ENGINES[engine].url.replace("%s", encodeURIComponent(query));
}

function looksLikeUrl(q) {
    var s = q.trim();
    if (/\s/.test(s) || s.length < 3) return false;
    if (/^[a-z][a-z0-9+.-]*:\/\/\S+$/i.test(s)) return true;
    return /^(www\.)?[a-z0-9-]+(\.[a-z0-9-]+)*\.(com|org|net|io|dev|app|co|me|gg|tv|xyz|edu|gov|uk|de|jp|fr|ph|ai|so|cc|info|wiki)(:\d+)?(\/\S*)?$/i.test(s);
}

function urlFor(q) {
    var s = q.trim();
    return /^[a-z][a-z0-9+.-]*:\/\//i.test(s) ? s : "https://" + s;
}

function route(text) {
    var raw = String(text);
    var t = raw.replace(/^\s+/, "");
    var none = { mode: "", arg: "", prefix: "", engine: 0, terminal: false };
    if (t === "") return none;

    var c = t.charAt(0);
    if (c === ">" || c === "$") {
        var terminal = c === "$" || t.charAt(1) === ">";
        var body = t.replace(/^(>>|>|\$)\s*/, "");
        return { mode: "shell", arg: body, prefix: t.slice(0, t.length - body.length), engine: 0, terminal: terminal };
    }
    if (c === "?") return sym(t, "web");
    if (c === "=") return sym(t, "calc");

    var m = /^(\S+)(\s+)?([\s\S]*)$/.exec(t);
    var word = m[1].toLowerCase();
    var hasSpace = m[2] !== undefined;
    var rest = m[3];
    var hit = LONG[word] || (hasSpace ? SHORT[word] : undefined);
    if (hit) {
        var parts = hit.split(":");
        var prefix = hasSpace ? t.slice(0, t.length - rest.length) : m[1] + " ";
        return { mode: parts[0], arg: rest, prefix: prefix, engine: parts.length > 1 ? engineIndex(parts[1]) : 0, terminal: false };
    }

    if (Calc.looksLikeMath(t)) return { mode: "calc", arg: t, prefix: "", engine: 0, terminal: false };
    if (parseColor(t, false)) return { mode: "color", arg: t, prefix: "", engine: 0, terminal: false };
    return none;

    function sym(s, mode) {
        var body = s.slice(1).replace(/^\s+/, "");
        return { mode: mode, arg: body, prefix: s.slice(0, s.length - body.length), engine: 0, terminal: false };
    }
}

var NAMES = {
    black: "#000000", white: "#ffffff", red: "#ff0000", green: "#008000", blue: "#0000ff",
    yellow: "#ffff00", cyan: "#00ffff", magenta: "#ff00ff", orange: "#ffa500", purple: "#800080",
    pink: "#ffc0cb", hotpink: "#ff69b4", brown: "#a52a2a", gray: "#808080", grey: "#808080",
    silver: "#c0c0c0", gold: "#ffd700", navy: "#000080", teal: "#008080", lime: "#00ff00",
    maroon: "#800000", olive: "#808000", aqua: "#00ffff", coral: "#ff7f50", salmon: "#fa8072",
    crimson: "#dc143c", indigo: "#4b0082", violet: "#ee82ee", turquoise: "#40e0d0", tomato: "#ff6347",
    skyblue: "#87ceeb", lavender: "#e6e6fa", mint: "#00ffcb", beige: "#f5f5dc", khaki: "#f0e68c",
    orchid: "#da70d6", plum: "#dda0dd", chocolate: "#d2691e", tan: "#d2b48c", ivory: "#fffff0",
    transparent: "#00000000"
};

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

function num(s) {
    s = String(s).trim();
    return s.charAt(s.length - 1) === "%" ? parseFloat(s) * 2.55 : parseFloat(s);
}

function parseColor(input, loose) {
    var s = String(input).trim().toLowerCase();
    if (s === "") return null;
    if (NAMES.hasOwnProperty(s) && loose) s = NAMES[s];

    var m = /^#?([0-9a-f]{3,8})$/.exec(s);
    if (m && (s.charAt(0) === "#" || loose) && [3, 4, 6, 8].indexOf(m[1].length) >= 0) {
        var h = m[1];
        if (h.length <= 4)
            h = h.split("").map(function (d) { return d + d; }).join("");
        return {
            r: parseInt(h.substr(0, 2), 16), g: parseInt(h.substr(2, 2), 16),
            b: parseInt(h.substr(4, 2), 16), a: h.length === 8 ? parseInt(h.substr(6, 2), 16) / 255 : 1
        };
    }

    m = /^(rgba?|hsla?)\(\s*([^)]*)\)?$/.exec(s);
    if (m) {
        var p = m[2].split(/[\s,\/]+/).filter(function (x) { return x !== ""; });
        if (p.length < 3) return null;
        var a = p.length > 3 ? (String(p[3]).indexOf("%") >= 0 ? parseFloat(p[3]) / 100 : parseFloat(p[3])) : 1;
        if (isNaN(a)) return null;
        if (m[1].charAt(0) === "r") {
            var r = num(p[0]), g = num(p[1]), b = num(p[2]);
            if (isNaN(r) || isNaN(g) || isNaN(b)) return null;
            return { r: clamp(Math.round(r), 0, 255), g: clamp(Math.round(g), 0, 255), b: clamp(Math.round(b), 0, 255), a: clamp(a, 0, 1) };
        }
        var hh = parseFloat(p[0]), ss = parseFloat(p[1]), ll = parseFloat(p[2]);
        if (isNaN(hh) || isNaN(ss) || isNaN(ll)) return null;
        var rgb = hslToRgb(hh, clamp(ss, 0, 100) / 100, clamp(ll, 0, 100) / 100);
        rgb.a = clamp(a, 0, 1);
        return rgb;
    }
    return null;
}

function hslToRgb(h, s, l) {
    h = ((h % 360) + 360) % 360 / 360;
    function f(n) {
        var k = (n + h * 12) % 12;
        var a = s * Math.min(l, 1 - l);
        return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1));
    }
    return { r: Math.round(f(0) * 255), g: Math.round(f(8) * 255), b: Math.round(f(4) * 255), a: 1 };
}

function rgbToHsl(c) {
    var r = c.r / 255, g = c.g / 255, b = c.b / 255;
    var mx = Math.max(r, g, b), mn = Math.min(r, g, b);
    var l = (mx + mn) / 2, h = 0, s = 0;
    if (mx !== mn) {
        var d = mx - mn;
        s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx === r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (mx === g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h *= 60;
    }
    return { h: Math.round(h), s: Math.round(s * 100), l: Math.round(l * 100) };
}

function hex2(n) { var s = clamp(Math.round(n), 0, 255).toString(16); return s.length < 2 ? "0" + s : s; }

function toHex(c) {
    return "#" + hex2(c.r) + hex2(c.g) + hex2(c.b) + (c.a < 1 ? hex2(c.a * 255) : "");
}

function toRgb(c) {
    var a = Math.round(c.a * 100) / 100;
    return c.a < 1 ? "rgba(" + c.r + ", " + c.g + ", " + c.b + ", " + a + ")" : "rgb(" + c.r + ", " + c.g + ", " + c.b + ")";
}

function toHslString(c) {
    var h = rgbToHsl(c);
    var a = Math.round(c.a * 100) / 100;
    return c.a < 1 ? "hsla(" + h.h + ", " + h.s + "%, " + h.l + "%, " + a + ")" : "hsl(" + h.h + ", " + h.s + "%, " + h.l + "%)";
}

function withLightness(c, l) {
    var h = rgbToHsl(c);
    var o = hslToRgb(h.h, h.s / 100, l / 100);
    o.a = c.a;
    return o;
}

function inkFor(c) {
    return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) > 150 ? "#555473" : "#ffffff";
}

// Windows-native quick actions for Launcher search bar
var ACTIONS = [
    { id: "explorer",    title: "Restore Windows Explorer", sub: "Restore work area and start explorer.exe", glyph: "restart_alt", keys: "explorer restore taskbar emergency shell" },
    { id: "taskmgr",     title: "Task Manager",             sub: "Open Windows Task Manager",                glyph: "memory",      keys: "taskmgr task manager processes cpu" },
    { id: "winsettings", title: "Windows Settings",         sub: "Open ms-settings:",                        glyph: "settings",    keys: "windows settings control panel" },
    { id: "wallpaper",   title: "Change Wallpaper",         sub: "Pick a desktop background image",          glyph: "photo_library", keys: "wallpaper background image" },
    { id: "theme",       title: "Dark / light mode",        sub: "Toggle SekaiPop surface mode",             glyph: "contrast",    keys: "dark light theme mode appearance" },
    { id: "palette",     title: "Cycle Unit Theme",         sub: "Switch SekaiPop character color palette",  glyph: "palette",     keys: "palette color unit theme" },
    { id: "mutespk",     title: "Mute / unmute speakers",   sub: "Toggle default Windows audio output",      glyph: "volume_off",  keys: "mute speaker volume sound audio" },
    { id: "mutemic",     title: "Mute / unmute microphone", sub: "Toggle default Windows audio input",       glyph: "mic_off",     keys: "mute microphone mic input" },
    { id: "exitshell",   title: "Exit SekaiPop Shell",      sub: "Restore Explorer and close SekaiPop",      glyph: "logout",      keys: "exit quit close sekaipop", confirm: true }
];

function matchActions(text) {
    var q = String(text).trim().toLowerCase();
    if (q.length < 2) return [];
    var out = [];
    for (var i = 0; i < ACTIONS.length; i++) {
        var a = ACTIONS[i];
        var title = a.title.toLowerCase();
        var score = 0;
        if (title === q) score = 100;
        else if (title.indexOf(q) === 0) score = 80;
        else if ((" " + a.keys).indexOf(" " + q) >= 0) score = 60;
        else if (title.indexOf(q) >= 0) score = 50;
        else if (a.keys.indexOf(q) >= 0) score = 30;
        if (score > 0) out.push({ score: score, action: a });
    }
    out.sort(function (x, y) { return y.score - x.score; });
    return out;
}

function oneLine(s, max) {
    var t = String(s).replace(/\s+/g, " ").trim();
    return t.length > max ? t.slice(0, max - 1) + "..." : t;
}
