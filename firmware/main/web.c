/* Prusa-Touch — web interface: settings, live status, and firmware OTA. */
#include "web.h"

#include <string.h>
#include <stdlib.h>
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
#include "prusa_connect.h"

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
"input,button{font-size:16px;padding:10px;border-radius:8px;border:1px solid #3a3a3a;background:#2a2a2a;color:#f2f2f2;margin:4px 0;width:100%}"
"button{background:var(--o);color:#fff;border:0;font-weight:700;cursor:pointer}"
".bar{height:10px;background:#4e4e4e;border-radius:5px;overflow:hidden;margin-top:8px}.bar>i{display:block;height:100%;background:var(--o)}"
".muted{color:#a7a7a7}</style></head><body>"
"<header>PRUSA CONNECT TOUCH</header>"
"<nav><a class=on onclick=\"t(0)\">Status</a><a onclick=\"t(1)\">Printers</a>"
"<a onclick=\"t(2)\">Wi-Fi</a><a onclick=\"t(5)\">Account</a><a onclick=\"t(3)\">Firmware</a><a onclick=\"t(4)\">Screen</a></nav>"
"<div class=tab id=t0><div id=stlist></div><div class=card id=dev></div></div>"
"<div class=tab id=t1><div class=card><b id=pftitle>Add printer</b>"
"<input id=pn placeholder=Name><input id=ph placeholder='IP / host'>"
"<input id=pk placeholder='API key (blank = keep when editing)'>"
"<button onclick=savp()>Save</button> <button onclick=newp()>New</button></div>"
"<div id=plist></div>"
"<div class=card><b>Backup & Restore</b>"
"<p class=muted>Export your fleet config to a file, or import a saved config (replaces current fleet).</p>"
"<button onclick=expc()>Export Config</button>"
"<input type=file id=icf accept=.json style=margin-top:12px><button onclick=impc()>Import Config</button></div></div>"
"<div class=tab id=t2><div class=card><b>Wi-Fi</b>"
"<input id=ws placeholder=SSID><input id=wp type=password placeholder=Password>"
"<button onclick=savew()>Save &amp; connect</button></div></div>"
"<div class=tab id=t5><div class=card><b>Prusa Connect</b>"
"<div id=acstat class=muted>Not linked.</div>"
"<div id=loginform><input id=ae placeholder=Email><input id=ap type=password placeholder=Password>"
"<button onclick=connl()>Link Account</button></div>"
"<div id=totpform style=display:none><p class=muted>2FA required. Enter your TOTP code:</p>"
"<input id=tc placeholder=123456><button onclick=connt()>Verify</button></div>"
"<div id=acclist style=margin-top:16px></div></div></div>"
"<div class=tab id=t3>"
"<div class=card><b>Auto-update from GitHub</b>"
"<div id=gh class=muted>Tap Check.</div>"
"<button onclick=chk()>Check for updates</button>"
"<button id=ub style=display:none onclick=applyu()>Update now</button></div>"
"<div class=card><b>Manual firmware upload</b>"
"<p class=muted>Upload a Prusa-Touch .bin. The device reboots into it.</p>"
"<input type=file id=fw accept=.bin><button onclick=ota()>Flash</button>"
"<div id=otalog class=muted></div></div></div>"
"<div class=tab id=t4><div class=card><b>Live screen</b> "
"<button onclick=shot()>Refresh</button>"
"<div class=muted>What the touchscreen is showing right now.</div>"
"<img id=shot style='max-width:100%;border:1px solid #4e4e4e;margin-top:8px'></div></div>"
"<script>"
"function t(i){for(let n=0;n<6;n++){let el=document.getElementById('t'+n);if(el)el.className='tab'+(n==i?' on':'');"
"let nav=document.querySelectorAll('nav a')[n];if(nav)nav.className=(n==i?'on':'')}if(i==1)lp();if(i==4)shot();if(i==5)la()}"
"function shot(){document.getElementById('shot').src='/api/screen.bmp?t='+Date.now()}"
"async function st(){let L=await fetch('/api/fleet').then(x=>x.json());"
"const sc=s=>{s=(s||'').toUpperCase();if(s=='PRINTING'||s=='ATTENTION')return'orange';if(s=='PAUSED')return'yellow';if(s=='FINISHED')return'green';if(s=='READY')return'olive';if(s=='ERROR'||s=='STOPPED')return'red';if(s=='BUSY'||s=='PREPARING')return'blue';return'gray'};"
"document.getElementById('stlist').innerHTML=L.map(r=>{const c=r.online?sc(r.state):'gray';return '<div class=\"card '+c+'\">'+"
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
"'</div></div>'}).join('')||'<div class=card style=padding:18px>No printers yet.</div>';"
"try{let d=await fetch('/api/info').then(x=>x.json());document.getElementById('dev').innerHTML="
"'<span class=muted>'+d.name+' '+d.fw+' &middot; heap '+Math.round(d.heap_free/1024)+'KB &middot; up '+d.uptime_s+'s</span>'}catch(e){}}"
"async function la(){let r=await fetch('/api/connect/info').then(x=>x.json());"
"acstat.textContent=r.auth?'Linked':'Not linked.';"
"loginform.style.display=r.auth?'none':'block';totpform.style.display='none';"
"if(r.auth){let L=await fetch('/api/connect/fleet').then(x=>x.json());"
"acclist.innerHTML='<p class=muted>Printers on your account (tap to add):</p>'+L.map(p=>'<div class=card><b>'+p.name+'</b> ('+p.model+') '+"
"'<button onclick=\"addc(\\''+p.uuid+'\\',\\''+p.name+'\\')\">Add to local list</button></div>').join('')}}"
"async function connl(){acstat.textContent='Logging in...';"
"let r=await fetch('/api/connect/login',{method:'POST',body:JSON.stringify({e:ae.value,p:ap.value})});"
"let j=await r.json();if(j.res=='totp'){loginform.style.display='none';totpform.style.display='block';acstat.textContent='2FA Required'}else if(j.res=='ok'){la()}else alert('Login failed')}"
"async function connt(){let r=await fetch('/api/connect/totp',{method:'POST',body:JSON.stringify({c:tc.value})});"
"if((await r.json()).res=='ok')la();else alert('Verification failed')}"
"async function addc(id,name){await fetch('/api/printers',{method:'POST',body:JSON.stringify({name:name,host:'cloud:'+id,key:'connect'})});lp();alert('Added!')}"
"let PL=[],EI=-1;"
"function newp(){EI=-1;pn.value=ph.value=pk.value='';pftitle.textContent='Add printer'}"
"async function lp(){PL=await fetch('/api/printers').then(x=>x.json());"
"document.getElementById('plist').innerHTML=PL.map(p=>'<div class=card>'+(p.active?'\\u2605 ':'')+'<b>'+p.name+'</b> <span class=muted>'+p.host+(p.haskey?'':' (no key)')+'</span> '"
"+'<button onclick=usep('+p.i+')>Use</button> <button onclick=editp('+p.i+')>Edit</button> <button onclick=delp('+p.i+')>Remove</button></div>').join('')}"
"function editp(i){let p=PL.find(x=>x.i==i);if(!p)return;EI=i;pn.value=p.name;ph.value=p.host;pk.value='';pftitle.textContent='Edit '+p.name+' (key blank = keep)'}"
"async function savp(){let m=EI<0?{name:pn.value,host:ph.value,key:pk.value}:{i:EI,name:pn.value,host:ph.value,key:pk.value};"
"let r=await fetch(EI<0?'/api/printers':'/api/printers/update',{method:'POST',body:JSON.stringify(m)});"
"if(r.status>=400)alert(await r.text());else{newp();lp()}}"
"async function delp(i){await fetch('/api/printers/remove',{method:'POST',body:JSON.stringify({i:i})});if(EI==i)newp();lp()}"
"async function usep(i){await fetch('/api/printers/active',{method:'POST',body:JSON.stringify({i:i})});lp()}"
"async function expc(){let r=await fetch('/api/config/export').then(x=>x.json());"
"let b=new Blob([JSON.stringify(r,null,2)],{type:'application/json'});"
"let a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='prusa-touch-config.json';a.click()}"
"async function impc(){let f=icf.files[0];if(!f)return;if(!confirm('Replace ALL printers with config from '+f.name+'?'))return;"
"let r=await fetch('/api/config/import',{method:'POST',body:f});"
"if(r.status>=400)alert(await r.text());else{lp();alert('Import success!')}}"
"async function savew(){await fetch('/api/wifi',{method:'POST',body:JSON.stringify({ssid:ws.value,pass:wp.value})});alert('Saved; connecting...')}"
"async function ota(){let f=document.getElementById('fw').files[0];if(!f)return;"
"document.getElementById('otalog').textContent='Uploading '+f.name+'...';"
"let r=await fetch('/update',{method:'POST',body:f});"
"document.getElementById('otalog').textContent=await r.text()}"
"let GU='';"
"async function chk(){document.getElementById('gh').textContent='Checking...';let n=0;"
"const poll=async()=>{let r=await fetch('/api/update/check').then(x=>x.json());"
"if(r.checking&&n++<6){setTimeout(poll,2000);return;}"
"document.getElementById('gh').textContent='Current '+r.current+' / latest '+(r.latest||'?')+(r.available?' \\u2014 update available!':' \\u2014 up to date');"
"GU=r.url;document.getElementById('ub').style.display=r.available?'inline-block':'none'};poll()}"
"async function applyu(){if(!GU)return;document.getElementById('gh').innerHTML='Updating... <div class=bar id=upb><i style=width:0%></i></div>';"
"await fetch('/api/update/apply',{method:'POST',body:JSON.stringify({url:GU})});"
"const p=async()=>{let r=await fetch('/api/update/progress').then(x=>x.json());"
"if(r.progress>=0){document.getElementById('upb').firstChild.style.width=r.progress+'%';setTimeout(p,1000)}};p()}"
"st();la();setInterval(st,3000);"
"</script></body></html>";

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
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
            if (cJSON_IsString(h)) strlcpy(p.host, h->valuestring, sizeof(p.host));
            strlcpy(p.name, cJSON_IsString(n) && n->valuestring[0] ? n->valuestring : p.host, sizeof(p.name));
            if (cJSON_IsString(k)) strlcpy(p.api_key, k->valuestring, sizeof(p.api_key));
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
            if (cJSON_IsString(n) && n->valuestring[0]) strlcpy(p.name, n->valuestring, sizeof(p.name));
            if (cJSON_IsString(h) && h->valuestring[0]) strlcpy(p.host, h->valuestring, sizeof(p.host));
            if (cJSON_IsString(k) && k->valuestring[0]) strlcpy(p.api_key, k->valuestring, sizeof(p.api_key));
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
    if (req->content_len <= 0 || (size_t)req->content_len > part->size) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "oversized"); return ESP_FAIL; }
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(part, req->content_len, &ota) != ESP_OK) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "begin fail"); return ESP_FAIL; }
    char buf[1024];
    size_t remaining = (size_t)req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
        if (r <= 0) { esp_ota_abort(ota); return ESP_FAIL; }
        esp_ota_write(ota, buf, r);
        remaining -= r;
    }
    esp_ota_end(ota);
    esp_ota_set_boot_partition(part);
    httpd_resp_sendstr(req, "OK - rebooting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static ota_check_t s_upd;
static bool s_upd_have;
static volatile bool s_upd_busy;
static SemaphoreHandle_t s_upd_mtx;

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
    cJSON_AddNumberToObject(o, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js);
    free(js); cJSON_Delete(o);
    return ESP_OK;
}

static esp_err_t fleet_get(httpd_req_t *req)
{
    pp_status_t *arr = heap_caps_malloc(PP_MAX_PRINTERS * sizeof(pp_status_t), MALLOC_CAP_SPIRAM);
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
    if (prusa_connect_get_fleet(arr, 64, &n) != ESP_OK) { if (arr) heap_caps_free(arr); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "fail"); }
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
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

static esp_err_t screen_get(httpd_req_t *req)
{
    esp_lcd_panel_handle_t panel = pt_get_panel(); void *fb = NULL;
    if (!panel || esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || !fb) return httpd_resp_send_err(req, HTTPD_500, "no fb");
    const int W = 800, H = 480; uint8_t hdr[54] = { 'B','M', 0 };
    uint32_t imgsize = W*H*3, v = 54+imgsize; memcpy(&hdr[2],&v,4); hdr[10]=54; v=40; memcpy(&hdr[14],&v,4);
    int32_t iw=W, ih=H; memcpy(&hdr[18],&iw,4); memcpy(&hdr[22],&ih,4); hdr[26]=1; hdr[28]=24;
    httpd_resp_set_type(req, "image/bmp"); httpd_resp_send_chunk(req, (const char*)hdr, 54);
    uint8_t *row = malloc(W*3); const uint16_t *src = (const uint16_t*)fb;
    for (int y=H-1; y>=0; y--) {
        for (int x=0; x<W; x++) {
            uint16_t c = src[y*W+x];
            row[x*3+0]=(c&0x1F)<<3; row[x*3+1]=((c>>5)&0x3F)<<2; row[x*3+2]=((c>>11)&0x1F)<<3;
        }
        httpd_resp_send_chunk(req, (const char*)row, W*3);
    }
    free(row); httpd_resp_send_chunk(req, NULL, 0); return ESP_OK;
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

void web_start(void)
{
    s_upd_mtx = xSemaphoreCreateMutex();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard; cfg.max_uri_handlers = 40; cfg.stack_size = 20480;
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
        { "/api/wifi", HTTP_POST, wifi_post }, { "/update", HTTP_POST, ota_post },
        { "/api/update/check", HTTP_GET, update_check_get }, { "/api/update/apply", HTTP_POST, update_apply_post },
        { "/api/update/progress", HTTP_GET, update_progress_get }, { "/api/info", HTTP_GET, info_get },
        { "/api/fleet", HTTP_GET, fleet_get }, { "/api/screen.bmp", HTTP_GET, screen_get },
        { "/api/ui", HTTP_GET, ui_get }, { "/api/ui/nav", HTTP_GET, ui_nav_get }
    };
    for (size_t i=0; i<sizeof(rs)/sizeof(rs[0]); i++) httpd_register_uri_handler(srv, &rs[i]);
}
