/* Prusa-Touch — web interface: settings, live status, and firmware OTA. */
#include "web.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "pandatouch_display.h"   /* pt_get_panel */
#include "cJSON.h"

#include "pandaprusa.h"
#include "app_state.h"
#include "printer_store.h"
#include "wifi.h"
#include "ota_update.h"
#include "ui.h"
#include "wc_test_jpg.h"   /* embedded camera frame for the /api/test/webcam decode self-test */
#include "prusa_connect.h"
#include "bambu_cloud.h"
#include "netlog.h"
#include "prefs.h"
#include "i18n.h"
#include "skin.h"
#include "layout.h"
#include "mbedtls/base64.h"

static const char *TAG = "web";

/* ---- Prusa-themed single-page UI ---- */
static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Prusa Connect Touch</title><style>"
":root{--o:#FD5000}*{box-sizing:border-box;font-family:system-ui,Arial}"
"body{margin:0;background:#1c1e21;color:#f2f2f2}"
"header{background:#111316;color:#fff;padding:14px 18px;font-size:20px;font-weight:700;border-bottom:2px solid var(--o)}"
"nav{display:flex;background:#111316;border-bottom:1px solid #3a3a3a}"
"nav a{padding:12px 18px;color:#bbb;cursor:pointer;text-decoration:none}"
"nav a.on{color:var(--o);border-bottom:2px solid var(--o)}"
".tab{display:none;padding:18px;max-width:800px;margin:0 auto}.tab.on{display:block}"
".card{background:#2a2a2a;border-radius:6px;margin:12px 0;overflow:hidden;border:0}"
".c-head{height:34px;display:flex;align-items:center;padding-left:12px;font-size:18px;font-weight:600;color:#fff}"
".c-badge{margin-left:auto;height:34px;padding:0 12px;display:flex;align-items:center;font-size:14px;font-weight:700}"
".c-body{padding:12px}.c-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}"
".c-cell b{display:block;font-size:10px;color:#a7a7a7;margin-bottom:2px}"
".c-cell div{font-size:16px;color:#fff}"
".green .c-head{background:#303D2D}.green .c-badge{background:#435239}"
".olive .c-head{background:#2E3731}.olive .c-badge{background:#404B3F}"
".gray .c-head{background:#323336}.gray .c-badge{background:#454545}"
".orange .c-head{background:#3D312B}.orange .c-badge{background:#554237}"
".blue .c-head{background:#2B333D}.blue .c-badge{background:#3C444F}"
".yellow .c-head{background:#3E3B2D}.yellow .c-badge{background:#564F39}"
".red .c-head{background:#3D2C2A}.red .c-badge{background:#553B35}"
"input{font-size:15px;padding:9px 10px;border-radius:6px;border:1px solid #4e4e4e;background:#2a2a2a;color:#f2f2f2;margin:4px 0;width:100%}"
/* Connect-style buttons: ghost by default (transparent + thin border), compact + inline.
   Orange solid is reserved for the primary action via class p. */
"button{font-size:14px;padding:7px 14px;border-radius:6px;border:1px solid #4e4e4e;background:transparent;color:#f2f2f2;font-weight:600;cursor:pointer;margin:3px 6px 3px 0;width:auto}"
"button:hover{background:#333;border-color:#6a6a6a}"
"button.p{background:var(--o);color:#fff;border-color:var(--o)}button.p:hover{background:#e84a00}"
".ctlp{border-top:1px solid #3a3a3a;margin-top:12px;padding-top:10px}.ctlp .lbl{font-size:11px;color:#a7a7a7;letter-spacing:.05em;margin:2px 0 4px}"
".bar{height:10px;background:#4e4e4e;border-radius:5px;overflow:hidden;margin-top:8px}.bar>i{display:block;height:100%;background:var(--o)}"
".muted{color:#a7a7a7}"
/* Add-a-printer type picker: Connect-style tiles. */
".ptgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}"
".ptile{background:#222;border:1px solid #4e4e4e;border-radius:8px;padding:13px 14px;cursor:pointer;transition:border-color .12s,background .12s}"
".ptile:hover{border-color:var(--o);background:#262626}"
".ptile b{display:block;font-size:15px;color:#fff;margin-bottom:3px}"
".ptile small{font-size:12px;color:#a7a7a7;line-height:1.35}"
".ptile.soon{opacity:.45;cursor:default}.ptile.soon:hover{border-color:#4e4e4e;background:#222}"
".ptag{float:right;font-size:10px;font-weight:700;color:var(--o);border:1px solid var(--o);border-radius:4px;padding:1px 5px}"
".ptag.s{color:#a7a7a7;border-color:#4e4e4e}"
".ptback{float:right;color:var(--o);cursor:pointer;font-size:13px;font-weight:600}"
".pthead{font-size:12px;font-weight:700;letter-spacing:.04em;text-transform:uppercase;color:#a7a7a7;margin:16px 0 2px}"
".pthead small{font-weight:400;text-transform:none;letter-spacing:0}"
"@media(max-width:560px){.ptgrid{grid-template-columns:1fr}}</style></head><body>"
"<header>PRUSA CONNECT TOUCH</header>"
"<nav><a class=on onclick=\"t(0)\">Status</a><a onclick=\"t(1)\">Printers</a>"
"<a onclick=\"t(2)\">Wi-Fi</a><a onclick=\"t(5)\">Account</a><a onclick=\"t(6)\">Farm</a><a onclick=\"t(3)\">Firmware</a><a onclick=\"t(4)\">Screen</a><a onclick=\"t(7)\">Theme</a><a onclick=\"t(8)\">Layout</a></nav>"
"<div class=\"tab on\" id=t0><div id=stlist></div><div class=card id=dev></div></div>"
"<div class=tab id=t1>"
/* Type-first picker: choose how to add, then only the relevant fields appear. */
"<div class=card id=ppick><b>Add a printer</b>"
"<div class=pthead>Cloud accounts <small>&mdash; sign in once, your whole fleet appears</small></div>"
"<div class=ptgrid>"
"<div class=ptile onclick=pick('connect')><span class=ptag>Easiest</span><b>&#9729; Prusa</b><small>Sign in to Prusa Connect and pull in every printer on your account.</small></div>"
"<div class=ptile onclick=pick('bcloud')><span class=ptag>Alpha</span><b>&#9729; Bambu</b><small>Sign in to your Bambu Lab account (or paste an access token).</small></div>"
"</div>"
"<div class=pthead>Local printer <small>&mdash; connect directly over your LAN</small></div>"
"<div class=ptgrid>"
"<div class=ptile onclick=pick('link')><b>&#9635; Prusa (PrusaLink)</b><small>MK4 &middot; MK3.5/3.9 &middot; MINI &middot; CORE One &middot; XL, by IP + API key.</small></div>"
"<div class=ptile onclick=pick('klipper')><b>&#9881; Klipper (Moonraker)</b><small>Fluidd / Mainsail printers, at host:7125.</small></div>"
"<div class=ptile onclick=pick('bambu')><b>&#9635; Bambu (LAN)</b><small>X1 &middot; P1 &middot; A1 in LAN + Developer Mode, by IP + Access Code.</small></div>"
"</div></div>"
/* The reveal-on-select form (one set of fields, relabeled per type). */
"<div class=card id=pform style=display:none><b id=pftitle>Add printer</b><span class=ptback onclick=pcancel()>&#8592; Back</span>"
"<div id=bbhint class=muted style='display:none;margin:8px 0'>The printer must be in <b>LAN Mode</b> with <b>Developer Mode</b> on (printer Settings &rarr; Network). Find the Access Code and Serial there.</div>"
"<input id=pn placeholder=Name><input id=ph placeholder='IP / host'>"
"<input id=pk placeholder='API key (blank = keep when editing)'>"
"<input id=ps placeholder='Device serial' style=display:none>"
"<button class=p onclick=savp()>Save</button></div>"
"<div id=plist></div>"
"<div class=card><b>Backup & Restore</b>"
"<p class=muted>Export your fleet config to a file, or import a saved config (replaces current fleet).</p>"
"<button onclick=expc()>Export Config</button>"
"<input type=file id=icf accept=.json style=margin-top:12px><button onclick=impc()>Import Config</button></div></div>"
"<div class=tab id=t2><div class=card><b>Wi-Fi</b>"
"<input id=ws placeholder=SSID><input id=wp type=password placeholder=Password>"
"<button class=p onclick=savew()>Save &amp; connect</button></div></div>"
"<div class=tab id=t5><div class=card><b>Prusa Connect</b>"
"<div id=acstat class=muted>Not linked.</div>"
"<button id=lob style='display:none;background:#5a2d2d;width:auto;padding:6px 14px' onclick=logout()>Log out</button>"
"<div id=loginform><input id=ae placeholder=Email><input id=ap type=password placeholder=Password>"
"<label style='display:block;margin:6px 0;font-size:13px'><input id=arem type=checkbox checked> Stay signed in (store password on device for automatic re-login)</label>"
"<button class=p onclick=connl()>Link Account</button></div>"
"<div id=totpform style=display:none><p class=muted>2FA required. Enter your TOTP code:</p>"
"<input id=tc placeholder=123456><button class=p onclick=connt()>Verify</button></div>"
"<div id=acclist style=margin-top:16px></div></div>"
"<div class=card><b>Bambu Lab Cloud</b> <span class=ptag>Alpha</span> <span id=bcstat class=muted></span>"
"<div class=muted style=margin:6px 0>Sign in to your Bambu account, or paste an access token (more reliable &mdash; Bambu's login sits behind Cloudflare, which the device may not pass). Then pull in your printers.</div>"
"<div id=bcform><input id=bce placeholder=Email><input id=bcp type=password placeholder=Password>"
"<button class=p onclick=bclogin()>Sign in</button>"
"<div id=bccodef style='display:none;margin-top:8px'><input id=bccode placeholder='6-digit email code'><button class=p onclick=bcsubmitcode()>Verify code</button></div>"
"<div style=margin-top:10px><input id=bctok placeholder='or paste access token'><button onclick=bctoken()>Use token</button></div></div>"
"<div id=bcauthed style=display:none><button class=p onclick=bcpull()>Add my printers</button> <button onclick=bclogout()>Sign out</button></div></div>"
"<div class=card><b>Security</b> <span id=secstat class=muted></span>"
"<div class=muted style=margin:6px 0>Both optional, off by default.</div>"
"<label style='display:block;margin:8px 0 2px'>Web password (gates this page; blank = open)</label>"
"<input id=swp type=password placeholder='blank = no password'>"
"<label style='display:block;margin:8px 0 2px'>Screen unlock PIN (blank = no lock)</label>"
"<input id=spin placeholder='e.g. 1234'>"
"<label style='display:block;margin:8px 0 2px'>Auto-lock the touchscreen after</label>"
"<select id=slm><option value=0>Off</option><option value=1>1 min</option><option value=2>2 min</option><option value=5>5 min</option><option value=10>10 min</option><option value=30>30 min</option></select>"
"<button class=p onclick=secsave() style=margin-top:10px>Save security settings</button></div></div>"
"<div class=tab id=t6><div class=card><b>Prusa Farm</b>"
"<div class=muted>Org-wide printer + order status. Find your Organization ID at connect-farm.prusa3d.com.</div>"
"<input id=forg placeholder='Organization ID'><button class=p onclick=lf()>Load Farm</button></div>"
"<div id=fstat></div><div id=forders></div></div>"
"<div class=tab id=t3>"
"<div class=card><b>Auto-update from GitHub</b>"
"<div id=gh class=muted>Tap Check.</div>"
"<button onclick=chk()>Check for updates</button>"
"<button class=p id=ub style=display:none onclick=applyu()>Update now</button></div>"
"<div class=card><b>Manual firmware upload</b>"
"<p class=muted>Upload the <b>prusa-touch-app.bin</b> (the OTA image, ~2 MB). The device reboots into it. Don't upload the full 16 MB image here &mdash; that's for a USB flash.</p>"
"<input type=file id=fw accept=.bin><button class=p onclick=ota()>Flash</button>"
"<div id=otalog class=muted></div></div>"
"<div class=card><b>Scheduled reboot</b>"
"<div class=muted>Optionally reboot the device daily to keep its RAM fresh. Set your UTC offset so the hour is local time.</div>"
"<div style='margin-top:8px'><label><input type=checkbox id=rben> Daily reboot at</label> <select id=rbhr></select>:00 &nbsp; UTC offset <select id=rbtz></select> <button class=p onclick=rbsave()>Save</button></div>"
"<div id=rbmsg class=muted style='margin-top:6px'></div></div></div>"
"<div class=tab id=t4><div class=card><b>Live screen</b> "
"<button onclick=shot()>Refresh</button>"
"<div class=muted>What the touchscreen is showing right now.</div>"
"<img id=shot style='max-width:100%;border:1px solid #4e4e4e;margin-top:8px'></div>"
"<div class=card><b>Language</b>"
"<div class=muted>On-screen UI language. Saving reboots the device. Only English ships translated today; other languages also need fonts with the matching glyphs.</div>"
"<div style='margin-top:8px'><select id=lgsel></select> <button class=p onclick=lgsave()>Save &amp; reboot</button></div></div></div>"
"<div class=tab id=t7><div class=card><b>Theme</b>"
"<div class=muted>Pick a pre-baked theme, or design your own from six colors. The full palette derives automatically and is contrast-checked; applying reboots the device into the theme.</div>"
"<div style='margin:12px 0;padding:10px;background:#2a2a2a;border-radius:8px'><b style='font-size:13px'>Pre-baked themes</b>"
"<div style='margin-top:8px;display:flex;gap:10px;align-items:center;flex-wrap:wrap'>"
"<select id=tfpreset style='width:auto' onchange=tf_preset_load()></select>"
"<button class=p onclick=tf_preset_apply()>Apply this theme</button>"
"<button onclick=tf_default()>Reset to default</button></div>"
"<div class=muted style='margin-top:6px'>Selecting one loads it below to preview &amp; tweak; Apply restarts into the theme as-is.</div></div>"
"<div class=muted style='margin-top:6px'>Or design your own:</div>"
"<div id=tfseeds style='display:flex;flex-wrap:wrap;gap:14px;margin:12px 0'></div>"
"<label>Variant <select id=tfvar onchange=tf_render()><option value=dark>Dark</option><option value=light>Light</option></select></label>"
"<label style='margin-left:16px'>Font <select id=tffont onchange=tf_render()><option value=0>Montserrat</option><option value=1>Inter</option></select></label>"
"<div style='margin-top:10px;display:flex;gap:14px;flex-wrap:wrap'><label style='font-size:12px'>Wordmark<br><input id=tfbrand value='PRUSA | TOUCH' style='width:150px' oninput=tf_render()></label><label style='font-size:12px'>Byline<br><input id=tfbyline value='by NomadsGalaxy' style='width:150px' oninput=tf_render()></label></div>"
"<div style='display:flex;gap:20px;flex-wrap:wrap;margin-top:14px'>"
"<div><div class=muted>Preview</div><div id=tfprev></div></div>"
"<div style='flex:1;min-width:220px'><div class=muted>Derived palette (click a chip to copy)</div>"
"<div id=tfsw style='display:flex;flex-wrap:wrap;gap:5px;margin-top:5px'></div>"
"<div class=muted style='margin-top:12px'>Contrast (WCAG)</div><div id=tfwc style='display:flex;flex-wrap:wrap;gap:3px 14px'></div></div></div>"
"<div id=tfwarn style='color:#f8795f;margin-top:10px'></div>"
"<div style='margin-top:14px'><button class=p onclick=tf_save()>Save &amp; apply</button> "
"<button onclick=tf_export()>Export</button> <button onclick=tf_pick()>Import</button>"
"<input type=file id=tfimp accept=.json style=display:none onchange=tf_import(event)></div></div></div>"
"<div class=tab id=t8><div class=card><b>Layout</b>"
"<div class=muted>Arrange data tiles on a grid for your printer view. Click a tile to move, resize, restyle it (Card / Bare / Accent), or combine it with neighbours into one card (give them the same Combine group, like the stock job card). The Header tile is the name + state strip; the default is a 1:1 of the status screen. Save stores your layout; Generate preview renders exactly how it looks on the device.</div>"
"<div style='margin:10px 0'>Add tile: <span id=lypal></span></div>"
"<div id=lygrid ondragover=ly_over(event) ondrop=ly_drop(event) style='position:relative;width:560px;max-width:100%;aspect-ratio:5/3;background:#1c1e21;border:1px solid #4e4e4e;border-radius:6px'></div>"
"<div id=lysel style='margin-top:10px;min-height:26px;color:#a7a7a7;font-size:13px'></div>"
"<div style='margin-top:12px'><button class=p onclick=ly_preview()>Generate preview</button> <button onclick=ly_save()>Save</button> <button onclick=ly_export()>Export</button> <button onclick=ly_pick()>Import</button>"
"<input type=file id=lyimp accept=.json style=display:none onchange=ly_import(event)></div>"
"<div style='margin-top:14px'><div class=muted>Device preview (rendered on the device, exactly as it appears)</div>"
"<img id=lyprev alt='Generate a preview to see it as rendered on the device.' style='max-width:100%;width:340px;border:1px solid #4e4e4e;border-radius:6px;background:#111316;margin-top:6px'></div></div></div>"
"<div id=snapm style='display:none;position:fixed;inset:0;background:rgba(0,0,0,.82);z-index:99;align-items:center;justify-content:center' onclick=\"if(event.target==this)snapx()\">"
"<div style='background:#1c1e21;padding:14px;border-radius:10px;max-width:92%'>"
"<div style='display:flex;justify-content:space-between;align-items:center;gap:16px;margin-bottom:10px'><b id=snapt>Snapshot</b><span><button onclick=snapr()>Refresh</button><button onclick=snapx()>Close</button></span></div>"
"<img id=snapi style='max-width:84vw;max-height:70vh;border-radius:6px;background:#000;min-width:200px;min-height:120px' onerror=\"this.alt='No snapshot available for this printer'\"></div></div>"
"<script>"
"var FL=[];var SNAPU='';"
"function t(i){for(let n=0;n<9;n++){let el=document.getElementById('t'+n);if(el)el.className='tab'+(n==i?' on':'')}"
"document.querySelectorAll('nav a').forEach(function(a){a.className=a.getAttribute('onclick')==('t('+i+')')?'on':''});if(i==1)lp();if(i==4){shot();lgload()}if(i==5)la();if(i==3)rbload();if(i==6)lf_init();if(i==7)tf_load();if(i==8)ly_load()}"
"function shot(){document.getElementById('shot').src='/api/screen.bmp?t='+Date.now()}"
"async function st(){let L=await fetch('/api/fleet').then(x=>x.json());FL=L;"
"const sc=s=>{s=(s||'').toUpperCase();if(s=='PRINTING'||s=='ATTENTION')return'orange';if(s=='PAUSED')return'yellow';if(s=='FINISHED')return'green';if(s=='READY')return'olive';if(s=='ERROR'||s=='STOPPED')return'red';if(s=='BUSY'||s=='PREPARING')return'blue';return'gray'};"
"document.getElementById('stlist').innerHTML=L.map((r,i)=>{const c=r.online?sc(r.state):'gray';return '<div class=\"card '+c+'\">'+"
"'<div class=c-head>'+r.name+'<div class=c-badge>'+(r.online?r.state:'OFFLINE')+'</div></div>'+"
"'<div class=c-body>'+"
"'<div class=c-grid>'+"
"'<div class=c-cell><b>NOZZLE</b><div>'+(r.online?r.nozzle+(r.tnozzle>0?'/'+r.tnozzle:''):'--')+'&deg;C</div></div>'+"
"'<div class=c-cell><b>HEATBED</b><div>'+(r.online?r.bed+(r.tbed>0?'/'+r.tbed:''):'--')+'&deg;C</div></div>'+"
"'<div class=c-cell><b>SPEED</b><div>'+(r.online?r.speed+'%':'--')+'</div></div>'+"
"'<div class=c-cell><b>Z AXIS</b><div>'+(r.online?r.z.toFixed(2)+'mm':'--')+'</div></div>'+"
"'<div class=c-cell style=grid-column:span 2><b>PROGRESS</b><div>'+(r.printing?r.progress+'%':'--')+'</div></div>'+"
"'</div>'+"
"(r.printing?('<p class=muted style=margin:12px 0 4px 0>'+r.job+'</p><div class=bar><i style=width:'+r.progress+'%></i></div>'):'')+"
"(r.ctl?cp(r,i):'')+"
"'</div></div>'}).join('')||'<div class=card style=padding:18px>No printers yet.</div>';"
"try{let d=await fetch('/api/info').then(x=>x.json());document.getElementById('dev').innerHTML="
"'<span class=muted>'+d.name+' '+d.fw+' &middot; heap '+Math.round(d.heap_free/1024)+'KB &middot; up '+d.uptime_s+'s</span>'}catch(e){}}"
/* Per-printer control panel on each fleet card — mirrors the touchscreen Control screen.
   All onclick args are numeric (printer index, axis 0/1/2, signed distance, feedrate) so
   nothing needs quote-escaping inside these C string literals. */
"function csnap(i){var p=FL[i];if(!p||!p.uuid)return;SNAPU=p.uuid;document.getElementById('snapt').textContent=p.name+' \\u2014 webcam';snapr();document.getElementById('snapm').style.display='flex';}"
"function snapr(){document.getElementById('snapi').src='/api/connect/snapshot?uuid='+SNAPU+'&t='+Date.now();}"
"function snapx(){document.getElementById('snapm').style.display='none';document.getElementById('snapi').src='';}"
"function cp(r,i){return '<div class=ctlp>'+'<button onclick=csnap('+i+')>&#128247; Webcam</button>'+(r.printing?"
"'<div class=lbl>JOB</div><button onclick=cpause('+i+')>Pause</button><button onclick=cstop('+i+')>Stop</button>':"
"'<div class=lbl>PREHEAT</div><div><button onclick=cpre('+i+',215,60)>PLA</button><button onclick=cpre('+i+',230,85)>PETG</button><button onclick=cpre('+i+',260,100)>ASA</button><button onclick=cpre('+i+',0,0)>Cooldown</button></div>'+"
"'<div class=lbl style=margin-top:8px>MOVE</div><div><button onclick=chome('+i+')>&#8962; Home</button><button onclick=cjog('+i+',0,-10,3000)>X-</button><button onclick=cjog('+i+',0,10,3000)>X+</button><button onclick=cjog('+i+',1,-10,3000)>Y-</button><button onclick=cjog('+i+',1,10,3000)>Y+</button><button onclick=cjog('+i+',2,-10,600)>Z-</button><button onclick=cjog('+i+',2,10,600)>Z+</button></div>')+'</div>';}"
"function cgo(i,op,qs){let u=FL[i]&&FL[i].uuid;if(!u)return;return fetch('/api/connect/control?uuid='+u+'&op='+op+(qs||''),{method:'POST'}).then(()=>setTimeout(st,1200)).catch(()=>{});}"
"function cpause(i){cgo(i,'pause')}"
"function cstop(i){if(confirm('Stop the print on '+(FL[i]&&FL[i].name)+'?'))cgo(i,'stop')}"
"function cpre(i,n,b){cgo(i,'preheat','&n='+n+'&b='+b)}"
"function chome(i){cgo(i,'home')}"
"function cjog(i,a,d,f){if(a==2)cgo(i,'movez','&z='+d+'&f='+f);else cgo(i,'move','&'+(a==0?'x':'y')+'='+d+'&f='+f)}"
"async function logout(){if(!confirm('Log out of Prusa Connect? This clears the saved account.'))return;await fetch('/api/connect/logout',{method:'POST'});acclist.innerHTML='';la()}"
"async function la(){let r=await fetch('/api/connect/info').then(x=>x.json());"
"acstat.textContent=r.auth?'Linked':'Not linked.';"
"lob.style.display=r.auth?'inline-block':'none';"
"loginform.style.display=r.auth?'none':'block';totpform.style.display='none';"
"if(r.auth){"
"let teams=await fetch('/api/connect/teams').then(x=>x.json());"
"let def=await fetch('/api/connect/default_team').then(x=>x.text());"
"acclist.innerHTML='<p class=muted>Your Teams / Farm Mode:</p>'+teams.map(t=>'<div class=card><b>'+t.name+'</b> ('+t.role+')'+"
"(t.id==def?' <span style=\"background:var(--o);color:#fff;padding:2px 6px;border-radius:4px;font-size:10px;margin-left:8px\">DEFAULT</span>':'')+"
"'<div id=tlist_'+t.id+' style=margin-top:8px><button onclick=\"lt(\\''+t.id+'\\')\">Load Printers</button> '+"
"'<button class=p onclick=\"sd(\\''+t.id+'\\')\">Set Default</button></div></div>').join('')||'<div class=card style=padding:12px>No teams found.</div>'}"
"loadsec();bcla()}"
"async function bcla(){let r=await fetch('/api/bambu/info').then(x=>x.json());"
"bcstat.textContent=r.auth?'Signed in.':'Not signed in.';bcform.style.display=r.auth?'none':'block';bcauthed.style.display=r.auth?'block':'none'}"
"async function bclogin(){let r=await fetch('/api/bambu/login',{method:'POST',body:JSON.stringify({email:bce.value,password:bcp.value})}).then(x=>x.json());"
"if(r.res=='ok'){bce.value=bcp.value='';bcla()}else if(r.res=='code'){bccodef.style.display='block';alert('Check your email for a 6-digit code, enter it below.')}else alert('Sign-in failed \\u2014 Bambu may be blocking the device. Try the access-token option.')}"
"async function bcsubmitcode(){let r=await fetch('/api/bambu/code',{method:'POST',body:JSON.stringify({email:bce.value,code:bccode.value})}).then(x=>x.json());"
"if(r.res=='ok'){bccodef.style.display='none';bccode.value='';bcla()}else alert('Code rejected.')}"
"async function bctoken(){let r=await fetch('/api/bambu/token',{method:'POST',body:JSON.stringify({token:bctok.value})}).then(x=>x.json());"
"if(r.res=='ok'){bctok.value='';bcla()}else alert('Token rejected (could not read your account).')}"
"async function bcpull(){let r=await fetch('/api/bambu/pull',{method:'POST'}).then(x=>x.json());alert('Found '+r.found+', added '+r.added+' printer(s).')}"
"async function bclogout(){if(!confirm('Sign out of Bambu cloud?'))return;await fetch('/api/bambu/logout',{method:'POST'});bcla()}"
"async function loadsec(){let r=await fetch('/api/info').then(x=>x.json());"
"secstat.textContent=(r.webauth?'web password ON':'web password off')+' \\u00b7 '+(r.scrlock?('screen lock '+r.lockmin+'m'):'screen lock off');"
"slm.value=r.lockmin||0}"
"async function secsave(){await fetch('/api/security',{method:'POST',body:JSON.stringify({webpw:swp.value,pin:spin.value,lockmin:parseInt(slm.value)||0})});"
"swp.value='';spin.value='';alert('Saved. If you set a web password you may be prompted to sign in.');loadsec()}"
"async function sd(id){await fetch('/api/connect/default_team',{method:'POST',body:id});la()}"
"async function lt(tid){let L=await fetch('/api/connect/team_printers?id='+tid).then(x=>x.json());"
"document.getElementById('tlist_'+tid).innerHTML='<button class=p style=margin-bottom:8px onclick=\"addAll(\\''+tid+'\\')\">Add All from Team</button>'+"
"L.map(p=>'<div class=card style=\"margin:4px 0;padding:8px 12px\"><b>'+p.name+'</b> <span class=muted>'+p.model+'</span> '+"
"'<button onclick=\"addc(\\''+p.uuid+'\\',\\''+p.name+'\\')\">Add</button></div>').join('')}"
"async function addAll(tid){if(!confirm('Add all printers from this team?'))return;"
"let L=await fetch('/api/connect/team_printers?id='+tid).then(x=>x.json());"
"for(let p of L)await addc(p.uuid,p.name);lp()}"
"async function connl(){acstat.textContent='Logging in...';"
"let r=await fetch('/api/connect/login',{method:'POST',body:JSON.stringify({e:ae.value,p:ap.value,rem:arem.checked})});"
"let j=await r.json();if(j.res=='totp'){loginform.style.display='none';totpform.style.display='block';acstat.textContent='2FA Required'}else if(j.res=='ok'){la()}else alert('Login failed')}"
"async function connt(){let r=await fetch('/api/connect/totp',{method:'POST',body:JSON.stringify({c:tc.value})});"
"if((await r.json()).res=='ok')la();else alert('Verification failed')}"
"async function addc(id,name){await fetch('/api/printers',{method:'POST',body:JSON.stringify({name:name,host:'cloud:'+id,key:'connect'})});lp();alert('Added!')}"
"let PL=[],EI=-1;"
"var CT='link';"  /* current add type */
"function pick(ty){if(ty=='connect'||ty=='bcloud'){t(5);return}CT=ty;EI=-1;pn.value=ph.value=pk.value=ps.value='';"
"let b=ty=='bambu';ps.style.display=b?'':'none';bbhint.style.display=b?'':'none';"
"ph.placeholder=b?'Printer IP':(ty=='klipper'?'host:7125 (e.g. 192.168.1.50:7125)':'IP / host');"
"pk.placeholder=b?'LAN Access Code':(ty=='klipper'?'API key (usually blank)':'API key');"
"pftitle.textContent=({link:'Add a Prusa printer',klipper:'Add a Klipper printer',bambu:'Add a Bambu Lab printer'}[ty]||'Add printer');"
"ppick.style.display='none';pform.style.display='block'}"
"function pcancel(){pform.style.display='none';ppick.style.display='block';EI=-1}"
"async function lp(){PL=await fetch('/api/printers').then(x=>x.json());"
"document.getElementById('plist').innerHTML=PL.map(p=>'<div class=card style=\"padding:10px 12px\">'+(p.active?'\\u2605 ':'')+'<b>'+p.name+'</b> <span class=muted>'+(p.host.indexOf('cloud:')==0?'\\u2601 Prusa Connect':p.host.indexOf('bambucloud:')==0?'\\u2601 Bambu Cloud':p.host.indexOf('bambu:')==0?'Bambu LAN '+p.host.slice(6):p.host+(p.haskey?'':' (no key)'))+'</span> '"
"+'<button class=p onclick=usep('+p.i+')>Use</button> <button onclick=editp('+p.i+')>Edit</button> <button onclick=delp('+p.i+')>Remove</button></div>').join('')}"
"function editp(i){let p=PL.find(x=>x.i==i);if(!p)return;EI=i;let b=p.host.indexOf('bambu:')==0;CT=b?'bambu':'link';"
"pn.value=p.name;pk.value='';ph.value=b?p.host.slice(6):p.host;ps.value=p.serial||'';ps.style.display=b?'':'none';bbhint.style.display=b?'':'none';"
"ph.placeholder=b?'Printer IP':'IP / host';pk.placeholder=b?'LAN Access Code (blank = keep)':'API key (blank = keep)';"
"pftitle.textContent='Edit '+p.name;ppick.style.display='none';pform.style.display='block'}"
"async function savp(){let host=CT=='bambu'?'bambu:'+ph.value:ph.value;let m={name:pn.value,host:host,key:pk.value,serial:ps.value};if(EI>=0)m.i=EI;"
"let r=await fetch(EI<0?'/api/printers':'/api/printers/update',{method:'POST',body:JSON.stringify(m)});"
"if(r.status>=400)alert(await r.text());else{pcancel();lp()}}"
"async function delp(i){if(!confirm('Remove this printer?'))return;await fetch('/api/printers/remove',{method:'POST',body:JSON.stringify({i:i})});if(EI==i)pcancel();lp()}"
"async function usep(i){await fetch('/api/printers/active',{method:'POST',body:JSON.stringify({i:i})});lp()}"
"async function expc(){let r=await fetch('/api/config/export').then(x=>x.json());"
"let b=new Blob([JSON.stringify(r,null,2)],{type:'application/json'});"
"let a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='prusa-touch-config.json';a.click()}"
"async function impc(){let f=icf.files[0];if(!f)return;if(!confirm('Replace ALL printers with config from '+f.name+'?'))return;"
"let r=await fetch('/api/config/import',{method:'POST',body:f});"
"if(r.status>=400)alert(await r.text());else{lp();alert('Import success!')}}"
"async function savew(){await fetch('/api/wifi',{method:'POST',body:JSON.stringify({ssid:ws.value,pass:wp.value})});alert('Saved; connecting...')}"
"async function ota(){let f=document.getElementById('fw').files[0];if(!f)return;"
"if(f.size>5242880&&!confirm('That file is over 5 MB \\u2014 it looks like the full image, not the OTA app.bin. Upload anyway?'))return;"
"document.getElementById('otalog').textContent='Uploading '+f.name+'...';"
"try{let r=await fetch('/update',{method:'POST',body:f});document.getElementById('otalog').textContent=await r.text()}"
"catch(e){document.getElementById('otalog').textContent='Upload failed (the device may have rejected an oversized file).'}}"
"let GU='';"
"async function chk(){document.getElementById('gh').textContent='Checking...';let n=0;"
"const poll=async()=>{let r=await fetch('/api/update/check').then(x=>x.json());"
"if(r.checking&&n++<6){setTimeout(poll,2000);return;}"
"document.getElementById('gh').textContent='Current '+r.current+' / latest '+(r.latest||'?')+(r.available?' \\u2014 update available!':' \\u2014 up to date');"
"GU=r.url;document.getElementById('ub').style.display=r.available?'inline-block':'none'};poll()}"
"async function applyu(){if(!GU)return;document.getElementById('gh').innerHTML='Updating... <div class=bar id=upb><i style=width:0%></i></div>';"
"await fetch('/api/update/apply',{method:'POST',body:JSON.stringify({url:GU})});"
"const p=async()=>{try{let r=await fetch('/api/update/progress').then(x=>x.json());"
"if(r.progress==-2){document.getElementById('gh').innerHTML='<b style=color:#F8795F>Update failed.</b> '+(r.msg||'')+' &mdash; check the file / URL and try again.';return}"
"if(r.progress>=0)document.getElementById('upb').firstChild.style.width=r.progress+'%';"
"setTimeout(p,1000)}catch(e){}};p()}"
"function lf_init(){let o=localStorage.getItem('farmorg');if(o){forg.value=o;lf()}}"
"async function lf(){let o=forg.value.trim();if(!o){alert('Enter your Organization ID');return}localStorage.setItem('farmorg',o);fetch('/api/connect/setorg',{method:'POST',body:o});"
"fstat.innerHTML='<div class=card style=padding:12px>Loading...</div>';forders.innerHTML='';"
"try{let s=await fetch('/api/connect/farm?org='+o).then(x=>x.json());let p=s.data.stats.printers;"
"fstat.innerHTML='<div class=card><div class=c-head>Printers</div><div class=c-body><div class=c-grid>'+"
"'<div class=c-cell><b>ACTIVE</b><div>'+p.active+'</div></div>'+"
"'<div class=c-cell><b>ONLINE</b><div>'+p.online+'</div></div>'+"
"'<div class=c-cell><b>ERROR</b><div>'+p.error+'</div></div>'+"
"'<div class=c-cell><b>TOTAL</b><div>'+p.total+'</div></div></div></div></div>'}"
"catch(e){fstat.innerHTML='<div class=card style=padding:12px>Farm stats unavailable</div>'}"
"try{let d=await fetch('/api/connect/orders?org='+o).then(x=>x.json());let ns=d.data.order.orders.edges.map(e=>e.node);"
"let act=ns.filter(n=>n.state==='PROCESSING');"   /* only Processing orders (not Finished/Cancelled/Draft) */
"let rows=act.map(n=>{let j=n.jobCounts;let tot=j.created+j.printing+j.done+j.cancelled+j.needAttention;let dn=n.jobsCountCompleted||0;let pct=tot?Math.round(dn/tot*100):0;"
"return '<div class=card style=padding:10px><b>#'+n.number+' '+n.name+'</b><br><span class=muted>'+(n.completionDate||'')+' - done '+dn+'/'+tot+(j.printing?', '+j.printing+' printing':'')+(j.needAttention?', '+j.needAttention+' attn':'')+'</span><div class=bar><i style=width:'+pct+'%></i></div></div>'}).join('')||'<div class=card style=padding:12px class=muted>No active orders</div>';"
"forders.innerHTML='<div class=card style=padding:10px><b>Active Orders</b> <span class=muted>'+act.length+'</span></div>'+rows}"
"catch(e){forders.innerHTML='<div class=card style=padding:12px>Orders unavailable</div>'}}"
/* ---- ThemeForge skin editor (issue #6 Phase 1b). Single-quotes + backtick templates only, so
 * it embeds in these C string literals with no escaping. 6 seeds -> derive 19 tokens -> WCAG. ---- */
"var TFS={primary:'#fa6831',secondary:'#7da7d9',error:'#f8795f',surface:'#2a2a2a',ink:'#ffffff',bg:'#1c1e21'},TFV='dark',TFC={},TFPRESETS=[];"
"var TFSL=[['primary','Accent'],['secondary','Info/blue'],['error','Error'],['surface','Surface'],['ink','Text'],['bg','Background']];"
"var TFTOK=['orange','orange_dark','bg','header','surface','surface_hi','border','text','text_muted','text_inverse','state_green','state_olive','state_gray','state_orange','state_blue','state_yellow','state_red','temp_cold','temp_hot'];"
"var TFPR=[['text','surface'],['text','bg'],['text_muted','surface'],['text_inverse','orange'],['text_inverse','state_orange'],['text','header'],['state_red','surface'],['state_green','surface'],['state_blue','surface'],['state_yellow','surface'],['temp_hot','surface'],['temp_cold','surface']];"
"function _cl(n,a,b){return n<a?a:n>b?b:n}"
"function h2r(h){h=String(h).trim().replace(/^#/,'');if(h.length==3)h=h[0]+h[0]+h[1]+h[1]+h[2]+h[2];var n=parseInt(h,16);return{r:(n>>16)&255,g:(n>>8)&255,b:n&255}}"
"function r2h(r,g,b){var t=function(v){v=Math.round(_cl(v,0,255)).toString(16);return v.length<2?'0'+v:v};return '#'+t(r)+t(g)+t(b)}"
"function r2l(r,g,b){r/=255;g/=255;b/=255;var mx=Math.max(r,g,b),mn=Math.min(r,g,b),h,s,l=(mx+mn)/2;if(mx==mn){h=0;s=0}else{var d=mx-mn;s=l>.5?d/(2-mx-mn):d/(mx+mn);switch(mx){case r:h=(g-b)/d+(g<b?6:0);break;case g:h=(b-r)/d+2;break;default:h=(r-g)/d+4}h/=6}return{h:h*360,s:s*100,l:l*100}}"
"function l2r(h,s,l){h=((h%360)+360)%360/360;s=_cl(s,0,100)/100;l=_cl(l,0,100)/100;var r,g,b;if(s==0){r=g=b=l}else{var f=function(p,q,t){if(t<0)t+=1;if(t>1)t-=1;if(t<1/6)return p+(q-p)*6*t;if(t<.5)return q;if(t<2/3)return p+(q-p)*(2/3-t)*6;return p};var q=l<.5?l*(1+s):l+s-l*s,p=2*l-q;r=f(p,q,h+1/3);g=f(p,q,h);b=f(p,q,h-1/3)}return{r:r*255,g:g*255,b:b*255}}"
"function h2l(h){var c=h2r(h);return r2l(c.r,c.g,c.b)}function l2h(h,s,l){var c=l2r(h,s,l);return r2h(c.r,c.g,c.b)}"
"function mix(a,b,t){var x=h2r(a),y=h2r(b);return r2h(x.r+(y.r-x.r)*t,x.g+(y.g-x.g)*t,x.b+(y.b-x.b)*t)}"
"function sh(h,dl,ds){var c=h2l(h);return l2h(c.h,_cl(c.s+(ds||0),0,100),_cl(c.l+(dl||0),0,100))}"
"var TFA={green:'#5cd35c',olive:'#9bbf6e',yellow:'#ffd23f'};"
"function _stt(h,v){var u=h2l(h).h;return v=='light'?l2h(u,55,42):l2h(u,68,68)}"
"function derive(s,v){var d=v!='light',T={};T.orange=s.primary;T.orange_dark=sh(s.primary,d?-22:-18,6);T.bg=s.bg;T.header=sh(s.bg,d?-6:-5);T.surface=s.surface;T.surface_hi=sh(s.surface,d?15:-12);T.border=T.surface_hi;T.text=s.ink;T.text_muted=mix(s.ink,s.surface,d?.42:.4);T.text_inverse=d?sh(s.bg,3):'#ffffff';T.state_green=_stt(TFA.green,v);T.state_olive=_stt(TFA.olive,v);T.state_gray=mix(s.ink,s.surface,d?.32:.3);T.state_orange=_stt(s.primary,v);T.state_blue=_stt(s.secondary,v);T.state_yellow=_stt(TFA.yellow,v);T.state_red=_stt(s.error,v);T.temp_cold=d?'#3a96ff':'#0072ff';T.temp_hot=d?'#ff5a40':'#e10000';return T}"
"function exS(c){return{primary:c.orange,secondary:c.state_blue||c.temp_cold,error:c.state_red,surface:c.surface,ink:c.text,bg:c.bg}}"
"function _lum(h){var c=h2r(h),f=function(x){x/=255;return x<=.03928?x/12.92:Math.pow((x+.055)/1.055,2.4)};return .2126*f(c.r)+.7152*f(c.g)+.0722*f(c.b)}"
"function _cr(a,b){var L=_lum(a),M=_lum(b),hi=Math.max(L,M),lo=Math.min(L,M);return(hi+.05)/(lo+.05)}"
"function _ct(r){return r>=7?'AAA':r>=4.5?'AA':r>=3?'AA-L':'FAIL'}"
"async function rbload(){var r=await fetch('/api/info').then(x=>x.json()),en=r.reboot_hour<=23,hr=document.getElementById('rbhr'),tz=document.getElementById('rbtz');"
"var hh='';for(var v=0;v<24;v++)hh+=`<option value='${v}'${v==(en?r.reboot_hour:4)?' selected':''}>${(''+v).padStart(2,'0')}</option>`;hr.innerHTML=hh;"
"var th='';for(var v=-12;v<=14;v++)th+=`<option value='${v}'${v==r.tz_offset?' selected':''}>${v>=0?'+':''}${v}</option>`;tz.innerHTML=th;"
"document.getElementById('rben').checked=en;document.getElementById('rbmsg').textContent=r.clock_ok?('Device local time now: '+r.local+' (adjust the UTC offset if that is wrong).'):'Device clock not synced yet (needs internet).'}"
"async function rbsave(){var en=document.getElementById('rben').checked,hr=en?parseInt(document.getElementById('rbhr').value):255,tz=parseInt(document.getElementById('rbtz').value);"
"var r=await fetch('/api/reboot',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hour:hr,tz:tz})});document.getElementById('rbmsg').textContent=r.ok?'Saved.':'Save failed (HTTP '+r.status+')'}"
"async function lgload(){var r=await fetch('/api/info').then(x=>x.json()),s=document.getElementById('lgsel');s.innerHTML=(r.langs||['English']).map(function(n,i){return `<option value='${i}'${i==r.lang?' selected':''}>${n}</option>`}).join('')}"
"async function lgsave(){var l=parseInt(document.getElementById('lgsel').value);var r=await fetch('/api/lang',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({lang:l})});alert(r.ok?'Saved - the device is restarting in the selected language.':'Failed (HTTP '+r.status+')')}"
"async function tf_load(){try{var r=await fetch('/api/skin').then(x=>x.json());TFS=exS(r.colors);TFPRESETS=r.presets||[];if(r.font!=null)document.getElementById('tffont').value=r.font;if(r.brand)document.getElementById('tfbrand').value=r.brand;if('byline' in r)document.getElementById('tfbyline').value=r.byline}catch(e){}document.getElementById('tfpreset').innerHTML=TFPRESETS.map(function(p){return `<option value='${p.index}'>${p.name}</option>`}).join('');document.getElementById('tfvar').value=TFV;tf_si();tf_render()}"
"function tf_preset_load(){var idx=parseInt(document.getElementById('tfpreset').value),p=TFPRESETS.find(function(x){return x.index==idx});if(!p)return;TFS=exS(p.colors);TFV='dark';document.getElementById('tfvar').value='dark';if(p.font!=null)document.getElementById('tffont').value=p.font;if(p.brand)document.getElementById('tfbrand').value=p.brand;if('byline' in p)document.getElementById('tfbyline').value=p.byline;tf_si();tf_render()}"
"async function tf_preset_apply(){var idx=parseInt(document.getElementById('tfpreset').value),p=TFPRESETS.find(function(x){return x.index==idx}),nm=p?p.name:'theme';var r=await fetch('/api/skin',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:idx})});alert(r.ok?'Applying '+nm+' - the device is restarting.':'Failed (HTTP '+r.status+')')}"
"async function tf_default(){var r=await fetch('/api/skin',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:0})});alert(r.ok?'Resetting to the default theme - the device is restarting.':'Failed (HTTP '+r.status+')')}"
"function tf_si(){var h='';TFSL.forEach(function(p){h+=`<label style='font-size:12px;text-align:center'>${p[1]}<br><input type=color value='${TFS[p[0]]}' style='width:58px;height:42px;padding:2px;border:1px solid #4e4e4e;border-radius:6px;cursor:pointer' oninput='TFS.${p[0]}=this.value;tf_render()'><br><span id='tfsl_${p[0]}' style='font-family:monospace;font-size:11px;color:#a7a7a7'>${TFS[p[0]]}</span></label>`});document.getElementById('tfseeds').innerHTML=h}"
"function tf_prev(){var C=TFC,bd=mix(C.state_orange,C.surface,.54),sp=mix(C.state_orange,C.bg,.38),BR=(document.getElementById('tfbrand').value||'PRUSA | TOUCH');document.getElementById('tfprev').innerHTML=`<div style='width:300px;border-radius:8px;overflow:hidden;background:${C.bg};font-size:12px;border:1px solid ${C.border}'><div style='background:${C.header};color:${C.text};padding:8px 10px;display:flex;justify-content:space-between'><b>${BR}</b><span style='width:10px;height:10px;border-radius:50%;background:${C.state_green};align-self:center'></span></div><div style='padding:10px'><div style='background:${sp};height:6px;border-radius:3px 3px 0 0'></div><div style='background:${C.surface};border:1px solid ${C.border};border-top:none;padding:10px'><div style='display:flex;justify-content:space-between'><b style='color:${C.text}'>Apollo</b><span style='background:${bd};color:${C.text};padding:2px 8px;border-radius:4px;font-size:10px'>PRINTING</span></div><div style='color:${C.text_muted};margin:2px 0 8px'>Prusa CORE One</div><div style='background:${C.surface_hi};height:8px;border-radius:4px'><div style='background:${C.orange};width:62%;height:8px;border-radius:4px'></div></div><div style='display:flex;gap:14px;margin-top:8px;color:${C.text_muted}'><span><span style='color:${C.temp_hot}'>&#9679;</span> 215&deg;C</span><span><span style='color:${C.temp_cold}'>&#9679;</span> 60&deg;C</span></div><div style='margin-top:10px'><button style='background:${C.orange};color:${C.text_inverse};border:none;padding:6px 12px;border-radius:6px'>Pause</button> <button style='background:${C.surface_hi};color:${C.text};border:none;padding:6px 12px;border-radius:6px'>Control</button></div></div></div></div>`}"
"function tf_render(){TFV=document.getElementById('tfvar').value;TFC=derive(TFS,TFV);var C=TFC;"
"TFSL.forEach(function(p){var s=document.getElementById('tfsl_'+p[0]);if(s)s.textContent=TFS[p[0]]});"
"var sw='';TFTOK.forEach(function(k){sw+=`<div title='${k} ${C[k]}' style='width:32px;height:32px;border-radius:5px;border:1px solid #0006;background:${C[k]}'></div>`});document.getElementById('tfsw').innerHTML=sw;"
"var wc='',wn='';TFPR.forEach(function(p){var r=_cr(C[p[0]],C[p[1]]),t=_ct(r),cc=t=='FAIL'?'#f8795f':t=='AA-L'?'#fddc71':'#a1ea70';wc+=`<span style='font-size:11px'>${p[0]}/${p[1]} <b style='color:${cc}'>${r.toFixed(1)} ${t}</b></span>`;if(t=='FAIL'&&(p[1]=='surface'||p[1]=='bg'))wn=`Low contrast: ${p[0]} on ${p[1]} may be hard to read.`});document.getElementById('tfwc').innerHTML=wc;document.getElementById('tfwarn').textContent=wn;tf_prev()}"
"async function tf_save(){var b={colors:TFC,font:parseInt(document.getElementById('tffont').value),brand:document.getElementById('tfbrand').value,byline:document.getElementById('tfbyline').value};var r=await fetch('/api/skin',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});alert(r.ok?'Saved - the device is restarting into your theme.':'Save failed (HTTP '+r.status+')')}"
"function tf_export(){var o={name:document.getElementById('tfbrand').value||'Custom',variant:TFV,font:parseInt(document.getElementById('tffont').value),brand:document.getElementById('tfbrand').value,byline:document.getElementById('tfbyline').value,seeds:TFS,colors:TFC},b=new Blob([JSON.stringify(o,null,2)],{type:'application/json'}),a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='prusa-touch.skin.json';a.click()}"
"function tf_pick(){document.getElementById('tfimp').click()}"
"function tf_import(e){var f=e.target.files[0];if(!f)return;var rd=new FileReader();rd.onload=function(){try{var o=JSON.parse(rd.result);TFS=o.seeds||exS(o.colors);if(o.variant)TFV=o.variant;if(o.font!=null)document.getElementById('tffont').value=o.font;if(o.brand)document.getElementById('tfbrand').value=o.brand;if('byline' in o)document.getElementById('tfbyline').value=o.byline;document.getElementById('tfvar').value=TFV;tf_si();tf_render()}catch(x){alert('Invalid skin file')}};rd.readAsText(f)}"
/* ---- Layout designer (issue #6 Phase 4): a chunk-grid editor. Single-quotes + backtick templates
 * + data-attributes only, so it embeds with no escaping. ---- */
"var LAY={cols:8,tiles:[]},LYTYPES=[],LYSEL=-1;"
"var LYMIN={name:[2,1],model:[2,1],state:[2,1],nozzle:[2,1],bed:[2,1],speed:[2,1],z:[2,1],progress:[3,1],eta:[1,1],thumb:[1,1],header:[2,1],job:[2,1]};"
"var LYLBL={name:'Name',model:'Model',state:'State',nozzle:'Nozzle',bed:'Bed',speed:'Speed',z:'Z Axis',progress:'Progress',eta:'ETA',thumb:'Preview',header:'Header',job:'File'};"
"var LYSAMPLE={name:'Apollo',model:'CORE One',state:'PRINTING',nozzle:'215\\u00b0',bed:'60\\u00b0',speed:'100%',z:'12.4',progress:'64%',eta:'1h 04m',thumb:'',header:'Apollo',job:'benchy.gcode'};"
"async function ly_load(){try{var r=await fetch('/api/layout').then(x=>x.json());LAY={cols:r.cols||8,tiles:r.tiles||[]};LYTYPES=r.types||[]}catch(e){}ly_pal();ly_render()}"
"function ly_rows(){var m=4;LAY.tiles.forEach(function(t){if(t.r+t.h>m)m=t.r+t.h});return m}"
"function ly_pal(){var h='';LYTYPES.forEach(function(t){h+=`<button data-t='${t}' onclick=ly_addsel(this) style='margin:2px'>${LYLBL[t]||t}</button>`});document.getElementById('lypal').innerHTML=h}"
"function ly_addsel(el){ly_add(el.getAttribute('data-t'))}"
"function ly_free(c,r,w,h,ex){for(var i=0;i<LAY.tiles.length;i++){if(i==ex)continue;var t=LAY.tiles[i];if(c<t.c+t.w&&c+w>t.c&&r<t.r+t.h&&r+h>t.r)return false}return true}"
"function ly_add(type){var mn=LYMIN[type]||[2,1],rows=ly_rows();for(var r=0;r<rows+2;r++)for(var c=0;c+mn[0]<=LAY.cols;c++){if(ly_free(c,r,mn[0],mn[1],-1)){LAY.tiles.push({type:type,c:c,r:r,w:mn[0],h:mn[1],style:0,group:0});LYSEL=LAY.tiles.length-1;return ly_render()}}}"
"function ly_render(){var rows=ly_rows(),g=document.getElementById('lygrid'),cw=100/LAY.cols,ch=100/rows,h='';"
"var gs={};LAY.tiles.forEach(function(t){var gp=t.group||0;if(!gp||gs[gp])return;gs[gp]=1;var mc=99,mr=99,xc=0,xr=0;LAY.tiles.forEach(function(u){if((u.group||0)!=gp)return;if(u.c<mc)mc=u.c;if(u.r<mr)mr=u.r;if(u.c+u.w>xc)xc=u.c+u.w;if(u.r+u.h>xr)xr=u.r+u.h});h+=`<div style='position:absolute;left:${mc*cw}%;top:${mr*ch}%;width:${(xc-mc)*cw}%;height:${(xr-mr)*ch}%;box-sizing:border-box;padding:6px'><div style='width:100%;height:100%;background:#2a2a2a;border-radius:5px'></div></div>`});"
"LAY.tiles.forEach(function(t,i){var sv=LYSAMPLE[t.type]||'',sy=t.style||0,gp=t.group||0,bg=gp?'transparent':(sy==2?'#fa6831':(sy==1?'transparent':'#2a2a2a')),fg=sy==2?'#212529':'#fff',cc=sy==2?'#212529':'#a7a7a7',cap=(t.type=='name'||t.type=='thumb'||t.type=='header'||sy==1)?'':(LYLBL[t.type]||t.type).toUpperCase(),inner='';"
"if(t.type=='header'){bg='#2f3a28';inner=`<div style='position:absolute;left:7px;right:7px;top:0;bottom:0;display:flex;align-items:center;gap:8px'><span style='flex:1;color:#fff;font-size:13px;font-weight:600;overflow:hidden;white-space:nowrap'>${sv}</span><span style='background:#46603a;color:#fff;font-size:9px;padding:1px 6px;border-radius:3px'>PRINTING</span></div>`}"
"else if(t.type=='thumb'){inner=`<div style='width:100%;height:100%;display:flex;align-items:center;justify-content:center;background:${sy==2?'#fa6831':'#4e4e4e'};border-radius:3px;color:${sy==2?'#b35021':'#9a9a9a'};font-size:9px;letter-spacing:1px'>PREVIEW</div>`}"
"else if(t.type=='progress'){inner=`<div style='color:${cc};font-size:9px'>${cap}</div><div style='position:absolute;left:6px;right:6px;bottom:6px;height:8px;background:#4e4e4e;border-radius:4px;overflow:hidden'><div style='width:64%;height:100%;background:#fa6831'></div></div>`}"
"else if(t.type=='state'){inner=`<div style='color:${cc};font-size:9px'>${cap}</div><div style='position:absolute;left:6px;bottom:6px;background:#464646;color:#fff;font-size:10px;padding:1px 7px;border-radius:3px'>${sv}</div>`}"
"else if(t.type=='name'){inner=`<div style='position:absolute;left:7px;top:0;bottom:0;display:flex;align-items:center;color:${fg};font-size:14px;font-weight:600'>${sv}</div>`}"
"else{var vp=sy==1?'top:0;bottom:0;display:flex;align-items:center':'bottom:3px';inner=`<div style='color:${cc};font-size:9px'>${cap}</div><div style='position:absolute;left:6px;${vp};color:${fg};font-size:14px'>${sv}</div>`}"
"var bd=i==LYSEL?'2px solid #fa6831':(gp?'none':(sy==1?'1px dashed #4e4e4e':'2px solid #1c1e21'));"
"h+=`<div onclick='ly_sel(${i})' draggable=true ondragstart='ly_drag(event,${i})' style='position:absolute;left:${t.c*cw}%;top:${t.r*ch}%;width:${t.w*cw}%;height:${t.h*ch}%;box-sizing:border-box;padding:6px;border:${bd};background:${bg};border-radius:5px;color:#fff;cursor:move;overflow:hidden'>${inner}</div>`});"
"g.innerHTML=h;g.style.backgroundImage=`repeating-linear-gradient(90deg,#2f3338 0px,#2f3338 1px,transparent 1px,transparent ${cw}%),repeating-linear-gradient(0deg,#2f3338 0px,#2f3338 1px,transparent 1px,transparent ${ch}%)`;ly_selbar()}"
"function ly_sel(i){LYSEL=i;ly_render()}"
"function ly_selbar(){var b=document.getElementById('lysel');if(LYSEL<0||LYSEL>=LAY.tiles.length){b.innerHTML='Click a tile to move, resize, restyle, combine, or remove it. Tiles sharing a Combine group render as one card.';return}var t=LAY.tiles[LYSEL],so=[0,1,2].map(function(s){return `<option value='${s}'${(t.style||0)==s?' selected':''}>${['Card','Bare','Accent'][s]}</option>`}).join(''),go=[0,1,2,3,4].map(function(gv){return `<option value='${gv}'${(t.group||0)==gv?' selected':''}>${gv==0?'None':'Group '+gv}</option>`}).join('');b.innerHTML=`<b style='color:#fff'>${LYLBL[t.type]}</b> &nbsp; style <select onchange='ly_setstyle(this.value)'>${so}</select> &nbsp; combine <select onchange='ly_setgroup(this.value)'>${go}</select> &nbsp; size <button onclick='ly_rs(-1,0)'>w-</button><button onclick='ly_rs(1,0)'>w+</button> <button onclick='ly_rs(0,-1)'>h-</button><button onclick='ly_rs(0,1)'>h+</button> &nbsp; move <button onclick='ly_mv(-1,0)'>&larr;</button><button onclick='ly_mv(1,0)'>&rarr;</button><button onclick='ly_mv(0,-1)'>&uarr;</button><button onclick='ly_mv(0,1)'>&darr;</button> &nbsp; <button onclick='ly_del()'>Remove</button>`}"
"function ly_setgroup(gv){LAY.tiles[LYSEL].group=parseInt(gv);ly_render()}"
"function ly_setstyle(s){LAY.tiles[LYSEL].style=parseInt(s);ly_render()}"
"function ly_rs(dw,dh){var t=LAY.tiles[LYSEL],mn=LYMIN[t.type]||[1,1],nw=t.w+dw,nh=t.h+dh;if(nw<mn[0]||nh<mn[1]||t.c+nw>LAY.cols||nh>8)return;if(!ly_free(t.c,t.r,nw,nh,LYSEL))return;t.w=nw;t.h=nh;ly_render()}"
"function ly_mv(dc,dr){var t=LAY.tiles[LYSEL],nc=t.c+dc,nr=t.r+dr;if(nc<0||nr<0||nc+t.w>LAY.cols)return;if(!ly_free(nc,nr,t.w,t.h,LYSEL))return;t.c=nc;t.r=nr;ly_render()}"
"function ly_del(){LAY.tiles.splice(LYSEL,1);LYSEL=-1;ly_render()}"
"function ly_drag(e,i){LYSEL=i;e.dataTransfer.setData('text',i)}function ly_over(e){e.preventDefault()}"
"function ly_drop(e){e.preventDefault();var g=document.getElementById('lygrid'),rc=g.getBoundingClientRect(),rows=ly_rows(),t=LAY.tiles[LYSEL];if(!t)return;var c=Math.floor((e.clientX-rc.left)/rc.width*LAY.cols),r=Math.floor((e.clientY-rc.top)/rc.height*rows);c=Math.max(0,Math.min(c,LAY.cols-t.w));r=Math.max(0,r);if(ly_free(c,r,t.w,t.h,LYSEL)){t.c=c;t.r=r;ly_render()}}"
"async function ly_save(){var r=await fetch('/api/layout',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(LAY)});alert(r.ok?'Saved.':'Save failed (HTTP '+r.status+')')}"
"async function ly_preview(){var img=document.getElementById('lyprev');if(img._u){URL.revokeObjectURL(img._u);img._u=null}img.alt='Rendering on the device...';"
"var r=await fetch('/api/layout/preview',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(LAY)});"
"if(!r.ok){img.removeAttribute('src');img.alt='Preview failed (HTTP '+r.status+')';return}var b=await r.blob();img._u=URL.createObjectURL(b);img.src=img._u;img.alt=''}"
"function ly_export(){var b=new Blob([JSON.stringify(LAY,null,2)],{type:'application/json'}),a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='prusa-touch.layout.json';a.click()}"
"function ly_pick(){document.getElementById('lyimp').click()}"
"function ly_import(e){var f=e.target.files[0];if(!f)return;var rd=new FileReader();rd.onload=function(){try{var o=JSON.parse(rd.result);if(o.tiles){LAY={cols:o.cols||8,tiles:o.tiles};LYSEL=-1;ly_render()}}catch(x){alert('Invalid layout file')}};rd.readAsText(f)}"
"st();la();setInterval(st,3000);"
"</script></body></html>";

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    /* The UI is baked into the firmware, so a cached copy goes stale on every update. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    pp_status_t s;
    app_state_get(&s);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", s.printer_name);
    cJSON_AddBoolToObject(o, "online", s.online);
    cJSON_AddStringToObject(o, "state", s.state[0] ? s.state : "READY");
    cJSON_AddNumberToObject(o, "nozzle", (int)s.temp_nozzle);
    cJSON_AddNumberToObject(o, "tnozzle", (int)s.target_nozzle);
    cJSON_AddNumberToObject(o, "bed", (int)s.temp_bed);
    cJSON_AddNumberToObject(o, "tbed", (int)s.target_bed);
    cJSON_AddBoolToObject(o, "has_job", s.has_job);
    cJSON_AddNumberToObject(o, "progress", (int)(s.progress + 0.5f));
    cJSON_AddStringToObject(o, "job", s.job_name);
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js);
    cJSON_Delete(o);
    return ESP_OK;
}

static esp_err_t printers_get(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    int active = printer_store_active();
    for (int i = 0; i < printer_store_count(); i++) {
        pp_printer_t p;
        if (!printer_store_get(i, &p)) continue;
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "i", i);
        cJSON_AddStringToObject(e, "name", p.name);
        cJSON_AddStringToObject(e, "host", p.host);   /* key intentionally omitted */
        cJSON_AddBoolToObject(e, "active", i == active);
        cJSON_AddBoolToObject(e, "haskey", p.api_key[0] != '\0');
        if (strncmp(p.host, "bambu:", 6) == 0) cJSON_AddStringToObject(e, "serial", p.uuid);  /* Bambu serial for edit prefill */
        if (p.local_host[0]) cJSON_AddStringToObject(e, "local", p.local_host);  /* LAN fallback learned from Connect */
        cJSON_AddItemToArray(arr, e);
    }
    char *js = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js);
    cJSON_Delete(arr);
    return ESP_OK;
}

/* Read the whole request body into a heap buffer (caller frees). */
static char *recv_body(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 4096) return NULL;
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r <= 0) { free(buf); return NULL; }
        got += r;
    }
    buf[len] = '\0';
    return buf;
}

static esp_err_t printers_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad/oversized body"); return ESP_FAIL; }
    {
        cJSON *j = cJSON_Parse(body);
        if (j) {
            pp_printer_t p = {0};
            const cJSON *n = cJSON_GetObjectItem(j, "name");
            const cJSON *h = cJSON_GetObjectItem(j, "host");
            const cJSON *k = cJSON_GetObjectItem(j, "key");
            const cJSON *s = cJSON_GetObjectItem(j, "serial");   /* Bambu device serial -> uuid */
            if (cJSON_IsString(h)) strlcpy(p.host, h->valuestring, sizeof(p.host));
            strlcpy(p.name, cJSON_IsString(n) && n->valuestring[0] ? n->valuestring : p.host, sizeof(p.name));
            if (cJSON_IsString(k)) strlcpy(p.api_key, k->valuestring, sizeof(p.api_key));
            if (cJSON_IsString(s)) strlcpy(p.uuid, s->valuestring, sizeof(p.uuid));
            p.port = 80;
            if (p.host[0]) {
                if (printer_store_add(&p) < 0) {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Printer limit reached");
                    cJSON_Delete(j);
                    free(body);
                    return ESP_FAIL;
                }
                app_state_printers_changed();
            }
            cJSON_Delete(j);
        }
        free(body);
    }
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

/* Edit an existing printer by index. An empty/omitted "key" keeps the stored key. */
static esp_err_t printers_update_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    if (j) {
        const cJSON *iv = cJSON_GetObjectItem(j, "i");
        int idx = cJSON_IsNumber(iv) ? (int)iv->valuedouble : -1;
        pp_printer_t p;
        if (idx >= 0 && printer_store_get(idx, &p)) {   /* start from existing (keeps key) */
            const cJSON *n = cJSON_GetObjectItem(j, "name");
            const cJSON *h = cJSON_GetObjectItem(j, "host");
            const cJSON *k = cJSON_GetObjectItem(j, "key");
            const cJSON *s = cJSON_GetObjectItem(j, "serial");
            if (cJSON_IsString(n) && n->valuestring[0]) strlcpy(p.name, n->valuestring, sizeof(p.name));
            if (cJSON_IsString(h) && h->valuestring[0]) strlcpy(p.host, h->valuestring, sizeof(p.host));
            if (cJSON_IsString(k) && k->valuestring[0]) strlcpy(p.api_key, k->valuestring, sizeof(p.api_key));
            if (cJSON_IsString(s)) strlcpy(p.uuid, s->valuestring, sizeof(p.uuid));   /* Bambu serial */
            printer_store_update(idx, &p);
            app_state_printers_changed();
        }
        cJSON_Delete(j);
    }
    free(body);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t printers_remove_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    if (j) {
        const cJSON *iv = cJSON_GetObjectItem(j, "i");
        if (cJSON_IsNumber(iv)) { printer_store_remove((int)iv->valuedouble); app_state_printers_changed(); }
        cJSON_Delete(j);
    }
    free(body);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t printers_active_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    if (j) {
        const cJSON *iv = cJSON_GetObjectItem(j, "i");
        if (cJSON_IsNumber(iv)) printer_store_set_active((int)iv->valuedouble);
        cJSON_Delete(j);
    }
    free(body);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad/oversized body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    if (j) {
        const cJSON *s = cJSON_GetObjectItem(j, "ssid");
        const cJSON *p = cJSON_GetObjectItem(j, "pass");
        if (cJSON_IsString(s) && s->valuestring[0]) {
            app_state_wifi_connect(s->valuestring, cJSON_IsString(p) ? p->valuestring : "");
        }
        cJSON_Delete(j);
    }
    free(body);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t ota_post(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no partition"); return ESP_FAIL; }
    if (req->content_len <= 0 || (size_t)req->content_len > part->size) {
        httpd_resp_set_hdr(req, "Connection", "close");   /* stop the browser streaming the rest */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "oversized");
        return ESP_FAIL;
    }
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(part, req->content_len, &ota) != ESP_OK) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "begin fail"); return ESP_FAIL; }
    char buf[1024];
    size_t remaining = (size_t)req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
        if (r <= 0) { esp_ota_abort(ota); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv fail"); return ESP_FAIL; }
        /* A write failure (bad flash / out of space) must abort — never boot a partial image. */
        if (esp_ota_write(ota, buf, r) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
            return ESP_FAIL;
        }
        remaining -= r;
    }
    /* esp_ota_end validates the image (hash/magic) — if it fails, do NOT set it bootable. */
    if (esp_ota_end(ota) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "image invalid");
        return ESP_FAIL;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set-boot fail");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "OK - rebooting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static ota_check_t s_upd;
static bool s_upd_have;
static volatile bool s_upd_busy;
static SemaphoreHandle_t s_upd_mtx;
static SemaphoreHandle_t s_preview_mtx;   /* serializes off-screen layout-preview renders */

static void upd_check_task(void *arg)
{
    ota_check_t tmp;
    bool ok = ota_update_check(&tmp);
    xSemaphoreTake(s_upd_mtx, portMAX_DELAY);
    if (ok) { s_upd = tmp; s_upd_have = true; }
    s_upd_busy = false;
    xSemaphoreGive(s_upd_mtx);
    vTaskDelete(NULL);
}

static esp_err_t update_check_get(httpd_req_t *req)
{
    ota_check_t snap; bool have, busy, spawn = false;
    xSemaphoreTake(s_upd_mtx, portMAX_DELAY);
    if (!s_upd_busy) { s_upd_busy = true; spawn = true; }
    busy = s_upd_busy; snap = s_upd; have = s_upd_have;
    xSemaphoreGive(s_upd_mtx);
    if (spawn) xTaskCreate(upd_check_task, "upd_chk", 8192, NULL, 4, NULL);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "checking", busy);
    cJSON_AddStringToObject(o, "current", have ? snap.current : PP_FW_VERSION);
    cJSON_AddStringToObject(o, "latest", have ? snap.latest : "");
    cJSON_AddBoolToObject(o, "available", have ? snap.available : false);
    cJSON_AddStringToObject(o, "url", have ? snap.url : "");
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(o);
    return ESP_OK;
}

static esp_err_t update_progress_get(httpd_req_t *req)
{
    int p = ota_update_get_progress();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "progress", p);
    cJSON_AddStringToObject(o, "state", p == -2 ? "error" : p >= 100 ? "done" : p >= 0 ? "running" : "idle");
    cJSON_AddStringToObject(o, "msg", ota_update_get_msg());
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(o);
    return ESP_OK;
}

static void ota_apply_task(void *arg) { ota_update_apply(arg); free(arg); vTaskDelete(NULL); }

static esp_err_t update_apply_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (body) {
        cJSON *j = cJSON_Parse(body);
        if (j) {
            const cJSON *u = cJSON_GetObjectItem(j, "url");
            if (cJSON_IsString(u)) xTaskCreate(ota_apply_task, "ota", 8192, strdup(u->valuestring), 5, NULL);
            cJSON_Delete(j);
        }
        free(body);
    }
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t info_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", "Prusa Connect Touch");
    cJSON_AddStringToObject(o, "fw", PP_FW_VERSION);
    cJSON_AddNumberToObject(o, "heap_free", (double)esp_get_free_heap_size());
    /* Internal RAM is the scarce resource (TLS/mbedTLS allocates here, not PSRAM). Surface
     * it so OOM regressions are visible — total free heap is mostly PSRAM and hides this. */
    cJSON_AddNumberToObject(o, "heap_internal", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(o, "heap_internal_min", (double)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(o, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    /* Security opt-in state (booleans only — never the actual PIN/password). */
    cJSON_AddBoolToObject(o, "webauth", prefs_web_pass()[0] != '\0');
    cJSON_AddBoolToObject(o, "scrlock", prefs_scrpin()[0] != '\0');
    cJSON_AddNumberToObject(o, "lockmin", prefs_lock_min());
    cJSON_AddNumberToObject(o, "reboot_hour", prefs_reboot_hour());   /* 0..23 or 255 = off */
    cJSON_AddNumberToObject(o, "tz_offset", prefs_tz_offset());
    cJSON_AddNumberToObject(o, "lang", prefs_lang());
    { cJSON *la = cJSON_CreateArray();
      for (int i = 0; i < LANG_COUNT; i++) cJSON_AddItemToArray(la, cJSON_CreateString(i18n_lang_label((pp_lang_t)i)));
      cJSON_AddItemToObject(o, "langs", la); }
    { time_t t = time(NULL); cJSON_AddNumberToObject(o, "clock_ok", t > 1700000000 ? 1 : 0);
      if (t > 1700000000) { time_t l = t + (time_t)prefs_tz_offset() * 3600; struct tm tm; gmtime_r(&l, &tm);
        char hm[6]; snprintf(hm, sizeof(hm), "%02d:%02d", tm.tm_hour, tm.tm_min); cJSON_AddStringToObject(o, "local", hm); } }
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(o);
    return ESP_OK;
}

static esp_err_t fleet_get(httpd_req_t *req)
{
    pp_status_t *arr = heap_caps_malloc(PP_MAX_PRINTERS * sizeof(pp_status_t), MALLOC_CAP_SPIRAM);
    if (!arr) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); return ESP_FAIL; }
    int n = 0; app_state_get_fleet(arr, PP_MAX_PRINTERS, &n);
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", arr[i].printer_name);
        cJSON_AddBoolToObject(e, "online", arr[i].online);
        cJSON_AddStringToObject(e, "state", arr[i].state);
        cJSON_AddNumberToObject(e, "nozzle", (int)arr[i].temp_nozzle);
        cJSON_AddNumberToObject(e, "tnozzle", (int)arr[i].target_nozzle);
        cJSON_AddNumberToObject(e, "bed", (int)arr[i].temp_bed);
        cJSON_AddNumberToObject(e, "tbed", (int)arr[i].target_bed);
        cJSON_AddBoolToObject(e, "printing", arr[i].has_job);
        cJSON_AddNumberToObject(e, "progress", (int)(arr[i].progress + 0.5f));
        cJSON_AddStringToObject(e, "job", arr[i].job_name);
        cJSON_AddNumberToObject(e, "speed", arr[i].speed);
        cJSON_AddNumberToObject(e, "z", arr[i].axis_z);
        cJSON_AddStringToObject(e, "uuid", arr[i].uuid);      /* control target (cloud) */
        cJSON_AddBoolToObject(e, "cloud", arr[i].is_cloud);
        cJSON_AddBoolToObject(e, "ctl", arr[i].has_control);  /* show control panel */
        cJSON_AddItemToArray(a, e);
    }
    char *js = cJSON_PrintUnformatted(a);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(a); heap_caps_free(arr);
    return ESP_OK;
}

static esp_err_t config_export_get(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < printer_store_count(); i++) {
        pp_printer_t p; if (!printer_store_get(i, &p)) continue;
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", p.name);
        cJSON_AddStringToObject(e, "host", p.host);
        cJSON_AddNumberToObject(e, "port", p.port);
        cJSON_AddStringToObject(e, "key", p.api_key);
        cJSON_AddItemToArray(arr, e);
    }
    char *js = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(arr);
    return ESP_OK;
}

static esp_err_t config_import_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body);
    if (!cJSON_IsArray(j)) { if (j) cJSON_Delete(j); free(body); return ESP_FAIL; }
    printer_store_clear();
    const cJSON *e = NULL;
    cJSON_ArrayForEach(e, j) {
        pp_printer_t p = {0};
        const cJSON *n = cJSON_GetObjectItem(e, "name");
        const cJSON *h = cJSON_GetObjectItem(e, "host");
        const cJSON *po = cJSON_GetObjectItem(e, "port");
        const cJSON *k = cJSON_GetObjectItem(e, "key");
        if (cJSON_IsString(h)) strlcpy(p.host, h->valuestring, sizeof(p.host));
        strlcpy(p.name, cJSON_IsString(n) && n->valuestring[0] ? n->valuestring : p.host, sizeof(p.name));
        p.port = cJSON_IsNumber(po) ? (int)po->valuedouble : 80;
        if (cJSON_IsString(k)) strlcpy(p.api_key, k->valuestring, sizeof(p.api_key));
        if (p.host[0]) printer_store_add(&p);
    }
    app_state_printers_changed();
    cJSON_Delete(j); free(body);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t connect_info_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "auth", prusa_connect_is_authenticated());
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(o);
    return ESP_OK;
}

static esp_err_t connect_login_post(httpd_req_t *req)
{
    char *body = recv_body(req); if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body); pp_connect_status_t res = PP_CONNECT_ERROR;
    if (j) {
        const cJSON *e = cJSON_GetObjectItem(j, "e"), *p = cJSON_GetObjectItem(j, "p");
        const cJSON *rem = cJSON_GetObjectItem(j, "rem");
        /* Set the remember flag BEFORE login so its success path persists creds (or not). */
        prusa_connect_set_remember(cJSON_IsBool(rem) ? cJSON_IsTrue(rem) : true);
        if (cJSON_IsString(e) && cJSON_IsString(p)) res = prusa_connect_login(e->valuestring, p->valuestring);
        cJSON_Delete(j);
    }
    free(body);
    cJSON *r = cJSON_CreateObject();
    if (res == PP_CONNECT_AUTH_OK) cJSON_AddStringToObject(r, "res", "ok");
    else if (res == PP_CONNECT_NEED_TOTP) cJSON_AddStringToObject(r, "res", "totp");
    else cJSON_AddStringToObject(r, "res", "err");
    char *js = cJSON_PrintUnformatted(r); httpd_resp_sendstr(req, js); free(js); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t connect_totp_post(httpd_req_t *req)
{
    char *body = recv_body(req); if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body); pp_connect_status_t res = PP_CONNECT_ERROR;
    if (j) {
        const cJSON *c = cJSON_GetObjectItem(j, "c");
        if (cJSON_IsString(c)) res = prusa_connect_submit_totp(c->valuestring);
        cJSON_Delete(j);
    }
    free(body);
    cJSON *r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "res", res == PP_CONNECT_AUTH_OK ? "ok" : "err");
    char *js = cJSON_PrintUnformatted(r); httpd_resp_sendstr(req, js); free(js); cJSON_Delete(r);
    return ESP_OK;
}

static esp_err_t connect_fleet_get(httpd_req_t *req)
{
    pp_status_t *arr = heap_caps_malloc(64 * sizeof(pp_status_t), MALLOC_CAP_SPIRAM);
    int n = 0;
    if (!arr || prusa_connect_get_fleet(arr, 64, &n) != ESP_OK) { if (arr) heap_caps_free(arr); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fail"); }
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", arr[i].printer_name);
        cJSON_AddStringToObject(e, "model", arr[i].model);
        cJSON_AddStringToObject(e, "uuid", arr[i].uuid);
        cJSON_AddStringToObject(e, "state", arr[i].state);
        cJSON_AddBoolToObject(e, "online", arr[i].online);
        cJSON_AddStringToObject(e, "team", arr[i].team);
        cJSON_AddStringToObject(e, "fw", arr[i].firmware);
        cJSON_AddNumberToObject(e, "nozzle", arr[i].temp_nozzle);
        cJSON_AddNumberToObject(e, "bed", arr[i].temp_bed);
        cJSON_AddItemToArray(a, e);
    }
    char *js = cJSON_PrintUnformatted(a);
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(a); heap_caps_free(arr);
    return ESP_OK;
}

/* Teams come per-printer in /app/printers (team_id/team_name), so derive the
 * distinct team list from the fleet — no separate (broken) mobile teams endpoint. */
static esp_err_t connect_teams_get(httpd_req_t *req)
{
    pp_status_t *arr = heap_caps_malloc(64 * sizeof(pp_status_t), MALLOC_CAP_SPIRAM);
    int n = 0;
    if (!arr || prusa_connect_get_fleet(arr, 64, &n) != ESP_OK) { if (arr) heap_caps_free(arr); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fail"); }
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (!arr[i].team[0]) continue;
        bool seen = false;
        cJSON *t = NULL;
        cJSON_ArrayForEach(t, a) {
            cJSON *nm = cJSON_GetObjectItemCaseSensitive(t, "name");
            if (cJSON_IsString(nm) && strcmp(nm->valuestring, arr[i].team) == 0) { seen = true; break; }
        }
        if (seen) continue;
        cJSON *e = cJSON_CreateObject();
        char idbuf[16]; snprintf(idbuf, sizeof(idbuf), "%d", arr[i].team_id);
        cJSON_AddStringToObject(e, "id", idbuf);
        cJSON_AddStringToObject(e, "name", arr[i].team);
        cJSON_AddStringToObject(e, "role", "member");
        cJSON_AddItemToArray(a, e);
    }
    char *js = cJSON_PrintUnformatted(a);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(a); heap_caps_free(arr);
    return ESP_OK;
}

static esp_err_t connect_logout_post(httpd_req_t *req)
{
    prusa_connect_logout();
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t connect_farmprobe_get(httpd_req_t *req)
{
    char buf[300];
    prusa_connect_farm_probe(buf, sizeof(buf));
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t connect_setorg_post(httpd_req_t *req)
{
    char buf[64]; int r = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (r <= 0) return ESP_FAIL;
    buf[r] = '\0';
    prusa_connect_set_org(buf);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t connect_farm_get(httpd_req_t *req)
{
    char q[160], org[48] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) httpd_query_key_value(q, "org", org, sizeof(org));
    char *j = prusa_connect_get_farm_stats(org);
    if (!j) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fail");
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, j); free(j);
    return ESP_OK;
}

static esp_err_t connect_orders_get(httpd_req_t *req)
{
    char q[160], org[48] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) httpd_query_key_value(q, "org", org, sizeof(org));
    char *j = prusa_connect_get_orders(org);
    if (!j) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fail");
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, j); free(j);
    return ESP_OK;
}

/* DEBUG/TEST: set screen orientation (0=landscape, 1=flipped 180°) — drives the same
 * app_state path as the on-device dropdown, so it can be smoke-tested remotely. */
static esp_err_t test_orient_get(httpd_req_t *req)
{
    char q[64], v[8] = {0};
    int o = 0;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && httpd_query_key_value(q, "o", v, sizeof(v)) == ESP_OK)
        o = atoi(v);
    if (o < 0 || o > 3) o = 0;   /* 0=landscape 1=180 2=portrait 3=portrait-flipped */
    app_state_set_pref(PP_PREF_ORIENT, o);
    char body[20]; snprintf(body, sizeof(body), "{\"orient\":%d}", o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

/* DEBUG: dump the raw per-printer Connect JSON (for inspecting dialog_info / network_info). */
static esp_err_t connect_printer_raw_get(httpd_req_t *req)
{
    char q[160], uuid[48] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "uuid", uuid, sizeof(uuid)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing uuid");
    char *j = prusa_connect_get_printer_raw(uuid);
    if (!j) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fail");
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, j); free(j);
    return ESP_OK;
}

static esp_err_t connect_ctrlprobe_get(httpd_req_t *req)
{
    char q[200], uuid[48] = {0}, cmd[48] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK || httpd_query_key_value(q, "uuid", uuid, sizeof(uuid)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing uuid");
    httpd_query_key_value(q, "cmd", cmd, sizeof(cmd));   /* optional */
    char buf[300];
    prusa_connect_ctrl_probe(uuid, cmd, buf, sizeof(buf));
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* Printer control from the web UI (mirrors the touchscreen Control screen). uuid +
 * op are query params (no spaces); for op=gcode/jog the G-code line is the POST body.
 * jog wraps the move in G91/G90 (relative) and restores absolute, exactly like the
 * touch's on_jog_clicked, so the web and device drive the printer identically. */
static esp_err_t connect_control_post(httpd_req_t *req)
{
    char q[200], uuid[48] = {0}, op[16] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "uuid", uuid, sizeof(uuid)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing uuid");
    httpd_query_key_value(q, "op", op, sizeof(op));

    /* Numeric params (optional, per op). */
    char v[16];
    int n = (httpd_query_key_value(q, "n", v, sizeof(v)) == ESP_OK) ? atoi(v) : 0;   /* nozzle °C */
    int b = (httpd_query_key_value(q, "b", v, sizeof(v)) == ESP_OK) ? atoi(v) : 0;   /* bed °C    */
    int f = (httpd_query_key_value(q, "f", v, sizeof(v)) == ESP_OK) ? atoi(v) : 3000;/* feedrate  */
    float dx = (httpd_query_key_value(q, "x", v, sizeof(v)) == ESP_OK) ? atof(v) : 0;
    float dy = (httpd_query_key_value(q, "y", v, sizeof(v)) == ESP_OK) ? atof(v) : 0;
    float dz = (httpd_query_key_value(q, "z", v, sizeof(v)) == ESP_OK) ? atof(v) : 0;

    /* Modern Connect cloud printers use dedicated commands (the web fleet is all cloud).
     * Klipper/Moonraker control stays on the touchscreen's backend-aware gcode path. */
    esp_err_t rc = ESP_FAIL;
    if (!strcmp(op, "pause"))        rc = prusa_connect_pause(uuid);
    else if (!strcmp(op, "resume"))  rc = prusa_connect_resume(uuid);
    else if (!strcmp(op, "stop"))    rc = prusa_connect_stop(uuid);
    else if (!strcmp(op, "preheat")) { prusa_connect_set_nozzle_temp(uuid, n); rc = prusa_connect_set_bed_temp(uuid, b); }
    else if (!strcmp(op, "home"))    rc = prusa_connect_home(uuid, "XYZ");
    else if (!strcmp(op, "move"))    rc = prusa_connect_move(uuid, f, dx, dy);
    else if (!strcmp(op, "movez"))   rc = prusa_connect_move_z(uuid, f, dz);
    else return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad op");

    char out[32]; snprintf(out, sizeof(out), "{\"ok\":%s}", rc == ESP_OK ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

/* Proxy a cloud printer's webcam snapshot to the browser. The device fetches the JPEG
 * from Connect with its Bearer token (the browser can't — token lives on the device) and
 * streams the bytes back as image/jpeg. 404 if the printer has no camera / no frame, so
 * the <img onerror> can hide itself cleanly. */
static esp_err_t connect_snapshot_get(httpd_req_t *req)
{
    char q[200], uuid[48] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "uuid", uuid, sizeof(uuid)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing uuid");
    uint8_t *buf = NULL; int len = 0;
    if (prusa_connect_fetch_snapshot(uuid, &buf, &len) != ESP_OK || !buf || len <= 0) {
        if (buf) free(buf);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "", 0);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, (const char *)buf, len);
    free(buf);
    return ESP_OK;
}

/* Self-test: decode + display an embedded real 250x140 camera frame on the Control screen.
 * Proves the on-device JPEG path (TJPGD + MEMFS + 1:1 render) works without a cloud token —
 * the live fetch is verified separately by the /api/connect/snapshot proxy. */
static esp_err_t test_webcam_get(httpd_req_t *req)
{
    pp_image_t *im = malloc(sizeof(*im));
    if (im) {
        im->len  = wc_test_jpg_len;
        im->data = malloc(wc_test_jpg_len);
        if (im->data) {
            memcpy(im->data, wc_test_jpg, wc_test_jpg_len);
            if (pt_display_schedule_ui(ui_apply_snapshot, im) != LV_RESULT_OK) { free(im->data); free(im); }
        } else { free(im); }
    }
    ui_request_screen("control");
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

/* Self-test: render a representative active-orders Farm view on the touch Farm screen,
 * proving ui_apply_farm displays active orders without needing live GraphQL data (the
 * fetch/parse path is exercised separately by /api/connect/farm + /orders). Caller should
 * already be on the Farm screen; pushes the data only (no nav) so a real refresh can't race. */
/* Schema probe: POST a raw GraphQL body, get the connect-api response (device has no CORS).
 * Used to introspect the farm Order type (order state + piece-count field names). */
static esp_err_t test_gql_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing body");
    char *resp = prusa_connect_graphql_raw(body);
    free(body);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp ? resp : "{\"error\":\"null\"}");
    free(resp);
    return ESP_OK;
}

static esp_err_t test_farm_get(httpd_req_t *req)
{
    pp_farm_t *f = malloc(sizeof(*f));
    if (f) {
        memset(f, 0, sizeof(*f));
        f->valid = true;
        f->p_active = 1; f->p_online = 5; f->p_error = 0; f->p_total = 5;
        f->order_count = 2;
        strlcpy(f->orders[0].name, "Sample order #1042", sizeof(f->orders[0].name));
        f->orders[0].done = 2; f->orders[0].total = 8; f->orders[0].attn = 0;
        strlcpy(f->orders[1].name, "Sample batch", sizeof(f->orders[1].name));
        f->orders[1].done = 0; f->orders[1].total = 4; f->orders[1].attn = 1;
        if (pt_display_schedule_ui(ui_apply_farm, f) != LV_RESULT_OK) free(f);
    }
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t connect_team_printers_get(httpd_req_t *req)
{
    char q[128], id[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK || httpd_query_key_value(q, "id", id, sizeof(id)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
    
    int want = atoi(id);
    pp_status_t *arr = heap_caps_malloc(64 * sizeof(pp_status_t), MALLOC_CAP_SPIRAM);
    int n = 0;
    if (!arr || prusa_connect_get_fleet(arr, 64, &n) != ESP_OK) {
        if (arr) heap_caps_free(arr);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fail");
    }
    /* Filter the fleet to this team (team_id is per-printer in /app/printers). */
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (arr[i].team_id != want) continue;
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", arr[i].printer_name);
        cJSON_AddStringToObject(e, "model", arr[i].model);
        cJSON_AddStringToObject(e, "uuid", arr[i].uuid);
        cJSON_AddItemToArray(a, e);
    }
    char *js = cJSON_PrintUnformatted(a);
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(a); heap_caps_free(arr);
    return ESP_OK;
}

static esp_err_t connect_default_team_get(httpd_req_t *req)
{
    httpd_resp_sendstr(req, prusa_connect_get_default_team());
    return ESP_OK;
}

static esp_err_t connect_default_team_post(httpd_req_t *req)
{
    char buf[64];
    int r = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (r <= 0) return ESP_FAIL;
    buf[r] = '\0';
    prusa_connect_set_default_team(buf);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

/* Stream a native-endian RGB565 image as a 24-bit bottom-up BMP over a chunked httpd response.
 * src_stride_px = uint16 per source row (w for a packed buffer; draw_buf->header.stride/2 otherwise). */
static esp_err_t bmp_send(httpd_req_t *req, const uint16_t *rgb565, int w, int h, int src_stride_px)
{
    const int rowbytes = w * 3;
    const int pad = (4 - (rowbytes & 3)) & 3;        /* BMP rows align to a 4-byte boundary */
    const int stride = rowbytes + pad;
    uint8_t hdr[54] = { 'B','M', 0 };
    uint32_t imgsize = (uint32_t)stride * h, v = 54 + imgsize; memcpy(&hdr[2],&v,4); hdr[10]=54; v=40; memcpy(&hdr[14],&v,4);
    int32_t iw=w, ih=h; memcpy(&hdr[18],&iw,4); memcpy(&hdr[22],&ih,4); hdr[26]=1; hdr[28]=24;
    httpd_resp_set_type(req, "image/bmp");
    if (httpd_resp_send_chunk(req, (const char*)hdr, 54) != ESP_OK) return ESP_FAIL;
    uint8_t *row = calloc(1, stride);                /* calloc => the padding bytes are zero */
    if (!row) { httpd_resp_send_chunk(req, NULL, 0); return ESP_FAIL; }
    esp_err_t e = ESP_OK;
    for (int y = h-1; y >= 0 && e == ESP_OK; y--) {
        const uint16_t *line = rgb565 + (size_t)y * src_stride_px;
        for (int x = 0; x < w; x++) {
            uint16_t c = line[x];
            row[x*3+0]=(c&0x1F)<<3; row[x*3+1]=((c>>5)&0x3F)<<2; row[x*3+2]=((c>>11)&0x1F)<<3;
        }
        e = httpd_resp_send_chunk(req, (const char*)row, stride);   /* abort the stream if the client hung up */
    }
    free(row);
    if (e == ESP_OK) httpd_resp_send_chunk(req, NULL, 0);   /* terminate cleanly only on success */
    return e;
}

static esp_err_t screen_get(httpd_req_t *req)
{
    esp_lcd_panel_handle_t panel = pt_get_panel(); void *fb = NULL;
    if (!panel || esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || !fb)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no fb");
    return bmp_send(req, (const uint16_t*)fb, 800, 480, 800);   /* panel fb is packed at 800px */
}

static esp_err_t ui_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "screen", ui_current_screen());
    char *js = cJSON_PrintUnformatted(o); httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(o); return ESP_OK;
}

static esp_err_t ui_nav_get(httpd_req_t *req)
{
    char q[64]={0}, s[24]={0}; if (httpd_req_get_url_query_str(req, q, 64) == ESP_OK) httpd_query_key_value(q, "screen", s, 24);
    if (s[0]) ui_request_screen(s);
    httpd_resp_sendstr(req, "ok"); return ESP_OK;
}

/* ---- network log pipe: GET /api/log?since=<seq> -> recent console output ("serial over WiFi") ---- */
static esp_err_t log_get(httpd_req_t *req)
{
    uint32_t since = 0;
    char q[64] = {0}, sv[24] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "since", sv, sizeof(sv)) == ESP_OK)
        since = (uint32_t)strtoul(sv, NULL, 10);
    char *buf = malloc(8192);
    if (!buf) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); return ESP_FAIL; }
    uint32_t head = 0, oldest = 0;
    size_t n = netlog_read(since, buf, 8192, &head, &oldest);
    char h[16]; snprintf(h, sizeof(h), "%u", (unsigned)head);
    char o[16]; snprintf(o, sizeof(o), "%u", (unsigned)oldest);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "X-Log-Head", h);       /* client uses this as the next ?since= */
    httpd_resp_set_hdr(req, "X-Log-Oldest", o);     /* if your since < this, you missed some  */
    httpd_resp_send(req, buf, n);
    free(buf);
    return ESP_OK;
}

/* ---- Bambu Lab cloud (ALPHA) ---- */
static esp_err_t bambu_info_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "auth", bambu_cloud_is_authed());
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(o); return ESP_OK;
}
static void send_res(httpd_req_t *req, const char *res)
{
    cJSON *r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "res", res);
    char *js = cJSON_PrintUnformatted(r);
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(r);
}
static esp_err_t bambu_login_post(httpd_req_t *req)
{
    char *body = recv_body(req); if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body); free(body);
    const char *res = "err";
    if (j) {
        cJSON *e = cJSON_GetObjectItem(j, "email"), *p = cJSON_GetObjectItem(j, "password");
        if (cJSON_IsString(e) && cJSON_IsString(p)) {
            bc_status_t st = bambu_cloud_login(e->valuestring, p->valuestring);
            res = st == BC_OK ? "ok" : st == BC_NEED_CODE ? "code" : "err";
        }
        cJSON_Delete(j);
    }
    send_res(req, res); return ESP_OK;
}
static esp_err_t bambu_code_post(httpd_req_t *req)
{
    char *body = recv_body(req); if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body); free(body);
    const char *res = "err";
    if (j) {
        cJSON *e = cJSON_GetObjectItem(j, "email"), *c = cJSON_GetObjectItem(j, "code");
        if (cJSON_IsString(e) && cJSON_IsString(c))
            res = (bambu_cloud_submit_code(e->valuestring, c->valuestring) == BC_OK) ? "ok" : "err";
        cJSON_Delete(j);
    }
    send_res(req, res); return ESP_OK;
}
static esp_err_t bambu_token_post(httpd_req_t *req)
{
    char *body = recv_body(req); if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body); free(body);
    const char *res = "err";
    if (j) {
        cJSON *t = cJSON_GetObjectItem(j, "token");
        if (cJSON_IsString(t)) res = (bambu_cloud_set_token(t->valuestring) == ESP_OK) ? "ok" : "err";
        cJSON_Delete(j);
    }
    send_res(req, res); return ESP_OK;
}
static esp_err_t bambu_pull_post(httpd_req_t *req)
{
    pp_printer_t *devs = heap_caps_malloc(16 * sizeof(pp_printer_t), MALLOC_CAP_SPIRAM);
    if (!devs) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); return ESP_FAIL; }
    int n = bambu_cloud_list_devices(devs, 16), added = 0;
    for (int i = 0; i < n; i++) {
        bool exists = false;
        for (int k = 0; k < printer_store_count(); k++) {
            pp_printer_t e;
            if (printer_store_get(k, &e) && strcmp(e.host, devs[i].host) == 0) { exists = true; break; }
        }
        if (!exists && printer_store_add(&devs[i]) >= 0) added++;   /* httpd task: NVS write is safe */
    }
    heap_caps_free(devs);
    app_state_printers_changed();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "found", n); cJSON_AddNumberToObject(r, "added", added);
    char *js = cJSON_PrintUnformatted(r);
    httpd_resp_set_type(req, "application/json"); httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(r); return ESP_OK;
}
static esp_err_t bambu_logout_post(httpd_req_t *req)
{
    bambu_cloud_logout(); httpd_resp_sendstr(req, "ok"); return ESP_OK;
}

/* ---- opt-in web-interface auth (HTTP Basic) ----
 * When a web password is set, every route is gated. Empty password => open (the default).
 * Each route is registered through auth_wrap, which checks Basic auth then calls the real
 * handler stashed in user_ctx. */
static bool web_authed(httpd_req_t *req)
{
    const char *pw = prefs_web_pass();
    if (!pw[0]) return true;                       /* auth disabled */
    size_t hl = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hl == 0 || hl > 200) return false;
    char hdr[208];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) return false;
    if (strncmp(hdr, "Basic ", 6) != 0) return false;
    unsigned char dec[160]; size_t dl = 0;
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &dl, (const unsigned char *)hdr + 6, strlen(hdr + 6)) != 0) return false;
    dec[dl] = '\0';
    const char *colon = strchr((char *)dec, ':');   /* "user:pass" — username ignored */
    const char *given = colon ? colon + 1 : (char *)dec;
    return strcmp(given, pw) == 0;
}

static esp_err_t web_unauth(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Prusa Touch\"");
    httpd_resp_sendstr(req, "Authentication required");
    return ESP_OK;
}

static esp_err_t auth_wrap(httpd_req_t *req)
{
    esp_err_t (*real)(httpd_req_t *) = req->user_ctx;
    if (!web_authed(req)) return web_unauth(req);
    return real(req);
}

/* Set the security opt-ins: {webpw, pin, lockmin}. Each field optional; "" clears.
 * Runs on the httpd task (internal stack), so the NVS writes are safe here. */
static esp_err_t security_post(httpd_req_t *req)
{
    char *body = recv_body(req); if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body);
    if (j) {
        cJSON *wp = cJSON_GetObjectItem(j, "webpw");
        cJSON *pin = cJSON_GetObjectItem(j, "pin");
        cJSON *lm = cJSON_GetObjectItem(j, "lockmin");
        if (cJSON_IsString(wp)) prefs_set_web_pass(wp->valuestring);
        if (cJSON_IsString(pin)) prefs_set_scrpin(pin->valuestring);
        if (cJSON_IsNumber(lm)) { int m = lm->valueint; prefs_set_lock_min(m < 0 ? 0 : m > 240 ? 240 : (uint8_t)m); }
        cJSON_Delete(j);
        pt_display_schedule_ui(ui_apply_lock_cfg, NULL);   /* re-arm the idle-lock timer */
    }
    free(body);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

/* Daily maintenance reboot config: {hour:0..23 (>23 disables), tz:UTC offset hours}. */
static esp_err_t reboot_post(httpd_req_t *req)
{
    char *body = recv_body(req); if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body);
    if (j) {
        cJSON *hr = cJSON_GetObjectItem(j, "hour");
        cJSON *tz = cJSON_GetObjectItem(j, "tz");
        if (cJSON_IsNumber(hr)) prefs_set_reboot_hour((uint8_t)hr->valueint);
        if (cJSON_IsNumber(tz)) prefs_set_tz_offset((int8_t)tz->valueint);
        cJSON_Delete(j);
    }
    free(body);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

/* Set UI language {lang:N} and reboot so every screen rebuilds in the new language. */
static esp_err_t lang_post(httpd_req_t *req)
{
    char *body = recv_body(req); if (!body) return ESP_FAIL;
    cJSON *j = cJSON_Parse(body);
    int l = -1;
    if (j) { cJSON *n = cJSON_GetObjectItem(j, "lang"); if (cJSON_IsNumber(n)) l = n->valueint; cJSON_Delete(j); }
    free(body);
    if (l < 0 || l >= LANG_COUNT) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad lang"); return ESP_FAIL; }
    prefs_set_lang((uint8_t)l);
    httpd_resp_sendstr(req, "OK - rebooting");
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
    return ESP_OK;
}

/* ---- Skins (issue #6 Phase 1b: the ThemeForge web editor) ---- */
static bool parse_hex_color(const char *s, uint8_t out[3])   /* "#rrggbb" -> 3 bytes */
{
    if (!s) return false;
    if (*s == '#') s++;
    if (strlen(s) != 6) return false;
    for (int k = 0; k < 3; k++) {
        char buf[3] = { s[k*2], s[k*2+1], 0 }, *end;
        long v = strtol(buf, &end, 16);
        if (*end) return false;
        out[k] = (uint8_t)v;
    }
    return true;
}

static esp_err_t skin_get(httpd_req_t *req)   /* current skin + preset names + the active 19 colors */
{
    uint8_t rgb[57]; skin_palette_rgb(skin_current(), rgb);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "current", skin_current());
    cJSON_AddNumberToObject(o, "custom", skin_custom_index());
    cJSON *names = cJSON_CreateArray();
    for (int i = 0; i < skin_count(); i++) cJSON_AddItemToArray(names, cJSON_CreateString(skin_name(i)));
    cJSON_AddItemToObject(o, "names", names);
    cJSON *cols = cJSON_CreateObject();
    for (int i = 0; i < 19; i++) {
        char hex[8]; snprintf(hex, sizeof(hex), "#%02x%02x%02x", rgb[i*3], rgb[i*3+1], rgb[i*3+2]);
        cJSON_AddStringToObject(cols, SKIN_TOKENS[i], hex);
    }
    cJSON_AddItemToObject(o, "colors", cols);
    cJSON_AddNumberToObject(o, "font", skin_font());
    cJSON_AddStringToObject(o, "brand", skin_brand());
    cJSON_AddStringToObject(o, "byline", skin_byline());
    /* Built-in presets (0..custom-1) with full palettes, so the editor can list + load them. */
    cJSON *presets = cJSON_CreateArray();
    for (int p = 0; p < skin_custom_index(); p++) {
        uint8_t prgb[57]; skin_palette_rgb(p, prgb);
        cJSON *pe = cJSON_CreateObject();
        cJSON_AddNumberToObject(pe, "index", p);
        cJSON_AddStringToObject(pe, "name", skin_name(p));
        cJSON *pc = cJSON_CreateObject();
        for (int i = 0; i < 19; i++) {
            char hx[8]; snprintf(hx, sizeof(hx), "#%02x%02x%02x", prgb[i*3], prgb[i*3+1], prgb[i*3+2]);
            cJSON_AddStringToObject(pc, SKIN_TOKENS[i], hx);
        }
        cJSON_AddItemToObject(pe, "colors", pc);
        cJSON_AddNumberToObject(pe, "font", skin_font_of(p));
        cJSON_AddStringToObject(pe, "brand", skin_brand_of(p));
        cJSON_AddStringToObject(pe, "byline", skin_byline_of(p));
        cJSON_AddItemToArray(presets, pe);
    }
    cJSON_AddItemToObject(o, "presets", presets);
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js ? js : "{}");
    free(js); cJSON_Delete(o);
    return ESP_OK;
}

static esp_err_t skin_post(httpd_req_t *req)   /* body {colors:{19 #rrggbb}} -> store custom + reboot */
{
    char *body = recv_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    bool ok = false;
    /* {index:N} selects a built-in preset (incl. 0 = Connect default) — no custom write, just reboot. */
    if (j) {
        cJSON *jidx = cJSON_GetObjectItem(j, "index");
        if (cJSON_IsNumber(jidx) && jidx->valueint >= 0 && jidx->valueint < skin_custom_index()) {
            int idx = jidx->valueint;
            cJSON_Delete(j); free(body);
            httpd_resp_sendstr(req, "ok");
            app_state_set_pref(PP_PREF_SKIN, idx);   /* persist active idx on the net task + reboot */
            return ESP_OK;
        }
    }
    if (j) {
        cJSON *cols = cJSON_GetObjectItem(j, "colors");
        if (cJSON_IsObject(cols)) {
            uint8_t rgb[57]; ok = true;
            for (int i = 0; i < 19 && ok; i++) {
                cJSON *c = cJSON_GetObjectItem(cols, SKIN_TOKENS[i]);
                if (!cJSON_IsString(c) || !parse_hex_color(c->valuestring, &rgb[i*3])) ok = false;
            }
            if (ok) {
                cJSON *jf = cJSON_GetObjectItem(j, "font"), *jb = cJSON_GetObjectItem(j, "brand"),
                      *jy = cJSON_GetObjectItem(j, "byline");
                skin_set_custom(rgb,
                                cJSON_IsNumber(jf) ? (jf->valueint ? 1 : 0) : 0,
                                cJSON_IsString(jb) ? jb->valuestring : "",
                                cJSON_IsString(jy) ? jy->valuestring : "");   /* NVS write on httpd task */
            }
        }
        cJSON_Delete(j);
    }
    free(body);
    if (!ok) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need colors{19 #rrggbb}"); return ESP_FAIL; }
    httpd_resp_sendstr(req, "ok");
    app_state_set_pref(PP_PREF_SKIN, skin_custom_index());   /* persist active idx + reboot to apply */
    return ESP_OK;
}

/* ---- Custom layout (issue #6): the chunk-grid spec — designed, saved, and previewed in the web UI.
 * The device no longer shows it as a navigable screen, so saving just persists the spec; the preview
 * is rendered off-screen on demand (POST /api/layout/preview). ---- */

static esp_err_t api_layout_get(httpd_req_t *req)
{
    const pp_layout_t *L = layout_get();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "cols", L->cols);
    cJSON *ts = cJSON_CreateArray();
    for (int i = 0; i < L->n; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", PP_TILE_KEYS[L->tiles[i].type]);
        cJSON_AddNumberToObject(t, "c", L->tiles[i].c); cJSON_AddNumberToObject(t, "r", L->tiles[i].r);
        cJSON_AddNumberToObject(t, "w", L->tiles[i].w); cJSON_AddNumberToObject(t, "h", L->tiles[i].h);
        cJSON_AddNumberToObject(t, "style", L->tiles[i].style);
        cJSON_AddNumberToObject(t, "group", L->tiles[i].group);
        cJSON_AddItemToArray(ts, t);
    }
    cJSON_AddItemToObject(o, "tiles", ts);
    cJSON *pal = cJSON_CreateArray();          /* the tile palette (for the designer) */
    for (int k = 1; k < LT_COUNT; k++) cJSON_AddItemToArray(pal, cJSON_CreateString(PP_TILE_KEYS[k]));
    cJSON_AddItemToObject(o, "types", pal);
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js ? js : "{}");
    free(js); cJSON_Delete(o);
    return ESP_OK;
}

static int tile_type_of(const char *key)
{
    if (key) for (int k = 1; k < LT_COUNT; k++) if (!strcmp(key, PP_TILE_KEYS[k])) return k;
    return 0;
}
static int jint(const cJSON *o, const char *k)   /* an int member, 0 if absent */
{
    const cJSON *m = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(m) ? m->valueint : 0;
}

static esp_err_t api_layout_post(httpd_req_t *req)   /* {cols, tiles:[{type,c,r,w,h}]} -> persist spec */
{
    char *body = recv_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    bool ok = false;
    if (j) {
        pp_layout_t L = {0};
        int cols = jint(j, "cols"); L.cols = (cols >= 1 && cols <= 16) ? cols : 8;
        cJSON *ts = cJSON_GetObjectItem(j, "tiles"), *t = NULL;
        int n = 0;
        if (cJSON_IsArray(ts)) cJSON_ArrayForEach(t, ts) {
            if (n >= PP_LAYOUT_MAX) break;
            const cJSON *ty = cJSON_GetObjectItem(t, "type");
            int type = tile_type_of(cJSON_IsString(ty) ? ty->valuestring : NULL);
            if (!type) continue;
            L.tiles[n].type = (uint8_t)type;
            L.tiles[n].c = (uint8_t)jint(t, "c"); L.tiles[n].r = (uint8_t)jint(t, "r");
            int w = jint(t, "w"), h = jint(t, "h");
            L.tiles[n].w = (uint8_t)(w < 1 ? 1 : w); L.tiles[n].h = (uint8_t)(h < 1 ? 1 : h);
            int sty = jint(t, "style"); L.tiles[n].style = (uint8_t)((sty >= 0 && sty < LS_COUNT) ? sty : 0);
            int grp = jint(t, "group"); L.tiles[n].group = (uint8_t)((grp >= 0 && grp < PP_LAYOUT_GROUPS) ? grp : 0);
            n++;
        }
        L.n = (uint8_t)n;
        if (n > 0) { layout_set(&L); ok = true; }   /* layout_set re-validates */
        cJSON_Delete(j);
    }
    free(body);
    if (!ok) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need tiles[]"); return ESP_FAIL; }
    httpd_resp_sendstr(req, "ok");   /* spec persisted; no reboot — the device no longer displays it */
    return ESP_OK;
}

/* POST /api/layout/preview — render the POSTed spec off the live screen (sample data) at the panel's
 * native resolution and return a 24-bit BMP. Does NOT call layout_set (the stored spec is untouched).
 * One render at a time: schedule the LVGL-task applier, block on a binary sem, then stream its snapshot. */
static esp_err_t api_layout_preview_post(httpd_req_t *req)
{
    char *body = recv_body(req);
    if (!body) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body"); return ESP_FAIL; }
    cJSON *j = cJSON_Parse(body);
    pp_layout_t L = {0}; bool ok = false;
    if (j) {
        int cols = jint(j, "cols"); L.cols = (cols >= 1 && cols <= 16) ? cols : 8;
        cJSON *ts = cJSON_GetObjectItem(j, "tiles"), *t = NULL; int n = 0;
        if (cJSON_IsArray(ts)) cJSON_ArrayForEach(t, ts) {
            if (n >= PP_LAYOUT_MAX) break;
            const cJSON *ty = cJSON_GetObjectItem(t, "type");
            int type = tile_type_of(cJSON_IsString(ty) ? ty->valuestring : NULL);
            if (!type) continue;
            L.tiles[n].type = (uint8_t)type;
            L.tiles[n].c = (uint8_t)jint(t, "c"); L.tiles[n].r = (uint8_t)jint(t, "r");
            int w = jint(t, "w"), h = jint(t, "h");
            L.tiles[n].w = (uint8_t)(w < 1 ? 1 : w); L.tiles[n].h = (uint8_t)(h < 1 ? 1 : h);
            int sty = jint(t, "style"); L.tiles[n].style = (uint8_t)((sty >= 0 && sty < LS_COUNT) ? sty : 0);
            int grp = jint(t, "group"); L.tiles[n].group = (uint8_t)((grp >= 0 && grp < PP_LAYOUT_GROUPS) ? grp : 0);
            n++;
        }
        L.n = (uint8_t)n; ok = (n > 0);
        cJSON_Delete(j);
    }
    free(body);
    if (!ok || !layout_valid(&L)) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad spec"); return ESP_FAIL; }

    if (xSemaphoreTake(s_preview_mtx, pdMS_TO_TICKS(8000)) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "render busy");
        return ESP_OK;
    }

    pp_preview_job_t *job = calloc(1, sizeof(*job));
    SemaphoreHandle_t sem = job ? xSemaphoreCreateBinary() : NULL;
    if (!job || !sem) {
        free(job); if (sem) vSemaphoreDelete(sem);
        xSemaphoreGive(s_preview_mtx);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    job->spec = L; job->sem = sem;   /* the applier fills job->w/h with the panel's native size */
    job->refs = 2;                   /* one ref for the handler, one for the applier */
    job->mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    if (pt_display_schedule_ui(ui_layout_preview_render, job) != LV_RESULT_OK) {
        pp_preview_job_release(job); pp_preview_job_release(job);   /* applier won't run: drop both refs */
        xSemaphoreGive(s_preview_mtx);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schedule");
    }

    esp_err_t ret;
    if (xSemaphoreTake(sem, pdMS_TO_TICKS(10000)) == pdTRUE) {
        if (job->ok && job->rgb) ret = bmp_send(req, (const uint16_t *)job->rgb, job->w, job->h, job->w);
        else                     ret = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "render");
    } else {
        /* The LVGL task is wedged (should never happen for a bounded render). Our ref is dropped here;
         * the still-pending applier holds the other and frees everything when it finally runs. */
        ESP_LOGE(TAG, "layout preview render timed out");
        ret = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "render timeout");
    }
    pp_preview_job_release(job);   /* drop the handler's ref (frees iff the applier already dropped its) */
    xSemaphoreGive(s_preview_mtx);
    return ret;
}

void web_start(void)
{
    s_upd_mtx = xSemaphoreCreateMutex();
    s_preview_mtx = xSemaphoreCreateMutex();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard; cfg.max_uri_handlers = 58; cfg.stack_size = 20480;
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) return;
    httpd_uri_t rs[] = {
        { "/", HTTP_GET, root_get }, { "/api/status", HTTP_GET, status_get },
        { "/api/printers", HTTP_GET, printers_get }, { "/api/printers", HTTP_POST, printers_post },
        { "/api/printers/update", HTTP_POST, printers_update_post }, { "/api/printers/remove", HTTP_POST, printers_remove_post },
        { "/api/printers/active", HTTP_POST, printers_active_post },
        { "/api/config/export", HTTP_GET, config_export_get }, { "/api/config/import", HTTP_POST, config_import_post },
        { "/api/connect/info", HTTP_GET, connect_info_get }, { "/api/connect/login", HTTP_POST, connect_login_post },
        { "/api/connect/totp", HTTP_POST, connect_totp_post }, { "/api/connect/fleet", HTTP_GET, connect_fleet_get },
        { "/api/connect/teams", HTTP_GET, connect_teams_get }, { "/api/connect/team_printers", HTTP_GET, connect_team_printers_get },
        { "/api/connect/logout", HTTP_POST, connect_logout_post },
        { "/api/connect/farmprobe", HTTP_GET, connect_farmprobe_get },
        { "/api/connect/ctrlprobe", HTTP_GET, connect_ctrlprobe_get },
        { "/api/connect/control", HTTP_POST, connect_control_post },
        { "/api/connect/snapshot", HTTP_GET, connect_snapshot_get },
        { "/api/test/webcam", HTTP_GET, test_webcam_get },
        { "/api/test/farm", HTTP_GET, test_farm_get },
        { "/api/test/gql", HTTP_POST, test_gql_post },
        { "/api/test/printer", HTTP_GET, connect_printer_raw_get },
        { "/api/test/orient", HTTP_GET, test_orient_get },
        { "/api/connect/farm", HTTP_GET, connect_farm_get },
        { "/api/connect/setorg", HTTP_POST, connect_setorg_post },
        { "/api/connect/orders", HTTP_GET, connect_orders_get },
        { "/api/connect/default_team", HTTP_GET, connect_default_team_get }, { "/api/connect/default_team", HTTP_POST, connect_default_team_post },
        { "/api/wifi", HTTP_POST, wifi_post }, { "/update", HTTP_POST, ota_post },
        { "/api/update/check", HTTP_GET, update_check_get }, { "/api/update/apply", HTTP_POST, update_apply_post },
        { "/api/update/progress", HTTP_GET, update_progress_get }, { "/api/info", HTTP_GET, info_get },
        { "/api/fleet", HTTP_GET, fleet_get }, { "/api/screen.bmp", HTTP_GET, screen_get },
        { "/api/ui", HTTP_GET, ui_get }, { "/api/ui/nav", HTTP_GET, ui_nav_get },
        { "/api/security", HTTP_POST, security_post },
        { "/api/reboot", HTTP_POST, reboot_post },
        { "/api/lang", HTTP_POST, lang_post },
        { "/api/bambu/info", HTTP_GET, bambu_info_get },
        { "/api/bambu/login", HTTP_POST, bambu_login_post },
        { "/api/bambu/code", HTTP_POST, bambu_code_post },
        { "/api/bambu/token", HTTP_POST, bambu_token_post },
        { "/api/bambu/pull", HTTP_POST, bambu_pull_post },
        { "/api/bambu/logout", HTTP_POST, bambu_logout_post },
        { "/api/log", HTTP_GET, log_get },
        { "/api/skin", HTTP_GET, skin_get }, { "/api/skin", HTTP_POST, skin_post },
        { "/api/layout", HTTP_GET, api_layout_get }, { "/api/layout", HTTP_POST, api_layout_post },
        { "/api/layout/preview", HTTP_POST, api_layout_preview_post },
    };
    /* Register each route through auth_wrap, stashing the real handler in user_ctx. When a web
     * password is set, auth_wrap gates every route; otherwise it's a transparent pass-through. */
    for (size_t i=0; i<sizeof(rs)/sizeof(rs[0]); i++) {
        httpd_uri_t u = rs[i];
        u.user_ctx = (void *)u.handler;
        u.handler  = auth_wrap;
        httpd_register_uri_handler(srv, &u);
    }
}
