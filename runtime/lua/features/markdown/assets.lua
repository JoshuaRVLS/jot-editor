-- Preview page assets: the built-in stylesheet, the CDN head tags, and the
-- in-page JavaScript client (live content, scroll sync, copy buttons, theme
-- toggle, and the optional diagram/math renderers).
local M = {}

local CDN = {
  highlight_js = "https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11.10.0/build/highlight.min.js",
  highlight_css_dark = "https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11.10.0/build/styles/github-dark.min.css",
  highlight_css_light = "https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11.10.0/build/styles/github.min.css",
  katex_css = "https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css",
  katex_js = "https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js",
  katex_auto = "https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/contrib/auto-render.min.js",
  mermaid_js = "https://cdn.jsdelivr.net/npm/mermaid@11.4.0/dist/mermaid.min.js",
  raphael_js = "https://cdn.jsdelivr.net/npm/raphael@2.3.0/raphael.min.js",
  flowchart_js = "https://cdn.jsdelivr.net/npm/flowchart.js@1.18.0/dist/flowchart.min.js",
  pako_js = "https://cdn.jsdelivr.net/npm/pako@2.1.0/dist/pako.min.js",
  echarts_js = "https://cdn.jsdelivr.net/npm/echarts@5.5.1/dist/echarts.min.js",
  vega_js = "https://cdn.jsdelivr.net/npm/vega@5",
  vega_lite_js = "https://cdn.jsdelivr.net/npm/vega-lite@5",
  vega_embed_js = "https://cdn.jsdelivr.net/npm/vega-embed@6",
}

-- ── stylesheet ──────────────────────────────────────────────────────────────
M.CSS = [[
:root { --fg:#1f2328; --bg:#ffffff; --muted:#59636e; --border:#d1d9e0;
        --code-bg:#f6f8fa; --link:#0969da; --quote:#59636e; --accent:#0969da; }
body.theme-dark { --fg:#e6edf3; --bg:#0d1117; --muted:#9198a1; --border:#3d444d;
        --code-bg:#151b23; --link:#4493f8; --quote:#9198a1; --accent:#4493f8; }
* { box-sizing: border-box; }
html, body { margin:0; padding:0; height:100%; }
body { background:var(--bg); color:var(--fg); display:flex; height:100vh;
       font:16px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif; }
#toc { width:264px; flex:0 0 264px; overflow:auto; padding:24px 12px 48px 24px;
       border-right:1px solid var(--border); font-size:14px; display:none; }
#toc.visible { display:block; }
#toc ul { list-style:none; margin:0; padding-left:14px; }
#toc > ul { padding-left:0; }
#toc a { color:var(--muted); text-decoration:none; display:block; padding:2px 0; }
#toc a:hover { color:var(--accent); }
#main { flex:1 1 auto; overflow:auto; scroll-behavior:auto; }
#content { max-width:980px; margin:0 auto; padding:32px 40px 160px; }
#toolbar { position:fixed; top:12px; right:14px; display:flex; gap:6px; z-index:20; }
#toolbar button { background:transparent; color:var(--muted); border:1px solid var(--border);
       border-radius:6px; padding:4px 9px; font-size:12px; cursor:pointer; }
#toolbar button:hover { color:var(--fg); border-color:var(--accent); }
#toolbar button.active { color:var(--accent); border-color:var(--accent); }
#status { position:fixed; left:14px; bottom:12px; color:var(--muted); font-size:12px; z-index:20; }
h1,h2,h3,h4,h5,h6 { line-height:1.25; margin:24px 0 16px; font-weight:600; }
h1 { font-size:2em; border-bottom:1px solid var(--border); padding-bottom:.3em; }
h2 { font-size:1.5em; border-bottom:1px solid var(--border); padding-bottom:.3em; }
h3 { font-size:1.25em; } h4 { font-size:1em; } h5 { font-size:.875em; } h6 { font-size:.85em; color:var(--muted); }
p { margin:0 0 16px; }
a { color:var(--link); text-decoration:none; } a:hover { text-decoration:underline; }
code { background:var(--code-bg); border-radius:6px; padding:.2em .4em; font-size:85%;
       font-family:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace; }
pre { background:var(--code-bg); border-radius:8px; padding:14px 16px; overflow:auto; margin:0 0 16px; }
pre code { background:transparent; padding:0; font-size:85%; line-height:1.45; }
.code-block { position:relative; }
.code-block .copy-code { position:absolute; top:8px; right:8px;
       opacity:0; transition:opacity .15s; background:var(--bg); color:var(--muted);
       border:1px solid var(--border); border-radius:6px; padding:2px 8px; font-size:11px;
       cursor:pointer; }
.code-block:hover .copy-code { opacity:1; }
.code-block .copy-code.done { color:var(--accent); border-color:var(--accent); }
blockquote { margin:0 0 16px; padding:0 1em; color:var(--quote); border-left:.25em solid var(--border); }
blockquote.alert { border-left-width:4px; border-radius:6px; padding:10px 16px; }
blockquote.alert .alert-title { font-weight:600; margin:0 0 6px; }
.alert-note { border-left-color:#4493f8; } .alert-tip { border-left-color:#3fb950; }
.alert-important { border-left-color:#a371f7; } .alert-warning { border-left-color:#d29922; }
.alert-caution { border-left-color:#f85149; }
ul,ol { margin:0 0 16px; padding-left:2em; }
li > ul, li > ol { margin-bottom:0; }
li { margin:.2em 0; }
li input[type=checkbox] { margin-right:.4em; }
.task-item, li > input[type=checkbox] { list-style:none; }
table { border-collapse:collapse; margin:0 0 16px; width:max-content; max-width:100%; }
.table-wrap { overflow:auto; margin:0 0 16px; }
.table-wrap table { margin:0; }
th,td { border:1px solid var(--border); padding:6px 13px; }
th { background:var(--code-bg); font-weight:600; }
tr:nth-child(2n) td { background:color-mix(in srgb, var(--code-bg) 45%, transparent); }
img { max-width:100%; }
hr { height:.25em; border:0; background:var(--border); margin:24px 0; }
dl { margin:0 0 16px; } dt { font-weight:600; } dd { margin:0 0 8px 1.5em; }
mark { background:#fff8c5; color:#1f2328; }
del { color:var(--muted); }
.footnotes { margin-top:40px; border-top:1px solid var(--border); padding-top:16px; font-size:14px; }
.footnotes ol { padding-left:1.4em; }
.footnote-backref { margin-left:.4em; }
.mermaid, .flowchart, .plantuml, .echarts, .vega { margin:0 0 16px; text-align:center; }
.mermaid svg, .flowchart svg { max-width:100%; }
.plantuml img { max-width:100%; }
]]

-- ── in-page client ──────────────────────────────────────────────────────────
-- Kept as one IIFE: EventSource for live content, throttled scroll reporting,
-- content versioning so a stale update never clobbers a newer one.
M.CLIENT_JS = [[
(function () {
  var content = document.getElementById('content');
  var main = document.getElementById('main');
  var status = document.getElementById('status');
  var toolbar = document.getElementById('toolbar');
  var version = 0;
  var syncEnabled = true;
  var suppressReport = false;
  var pendingReport = null;

  function topLine() {
    var nodes = content.querySelectorAll('[data-line]');
    var top = main.getBoundingClientRect().top;
    var best = null;
    for (var i = 0; i < nodes.length; i++) {
      var rect = nodes[i].getBoundingClientRect();
      if (rect.bottom >= top) { best = nodes[i]; break; }
      best = nodes[i];
    }
    return best ? parseInt(best.getAttribute('data-line'), 10) : 1;
  }

  function scrollToLine(line) {
    var nodes = content.querySelectorAll('[data-line]');
    var target = null;
    for (var i = 0; i < nodes.length; i++) {
      var value = parseInt(nodes[i].getAttribute('data-line'), 10);
      if (!isNaN(value) && value <= line) { target = nodes[i]; } else if (target) { break; }
    }
    if (!target) { return; }
    suppressReport = true;
    main.scrollTop = target.offsetTop - 8;
    setTimeout(function () { suppressReport = false; }, 60);
  }

  function report() {
    if (!syncEnabled || suppressReport) { return; }
    var line = topLine();
    if (pendingReport === line) { return; }
    pendingReport = line;
    try {
      fetch('/sync', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ line: line })
      });
    } catch (e) { /* offline: the next event retries */ }
  }

  var rafPending = false;
  main.addEventListener('scroll', function () {
    if (rafPending) { return; }
    rafPending = true;
    requestAnimationFrame(function () { rafPending = false; report(); });
  });

  function typeset() {
    if (window.hljs && window.hljs.highlightElement) {
      var blocks = content.querySelectorAll('pre code');
      for (var i = 0; i < blocks.length; i++) {
        try { window.hljs.highlightElement(blocks[i]); } catch (e) { /* keep going */ }
      }
    }
    if (window.renderMathInElement) {
      try {
        window.renderMathInElement(content, {
          delimiters: [
            { left: '$$', right: '$$', display: true },
            { left: '$', right: '$', display: false }
          ],
          throwOnError: false
        });
      } catch (e) { /* keep going */ }
    }
    if (window.mermaid) {
      var diagrams = content.querySelectorAll('.mermaid');
      if (diagrams.length) {
        try {
          window.mermaid.run({ nodes: diagrams, suppressErrors: true });
        } catch (e) { /* keep going */ }
      }
    }
    if (window.flowchart) {
      var flows = content.querySelectorAll('.flowchart');
      for (var f = 0; f < flows.length; f++) {
        var node = flows[f];
        if (node.getAttribute('data-drawn')) { continue; }
        node.setAttribute('data-drawn', '1');
        try {
          var chart = window.flowchart.parse(node.textContent);
          chart.drawSVG(node, { 'line-color': 'var(--muted)' });
        } catch (e) { /* keep going */ }
      }
    }
    if (window.pako) {
      var plants = content.querySelectorAll('.plantuml');
      for (var p = 0; p < plants.length; p++) {
        var plant = plants[p];
        if (plant.getAttribute('data-drawn')) { continue; }
        plant.setAttribute('data-drawn', '1');
        try {
          var raw = window.pako.deflateRaw(plant.textContent, { level: 9 });
          var text = '';
          for (var b = 0; b < raw.length; b++) { text += String.fromCharCode(raw[b]); }
          var image = document.createElement('img');
          image.src = 'https://www.plantuml.com/plantuml/svg/' + encode64(text);
          plant.textContent = '';
          plant.appendChild(image);
        } catch (e) { /* keep going */ }
      }
    }
    if (window.echarts) {
      var charts = content.querySelectorAll('.echarts');
      for (var c = 0; c < charts.length; c++) {
        var host = charts[c];
        if (host.getAttribute('data-drawn')) { continue; }
        host.setAttribute('data-drawn', '1');
        try {
          host.style.height = '420px';
          var instance = window.echarts.init(host, document.body.classList.contains('theme-dark') ? 'dark' : null);
          instance.setOption(JSON.parse(host.textContent));
        } catch (e) { /* keep going */ }
      }
    }
    if (window.vegaEmbed) {
      var vegas = content.querySelectorAll('.vega');
      for (var v = 0; v < vegas.length; v++) {
        var item = vegas[v];
        if (item.getAttribute('data-drawn')) { continue; }
        item.setAttribute('data-drawn', '1');
        try {
          var spec = JSON.parse(item.textContent);
          var lite = item.getAttribute('data-vega-lite') === '1';
          window.vegaEmbed(item, spec, { actions: false, mode: 'vega' })
            .catch(function () { if (lite) { window.vegaEmbed(item, spec); } });
        } catch (e) { /* keep going */ }
      }
    }
  }

  function encode6bit(b) {
    if (b < 10) { return String.fromCharCode(48 + b); }
    b -= 10;
    if (b < 26) { return String.fromCharCode(65 + b); }
    b -= 26;
    if (b < 26) { return String.fromCharCode(97 + b); }
    b -= 26;
    if (b === 0) { return '-'; }
    if (b === 1) { return '_'; }
    return '?';
  }

  function append3bytes(b1, b2, b3) {
    var c1 = b1 >> 2;
    var c2 = ((b1 & 0x3) << 4) | (b2 >> 4);
    var c3 = ((b2 & 0xF) << 2) | (b3 >> 6);
    var c4 = b3 & 0x3F;
    return encode6bit(c1 & 0x3F) + encode6bit(c2 & 0x3F) + encode6bit(c3 & 0x3F) + encode6bit(c4 & 0x3F);
  }

  function encode64(data) {
    var result = '';
    for (var i = 0; i < data.length; i += 3) {
      if (i + 2 === data.length) { result += append3bytes(data.charCodeAt(i), data.charCodeAt(i + 1), 0); }
      else if (i + 1 === data.length) { result += append3bytes(data.charCodeAt(i), 0, 0); }
      else { result += append3bytes(data.charCodeAt(i), data.charCodeAt(i + 1), data.charCodeAt(i + 2)); }
    }
    return result;
  }

  function setStatus(text) {
    if (!status) { return; }
    status.textContent = text;
    status.style.opacity = '1';
    clearTimeout(setStatus.timer);
    setStatus.timer = setTimeout(function () { status.style.opacity = '0.45'; }, 1800);
  }

  content.addEventListener('click', function (event) {
    var button = event.target.closest ? event.target.closest('.copy-code') : null;
    if (!button) { return; }
    var block = button.parentElement.querySelector('pre');
    if (!block) { return; }
    var text = block.innerText;
    var done = function () {
      button.textContent = 'copied';
      button.classList.add('done');
      setTimeout(function () { button.textContent = 'copy'; button.classList.remove('done'); }, 1400);
    };
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done, done);
    } else {
      var area = document.createElement('textarea');
      area.value = text;
      document.body.appendChild(area);
      area.select();
      try { document.execCommand('copy'); } catch (e) { /* ignore */ }
      document.body.removeChild(area);
      done();
    }
  });

  if (toolbar) {
    var themeButton = toolbar.querySelector('[data-action=theme]');
    var syncButton = toolbar.querySelector('[data-action=sync]');
    if (themeButton) {
      themeButton.addEventListener('click', function () {
        var body = document.body;
        var dark = body.classList.contains('theme-dark');
        body.classList.remove('theme-dark', 'theme-light');
        body.classList.add(dark ? 'theme-light' : 'theme-dark');
        themeButton.textContent = dark ? 'dark' : 'light';
      });
    }
    if (syncButton) {
      syncButton.classList.add('active');
      syncButton.addEventListener('click', function () {
        syncEnabled = !syncEnabled;
        syncButton.classList.toggle('active', syncEnabled);
        setStatus(syncEnabled ? 'scroll sync on' : 'scroll sync off');
      });
    }
  }

  var source = new EventSource('/events');
  var firstContent = true;
  source.addEventListener('content', function (event) {
    if (firstContent) {
      firstContent = false;
      // The page already embeds the current body; only adopt the streamed copy
      // when the embedded one is missing (shell served before first render).
      if (content.innerHTML.trim() !== '') {
        setStatus('connected');
        return;
      }
    }
    version += 1;
    var anchor = topLine();
    content.innerHTML = event.data;
    typeset();
    scrollToLine(anchor);
    setStatus('updated');
  });
  source.addEventListener('title', function (event) {
    if (event.data) { document.title = event.data; }
  });
  source.addEventListener('sync', function (event) {
    var line = parseInt(event.data, 10);
    if (!isNaN(line)) { scrollToLine(line); }
  });
  source.addEventListener('scroll', function (event) {
    var line = parseInt(event.data, 10);
    if (!isNaN(line)) { scrollToLine(line); }
  });
  source.onerror = function () { setStatus('reconnecting…'); };
  source.onopen = function () { setStatus('connected'); };
})();
]]

-- Head tags for the CDN libraries the enabled options need.
function M.head_tags(options, theme)
  local tags = {}
  local function script(url)
    tags[#tags + 1] = string.format('<script src="%s"></script>', url)
  end
  local function style(url)
    tags[#tags + 1] = string.format('<link rel="stylesheet" href="%s">', url)
  end

  if options.katex then
    style(CDN.katex_css)
    script(CDN.katex_js)
    script(CDN.katex_auto)
  end
  if options.highlightjs then
    style(theme == "light" and CDN.highlight_css_light or CDN.highlight_css_dark)
    script(CDN.highlight_js)
  end
  if options.mermaid then
    script(CDN.mermaid_js)
  end
  if options.flowchart then
    script(CDN.raphael_js)
    script(CDN.flowchart_js)
  end
  if options.plantuml then
    script(CDN.pako_js)
  end
  if options.echarts then
    script(CDN.echarts_js)
  end
  if options.vega then
    script(CDN.vega_js)
    script(CDN.vega_lite_js)
    script(CDN.vega_embed_js)
  end
  return table.concat(tags, "\n  ")
end

return M
