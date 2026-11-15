#pragma once

const char* page = R"HTML(<!doctype html>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Phone control panel</title>
<style>body{font:20px sans-serif;max-width:32em;margin:2em auto;padding:1em}
input[type=range]{width:100%}pre{height:16em;overflow:auto;background:#eee;white-space:pre-wrap}</style>
<h1>Phone control panel</h1>
<div id="radios"><label><input type="radio" name="mode" value="0" checked>Red</label>
<label><input type="radio" name="mode" value="1">Green</label>
<label><input type="radio" name="mode" value="2">Blue</label></div>
<p><label>Level <input id="fader" type="range" min="0" max="1" step="0.01" value="0.5"></label></p>
<p id="status">Ready</p><pre id="log"></pre>
<script>
const token = new URLSearchParams(location.search).get('token');
const endpoint = path => path + '?token=' + encodeURIComponent(token || '');
async function change() {
  const radio = document.querySelector('input[name=mode]:checked').value;
  try { const r = await fetch(endpoint('/value'), {method:'POST',body:radio+' '+document.querySelector('#fader').value});
    document.querySelector('#status').textContent = r.ok ? 'Sent' : 'HTTP ' + r.status;
  } catch { document.querySelector('#status').textContent = 'Disconnected'; }
}
document.querySelectorAll('input').forEach(input => input.onchange = change);
setInterval(async () => {
  try { const r = await fetch(endpoint('/log')); if (!r.ok) return;
    const box = document.querySelector('#log'); box.textContent = await r.text(); box.scrollTop = box.scrollHeight;
  } catch { document.querySelector('#status').textContent = 'Disconnected'; }
}, 1000);
</script>)HTML";
