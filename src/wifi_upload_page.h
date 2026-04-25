#pragma once

static const char WIFI_PAGE[] PROGMEM = R"WIFIPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Yottreader Upload</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#f0f0f0;color:#222;padding:16px;max-width:640px;margin:0 auto}
h1{font-size:1.3rem;margin-bottom:6px}
h2{font-size:1rem;margin:16px 0 8px;color:#444}
#space{font-size:.8rem;color:#666;margin-bottom:12px}
.sbar{height:5px;background:#ddd;border-radius:3px;margin-top:3px}
.sf{height:5px;border-radius:3px;transition:width .4s;background:#555}
.sf.warn{background:#c0392b}
#drop{background:#fff;border:2px dashed #bbb;border-radius:8px;padding:28px;text-align:center;cursor:pointer;transition:.15s}
#drop.over{border-color:#444;background:#e8e8e8}
#drop.busy{pointer-events:none;opacity:.55}
#drop svg{display:block;margin:0 auto 8px}
.lbl{font-weight:500}
.sub{font-size:.85rem;color:#888;margin-top:4px}
#drop input{display:none}
#msgs{margin:10px 0}
.msg{padding:7px 11px;border-radius:4px;font-size:.85rem;margin-bottom:4px}
.ok{background:#d4edda;color:#155724}
.er{background:#f8d7da;color:#721c24}
.in{background:#d1ecf1;color:#0c5460}
.brow{display:flex;align-items:center;justify-content:space-between;background:#fff;border-radius:4px;padding:8px 11px;margin-bottom:4px}
.brow span{font-size:.85rem;word-break:break-word;margin-right:8px}
.del{background:#c0392b;color:#fff;border:none;border-radius:3px;padding:3px 9px;cursor:pointer;font-size:.8rem;flex-shrink:0}
.del:hover{background:#a93226}
.empty{color:#888;font-size:.85rem}
#sf{background:#fff;border-radius:8px;padding:2px 12px;margin-bottom:8px}
.srow{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #f0f0f0;gap:8px}
.srow:last-child{border-bottom:none}
.srow label{font-size:.85rem;color:#333}
.srow select{font-size:.85rem;border:1px solid #ccc;border-radius:3px;padding:2px 6px;background:#fff;flex-shrink:0}
#sv-btn{width:100%;padding:9px;background:#333;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:.9rem;margin-bottom:4px}
#sv-btn:hover{background:#111}
#sv-msg{font-size:.82rem;min-height:1.2em;text-align:center}
</style>
</head>
<body>
<h1>Yottreader Upload</h1>
<div id="space">Storage: <span id="sp-txt">loading...</span>
  <div class="sbar"><div id="sp-bar" class="sf" style="width:0%"></div></div>
</div>
<div id="drop">
  <svg width="36" height="36" viewBox="0 0 24 24" fill="none" stroke="#aaa" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/>
    <polyline points="17 8 12 3 7 8"/>
    <line x1="12" y1="3" x2="12" y2="15"/>
  </svg>
  <div class="lbl">Drop .epub files here</div>
  <div class="sub">or click to select</div>
  <input id="fi" type="file" accept=".epub" multiple>
</div>
<div id="msgs"></div>
<h2>Books on device</h2>
<div id="books"><div class="empty">Loading...</div></div>
<h2>Settings</h2>
<div id="sf">
  <div class="srow"><label>Font size</label><select id="s-fontSz"><option value="0">small</option><option value="1">medium</option><option value="2">large</option></select></div>
  <div class="srow"><label>Font</label><select id="s-fontFam"><option value="0">sans</option><option value="1">sans bold</option><option value="2">serif</option><option value="3">serif bold</option></select></div>
  <div class="srow"><label>Hyphenation</label><select id="s-hyphen"><option value="0">off</option><option value="1">on</option></select></div>
  <div class="srow"><label>Display</label><select id="s-display"><option value="0">light</option><option value="1">dark</option></select></div>
  <div class="srow"><label>Orientation</label><select id="s-orient"><option value="0">normal</option><option value="1">flipped</option></select></div>
  <div class="srow"><label>Full refresh every</label><select id="s-refresh"><option value="5">5 pages</option><option value="10">10 pages</option><option value="20">20 pages</option><option value="50">50 pages</option></select></div>
  <div class="srow"><label>Stats bar</label><select id="s-stats"><option value="0">off</option><option value="1">chapter</option><option value="2">book</option></select></div>
  <div class="srow"><label>Deep sleep</label><select id="s-sleep"><option value="0">never</option><option value="1">2 min</option><option value="2">5 min</option><option value="3">10 min</option><option value="4">15 min</option><option value="5">30 min</option></select></div>
</div>
<button id="sv-btn">Save Settings</button>
<div id="sv-msg"></div>

<script>
var storageFree = Infinity;

// ── ZIP / epub parser ─────────────────────────────────────────────────────────
function Zip(ab) {
  this.b = new Uint8Array(ab);
  this.v = new DataView(ab);
  this.e = {};
  var eocd = -1;
  for (var i = this.b.length - 22; i >= 0; i--)
    if (this.v.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error('Not a valid ZIP/epub');
  var p = this.v.getUint32(eocd+16, true), end = p + this.v.getUint32(eocd+12, true);
  while (p < end) {
    if (this.v.getUint32(p, true) !== 0x02014b50) break;
    var comp = this.v.getUint16(p+10, true), csz = this.v.getUint32(p+20, true);
    var fnl  = this.v.getUint16(p+28, true), exl = this.v.getUint16(p+30, true);
    var cml  = this.v.getUint16(p+32, true), off = this.v.getUint32(p+42, true);
    var name = new TextDecoder().decode(this.b.slice(p+46, p+46+fnl));
    this.e[name] = {comp:comp, csz:csz, off:off};
    p += 46 + fnl + exl + cml;
  }
}
Zip.prototype.text = async function(name) {
  var e = this.e[name]; if (!e) return null;
  var lp = e.off, skip = 30 + this.v.getUint16(lp+26,true) + this.v.getUint16(lp+28,true);
  var data = this.b.slice(lp+skip, lp+skip+e.csz);
  if (e.comp === 0) return new TextDecoder().decode(data);
  if (e.comp !== 8) throw new Error('Unsupported compression: '+e.comp);
  var ds = new DecompressionStream('deflate-raw');
  var w = ds.writable.getWriter(); w.write(data); w.close();
  var chunks = [], r = ds.readable.getReader();
  for (;;) { var x = await r.read(); if (x.done) break; chunks.push(x.value); }
  var tot = 0; for (var i=0;i<chunks.length;i++) tot+=chunks[i].length;
  var out = new Uint8Array(tot), off = 0;
  for (var i=0;i<chunks.length;i++) { out.set(chunks[i], off); off+=chunks[i].length; }
  return new TextDecoder().decode(out);
};

var BLOCK_TAGS = {p:1,div:1,h1:1,h2:1,h3:1,h4:1,h5:1,h6:1,li:1,blockquote:1,tr:1,td:1,th:1,section:1,article:1,header:1,footer:1};
function extractText(node) {
  var out = [];
  function walk(n) {
    if (n.nodeType === 3) {
      var t = n.textContent.replace(/\s+/g, ' ');
      if (t.trim()) {
        if (!out.length || out[out.length-1]==='') out.push(t.trim());
        else out[out.length-1] += ' ' + t.trim();
      }
    } else if (n.nodeType === 1) {
      var tag = n.tagName.toLowerCase();
      if (tag==='script'||tag==='style'||tag==='head'||tag==='nav') return;
      var blk = !!BLOCK_TAGS[tag];
      if (blk && out.length && out[out.length-1]!=='') out.push('');
      for (var i=0;i<n.childNodes.length;i++) walk(n.childNodes[i]);
      if (blk && out.length && out[out.length-1]!=='') out.push('');
    }
  }
  walk(node);
  var res = [];
  for (var i=0;i<out.length;i++) {
    if (out[i]==='' && (!res.length || res[res.length-1]==='')) continue;
    res.push(out[i]);
  }
  while (res.length && res[res.length-1]==='') res.pop();
  return res;
}
function dcMeta(opf, localName) {
  var ns = opf.getElementsByTagNameNS('http://purl.org/dc/elements/1.1/', localName);
  if (ns.length) return ns[0].textContent.trim();
  var plain = opf.getElementsByTagName(localName);
  return plain.length ? plain[0].textContent.trim() : '';
}
async function epubToBook(file) {
  var ab = await file.arrayBuffer();
  var zip = new Zip(ab), xp = new DOMParser();
  var cont = await zip.text('META-INF/container.xml');
  if (!cont) throw new Error('Missing container.xml');
  var opfPath = (cont.match(/full-path="([^"]+)"/) || [])[1];
  if (!opfPath) throw new Error('Cannot find OPF path');
  var opfDir = opfPath.indexOf('/') >= 0 ? opfPath.slice(0, opfPath.lastIndexOf('/')+1) : '';
  var opfTxt = await zip.text(opfPath);
  if (!opfTxt) throw new Error('Cannot read OPF');
  var opf = xp.parseFromString(opfTxt, 'text/xml');
  var title  = dcMeta(opf,'title')   || file.name.replace(/\.epub$/i,'');
  var author = dcMeta(opf,'creator') || 'Unknown';
  var manifest = {};
  var items = opf.querySelectorAll('item');
  for (var i=0;i<items.length;i++) {
    var id = items[i].getAttribute('id'), href = items[i].getAttribute('href');
    if (id && href) manifest[id] = href;
  }
  var idrefs = [], refs = opf.querySelectorAll('itemref');
  for (var i=0;i<refs.length;i++) { var r = refs[i].getAttribute('idref'); if (r) idrefs.push(r); }
  var book = '::TITLE::\n'+title+'\n::AUTHOR::\n'+author+'\n';
  var chapters = 0;
  for (var i=0;i<idrefs.length;i++) {
    var href = manifest[idrefs[i]]; if (!href) continue;
    href = href.split('#')[0];
    var txt = await zip.text(opfDir+href); if (!txt) txt = await zip.text(href);
    if (!txt) continue;
    var doc = xp.parseFromString(txt, 'text/html');
    var paras = extractText(doc.body || doc.documentElement);
    if (!paras.length) continue;
    book += '::CHAPTER::\n::TEXT::\n'+paras.join('\n')+'\n';
    chapters++;
  }
  if (!chapters) throw new Error('No readable text found');
  var fname = title.replace(/[^\w\s-]/g,'').trim().replace(/\s+/g,'_').slice(0,50)+'.book';
  return {book:book, fname:fname, title:title};
}

// ── Upload ────────────────────────────────────────────────────────────────────
function uploadBook(book, fname, onProgress) {
  return new Promise(function(resolve, reject) {
    var fd = new FormData();
    fd.append('file', new Blob([book], {type:'text/plain'}), fname);
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/upload');
    xhr.upload.onprogress = function(e) { if (e.lengthComputable && onProgress) onProgress(e.loaded/e.total); };
    xhr.onload  = function() { xhr.status < 300 ? resolve() : reject(new Error(xhr.responseText || 'Error '+xhr.status)); };
    xhr.onerror = function() { reject(new Error('Network error')); };
    xhr.send(fd);
  });
}

// ── Storage ───────────────────────────────────────────────────────────────────
function loadSpace() {
  return fetch('/space?_='+Date.now()).then(function(r){ return r.json(); }).then(function(d) {
    var free = d.total - d.used, pct = Math.round(d.used/d.total*100);
    storageFree = free;
    document.getElementById('sp-txt').textContent =
      Math.round(free/1024)+' KB free of '+Math.round(d.total/1024)+' KB';
    var bar = document.getElementById('sp-bar');
    bar.style.width = pct+'%';
    bar.className = 'sf'+(pct>=80?' warn':'');
  }).catch(function(){ document.getElementById('sp-txt').textContent = 'unknown'; });
}

// ── Book list ─────────────────────────────────────────────────────────────────
function loadBooks() {
  return fetch('/books?_='+Date.now()).then(function(r){ return r.json(); }).then(function(list) {
    var el = document.getElementById('books');
    el.innerHTML = '';
    if (!list.length) { el.innerHTML = '<div class="empty">No books yet.</div>'; return; }
    list.forEach(function(path) {
      var row = document.createElement('div'); row.className = 'brow';
      var lbl = document.createElement('span');
      lbl.textContent = path.replace(/^\//,'').replace(/\.book$/,'');
      var btn = document.createElement('button'); btn.className = 'del'; btn.textContent = 'Delete';
      btn.onclick = function() {
        btn.disabled = true;
        fetch('/delete?name='+encodeURIComponent(path), {method:'DELETE'})
          .then(function(){ return Promise.all([loadBooks(), loadSpace()]); });
      };
      row.appendChild(lbl); row.appendChild(btn);
      el.appendChild(row);
    });
  }).catch(function(){
    document.getElementById('books').innerHTML = '<div class="empty">Could not load list.</div>';
  });
}

// ── Messages ──────────────────────────────────────────────────────────────────
function addMsg(text, cls) {
  var d = document.createElement('div');
  d.className = 'msg '+cls; d.textContent = text;
  document.getElementById('msgs').prepend(d);
  setTimeout(function(){ d.remove(); }, 9000);
}

// ── Process files: convert then upload ───────────────────────────────────────
function processFiles(files) {
  var drop = document.getElementById('drop');
  drop.classList.add('busy');
  var chain = Promise.resolve();
  for (var i = 0; i < files.length; i++) {
    (function(file) {
      chain = chain.then(function() {
        if (!file.name.toLowerCase().endsWith('.epub')) {
          addMsg('Skipped (not .epub): '+file.name, 'er');
          return;
        }
        var status = document.createElement('div');
        status.className = 'msg in';
        status.textContent = 'Converting: '+file.name+' ...';
        document.getElementById('msgs').prepend(status);
        return epubToBook(file).then(function(result) {
          var bookBytes = new Blob([result.book]).size;
          if (bookBytes > storageFree - 32768) {
            status.className = 'msg er';
            status.textContent = 'Too large: '+result.title+
              ' needs '+Math.round(bookBytes/1024)+' KB, only '+
              Math.round((storageFree-32768)/1024)+' KB free. Delete a book first.';
            setTimeout(function(){ status.remove(); }, 12000);
            return;
          }
          status.textContent = 'Uploading: '+result.title+' ... 0%';
          return uploadBook(result.book, result.fname, function(p) {
            status.textContent = 'Uploading: '+result.title+' ... '+Math.round(p*100)+'%';
          }).then(function() {
            status.className = 'msg ok';
            status.textContent = 'Done: '+result.title;
            setTimeout(function(){ status.remove(); }, 9000);
            return Promise.all([loadBooks(), loadSpace()]);
          });
        }).catch(function(e) {
          status.className = 'msg er';
          status.textContent = 'Failed: '+file.name+' - '+e.message;
          setTimeout(function(){ status.remove(); }, 9000);
          return loadSpace();
        });
      });
    })(files[i]);
  }
  chain.then(function(){ drop.classList.remove('busy'); });
}

// ── Events ────────────────────────────────────────────────────────────────────
var drop = document.getElementById('drop');
var fi   = document.getElementById('fi');
drop.addEventListener('click', function() {
  if (!drop.classList.contains('busy')) fi.click();
});
fi.addEventListener('change', function() {
  if (fi.files.length) processFiles(fi.files);
  fi.value = '';
});
drop.addEventListener('dragover',  function(e){ e.preventDefault(); drop.classList.add('over'); });
drop.addEventListener('dragleave', function(){ drop.classList.remove('over'); });
drop.addEventListener('drop', function(e){
  e.preventDefault(); drop.classList.remove('over');
  processFiles(e.dataTransfer.files);
});

// ── Settings ──────────────────────────────────────────────────────────────────
function loadSettings() {
  fetch('/settings?_='+Date.now())
    .then(function(r){ return r.json(); })
    .then(function(d) {
      document.getElementById('s-fontSz').value  = d.fontSz;
      document.getElementById('s-fontFam').value = d.fontFam;
      document.getElementById('s-hyphen').value  = d.hyphen;
      document.getElementById('s-display').value = d.display;
      document.getElementById('s-orient').value  = d.orient;
      document.getElementById('s-refresh').value = d.refresh;
      document.getElementById('s-stats').value   = d.stats;
      document.getElementById('s-sleep').value   = d.sleep;
    })
    .catch(function(){});
}

function saveSettings() {
  var body = 'fontSz='  + document.getElementById('s-fontSz').value
    + '&fontFam=' + document.getElementById('s-fontFam').value
    + '&hyphen='  + document.getElementById('s-hyphen').value
    + '&display=' + document.getElementById('s-display').value
    + '&orient='  + document.getElementById('s-orient').value
    + '&refresh=' + document.getElementById('s-refresh').value
    + '&stats='   + document.getElementById('s-stats').value
    + '&sleep='   + document.getElementById('s-sleep').value;
  var msg = document.getElementById('sv-msg');
  fetch('/settings', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:body})
    .then(function(r) {
      if (r.ok) { msg.style.color='#155724'; msg.textContent='Saved.'; }
      else      { msg.style.color='#721c24'; msg.textContent='Error saving.'; }
      setTimeout(function(){ msg.textContent=''; }, 3000);
    })
    .catch(function() {
      msg.style.color='#721c24'; msg.textContent='Error saving.';
      setTimeout(function(){ msg.textContent=''; }, 3000);
    });
}

document.getElementById('sv-btn').addEventListener('click', saveSettings);

loadBooks();
loadSpace();
loadSettings();
</script>
</body>
</html>
)WIFIPAGE";
