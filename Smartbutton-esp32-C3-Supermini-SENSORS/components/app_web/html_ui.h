const char index_html[] = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>SmartButton</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;background:#212529;color:#dee2e6;min-height:100vh;font-size:1rem;line-height:1.5}
.header{background:#2b3035;padding:14px 16px;text-align:center;border-bottom:1px solid #495057;position:sticky;top:0;z-index:50}
.header h1{font-size:1.25rem;color:#fff;margin:0;font-weight:600;letter-spacing:.5px}
.nav{position:fixed;bottom:0;left:0;right:0;background:#2b3035;border-top:1px solid #495057;display:flex;z-index:50;padding-bottom:env(safe-area-inset-bottom)}
.nav-item{flex:1;text-align:center;padding:10px 0 8px;cursor:pointer;color:#6c757d;font-size:.72em;transition:all .2s}
.nav-item .ico{font-size:1.4em;display:block;margin-bottom:3px}
.nav-item.act{color:#0d6efd;background:rgba(13,110,253,.08)}
.nav-item:active{background:rgba(13,110,253,.15)}
.sec{display:none;padding:16px;padding-bottom:90px;max-width:540px;margin:0 auto}
.sec.act{display:block}
.card{background:#2b3035;border:1px solid #495057;border-radius:.5rem;padding:20px;margin-bottom:16px}
.card h2{font-size:.95rem;font-weight:600;margin-bottom:14px;color:#fff;border-bottom:1px solid #495057;padding-bottom:10px}
label{display:block;font-size:.875rem;color:#adb5bd;margin-bottom:6px;font-weight:500}
input,select{width:100%;padding:10px 14px;border:1px solid #495057;border-radius:.375rem;background:#343a40;color:#dee2e6;font-size:.9rem;margin-bottom:12px;-webkit-appearance:none;transition:border-color .15s,box-shadow .15s}
input:focus,select:focus{outline:none;border-color:#86b7fe;box-shadow:0 0 0 .2rem rgba(13,110,253,.25)}
select{cursor:pointer;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3E%3Cpath fill='none' stroke='%23adb5bd' stroke-linecap='round' stroke-linejoin='round' stroke-width='2' d='m2 5 6 6 6-6'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center;background-size:12px;padding-right:36px}
.pw{position:relative}
.pw input{padding-right:44px}
.pw-tog{position:absolute;right:10px;top:7px;background:none;border:none;color:#6c757d;font-size:1.1em;cursor:pointer;padding:4px;width:auto;transition:color .2s}
.pw-tog:hover{color:#adb5bd}
.wifi-list{max-height:220px;overflow-y:auto;margin-bottom:12px;border-radius:.375rem}
.wifi-item{display:flex;justify-content:space-between;align-items:center;padding:10px 14px;cursor:pointer;margin-bottom:2px;background:#343a40;border:1px solid transparent;border-radius:.375rem;transition:all .15s}
.wifi-item:hover{background:#3d4349;border-color:#495057}
.wifi-item.sel{background:rgba(13,110,253,.15);border-color:#0d6efd}
.wifi-item .name{font-size:.9em;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#dee2e6}
.wifi-item .rssi{font-size:.75em;color:#6c757d;margin-left:8px;white-space:nowrap}
.wifi-item .lock{font-size:.75em;margin-left:4px}
button,.btn{width:100%;padding:10px 16px;border:none;border-radius:.375rem;font-size:.9rem;font-weight:600;cursor:pointer;color:#fff;margin-top:6px;transition:all .15s;letter-spacing:.3px}
button:active,.btn:active{transform:scale(.98)}
.btn-p{background:#343a40;color:#0d6efd;border:1px solid #495057}
.btn-p:hover{background:#3d4349}
.btn-ok{background:#0d6efd;color:#fff}
.btn-ok:active{background:#0b5ed7}
.btn-test{background:transparent;color:#ffc107;border:1px solid #ffc107;margin-top:8px}
.btn-test:active{background:rgba(255,193,7,.1)}
.btn-danger{background:#dc3545;color:#fff}
.btn-danger:active{background:#bb2d3b}
.msg{text-align:center;padding:10px 14px;border-radius:.375rem;margin-top:10px;font-size:.85rem;display:none;font-weight:500}
.msg.ok{display:block;background:rgba(25,135,84,.15);color:#75b798;border:1px solid rgba(25,135,84,.3)}
.msg.err{display:block;background:rgba(220,53,69,.15);color:#ea868f;border:1px solid rgba(220,53,69,.3)}
.pl-row{display:none}
.pl-row.show{display:block}
.tabs{display:flex;gap:6px;margin-bottom:14px}
.tab{flex:1;padding:9px;text-align:center;border-radius:.375rem;background:#343a40;color:#6c757d;cursor:pointer;font-size:.85em;border:1px solid #495057;font-weight:500;transition:all .15s}
.tab.act{background:rgba(13,110,253,.15);color:#6ea8fe;border-color:#0d6efd}
.r2{display:flex;gap:10px}
.r2>div{flex:1}
.hid{display:none}
.login-box{max-width:360px;margin:80px auto 0;padding:20px}
.login-box h1{margin-bottom:28px;text-align:center;color:#fff;font-size:1.5rem;font-weight:700}
input[type="file"]{padding:8px;cursor:pointer;font-size:.85rem}
.modal-bg{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.6);z-index:100;align-items:center;justify-content:center;backdrop-filter:blur(4px)}
.modal-bg.show{display:flex}
.modal{background:#2b3035;border:1px solid #495057;border-radius:.5rem;padding:24px;max-width:400px;width:90%;max-height:80vh;overflow-y:auto}
.modal h2{font-size:1rem;color:#fff;margin-bottom:16px;text-align:center;font-weight:600}
.irow{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid #495057;font-size:.875rem}
.irow .il{color:#6c757d}
.irow .iv{color:#dee2e6;font-family:"SFMono-Regular",Consolas,"Liberation Mono",Menlo,monospace;font-size:.85em}
.chk-row{display:flex;align-items:center;margin-top:6px;margin-bottom:14px;gap:10px}
.chk-row input[type="checkbox"]{width:20px;height:20px;margin:0;cursor:pointer;accent-color:#0d6efd;flex-shrink:0;-webkit-appearance:auto;appearance:auto}
.chk-row label{display:inline;margin:0;cursor:pointer;color:#adb5bd;font-size:.85em;line-height:1.4}
</style>
</head><body>

<div id="login" class="login-box">
<h1>🔐 SmartButton</h1>
<div class="card">
<label>Usuario</label>
<input id="luser" placeholder="admin" value="admin">
<label>Contraseña</label>
<div class="pw">
<input id="lpass" type="password" placeholder="Contraseña">
<button class="pw-tog" type="button" onclick="togVis('lpass')">🗝️</button>
</div>
<button class="btn btn-ok" onclick="doLogin()">Entrar</button>
<div id="lmsg" class="msg"></div>
</div>
</div>

<div id="app" class="hid">
<div class="header"><h1>⚡ SmartButton</h1></div>

<!-- WiFi -->
<div id="s-wifi" class="sec act">
<div class="card">
<h2>📶 WiFi</h2>
<button class="btn btn-p" onclick="scanWifi()">Buscar redes</button>
<div id="wlist" class="wifi-list"></div>
<label>SSID de tu Red</label>
<input id="ssid" placeholder="Nombre de la red">
<label>Contraseña</label>
<div class="pw">
<input id="wpass" type="password" placeholder="Contraseña WiFi">
<button class="pw-tog" type="button" onclick="togVis('wpass')">🗝️</button>
</div>
<button class="btn btn-ok" onclick="saveWifi()">Guardar WiFi</button>
<div id="wmsg" class="msg"></div>
</div>
</div>

<!-- Integraciones -->
<div id="s-int" class="sec">
<div class="card">
<h2>🦟 MQTT Broker</h2>
<div class="chk-row">
<input type="checkbox" id="men">
<label for="men">Habilitar MQTT</label>
</div>
<label>Servidor (Host o IP)</label>
<input id="mhost" placeholder="mqtt.ejemplo.com">
<div class="r2">
<div><label>Puerto</label>
<input id="mport" type="number" value="1883"></div>
<div><label>Client ID</label>
<input id="mclid" placeholder="Auto"></div>
</div>
<label>Usuario (Opcional)</label>
<input id="muser">
<label>Contraseña (Opcional)</label>
<div class="pw">
<input id="mpass" type="password">
<button class="pw-tog" type="button" onclick="togVis('mpass')">🗝️</button>
</div>
<button class="btn btn-ok" onclick="saveMqtt()">Guardar MQTT</button>
<div id="mmsg" class="msg"></div>
</div>
</div>

<!-- Entradas -->
<div id="s-btn" class="sec">
<div class="card">
<h2>🔘 Configuración Entradas</h2>
<div class="tabs">
<div class="tab act" onclick="swTab(1,this)">Entrada 1</div>
<div class="tab" onclick="swTab(2,this)">Entrada 2</div>
<div class="tab" onclick="swTab(3,this)">Entrada 3</div>
</div>

<div class="chk-row">
<input type="checkbox" id="benabled" checked>
<label for="benabled">Entrada habilitada</label>
</div>

<div class="r2">
<div><label>GPIO Pin</label>
<select id="bgpio">
<option value="-1">No asignado</option>
<option value="0">GPIO 0</option>
<option value="1">GPIO 1</option>
<option value="2">GPIO 2</option>
<option value="3">GPIO 3</option>
<option value="4">GPIO 4</option>
<option value="5">GPIO 5</option>
<option value="6">GPIO 6</option>
<option value="7">GPIO 7</option>
<option value="10">GPIO 10</option>
</select></div>
<div><label>Tipo de Entrada</label>
<select id="binmode" onchange="uiTog()">
<option value="0">Botón (Activo LOW)</option>
<option value="1">Sensor (Activo HIGH)</option>
</select></div>
</div>

<label>Estabilización (seg)</label>
<input id="bstab" type="number" min="0" max="120" value="0">

<div class="chk-row">
<input type="checkbox" id="bisreset">
<label for="bisreset">Usar como botón de Reset</label>
</div>

<hr style="border:0;border-top:1px solid #495057;margin:8px 0 14px">

<label>Tipo de Acción</label>
<select id="batype" onchange="uiTog()">
<option value="0">Petición HTTP</option>
<option value="1">Mensaje MQTT</option>
</select>

<label id="lbl-target">URL</label>
<input id="btarget" placeholder="http://..." maxlength="510">

<div id="div-method">
<label>Método HTTP</label>
<select id="bmethod" onchange="uiTog()">
<option value="0">GET</option>
<option value="1">POST</option>
</select>
</div>

<div class="r2">
<div><label>Timeout (seg)</label>
<input id="btimeout" type="number" min="1" max="30" value="5">
</div>
<div><label>Cooldown (seg)</label>
<input id="bcooldown" type="number" step="0.5" min="0.5" max="600" value="2">
</div></div>

<div id="div-nocache">
<div class="chk-row">
<input type="checkbox" id="bnocache">
<label for="bnocache">Evitar caché (HTTP 304)</label>
</div>
</div>
<div id="div-pl">
<label>Payload (Body / Msg)</label>
<textarea id="bpayload" placeholder="Datos opcionales" rows="3" style="width:100%;padding:10px 14px;border:1px solid #495057;border-radius:.375rem;background:#343a40;color:#dee2e6;font-size:.9rem;margin-bottom:12px;resize:vertical;font-family:inherit"></textarea>
</div>

<button class="btn btn-ok" onclick="saveBtn()">Guardar y Reiniciar</button>
<button class="btn btn-test" onclick="testBtn()">⚡ Probar Acción</button>
<div id="bmsg" class="msg"></div>
</div>
</div>

<!-- Sistema -->
<div id="s-sys" class="sec">
<div class="card">
<h2>🌐 Estado de Red</h2>
<button class="btn btn-p" onclick="showNet()">Ver Info de Red</button>
</div>
<div class="card">
<h2>📡 AP Local & Ahorro</h2>
<label>SSID Local</label>
<input id="ap_ssid" placeholder="SmartButton-XXXX">
<label>Contraseña AP Local</label>
<div class="pw">
<input id="ap_pass" type="password" placeholder="smartbutton">
<button class="pw-tog" type="button" onclick="togVis('ap_pass')">🗝️</button>
</div>
<div class="chk-row">
<input type="checkbox" id="pure_client">
<label for="pure_client">Modo Cliente Puro (Ocultar AP tras conectar)</label>
</div>
</div>
<div class="card">
<h2>🔧 Avanzado</h2>
<div class="r2">
<div><label>Reintentos WiFi</label>
<input id="sta_retries" type="number" min="1" max="20" value="5"></div>
<div><label>Canal AP</label>
<input id="ap_channel" type="number" min="1" max="13" value="1"></div>
</div>
<div class="r2">
<div><label>Anti-rebote (ms)</label>
<input id="debounce" type="number" min="50" max="500" value="200"></div>
<div><label>T. Reset (seg)</label>
<input id="reset_time" type="number" min="3" max="60" value="8"></div>
</div>
</div>
<div class="card">
<h2>🔑 Seguridad</h2>
<label>Usuario</label>
<input id="auser" placeholder="admin">
<label>Nueva Contraseña</label>
<div class="pw">
<input id="apass" type="password">
<button class="pw-tog" type="button" onclick="togVis('apass')">🗝️</button>
</div>
<button class="btn btn-danger" onclick="saveAdmin()">Guardar y Reiniciar</button>
<div id="amsg" class="msg"></div>
</div>
<div class="card">
<h2>📦 OTA & Reset</h2>
<input type="file" id="otafile" accept=".bin">
<button class="btn btn-ok" onclick="doOta()">Actualizar Firmware</button>
<div id="omsg" class="msg"></div>
<hr style="border:0;border-top:1px solid #495057;margin:16px 0">
<button class="btn btn-danger" onclick="doFactoryReset()">Factory Reset</button>
<div id="frmsg" class="msg"></div>
</div>
</div>

<nav class="nav">
<div class="nav-item act" onclick="go('wifi',this)"><span class="ico">📶</span>WiFi</div>
<div class="nav-item" onclick="go('int',this)"><span class="ico">🔌</span>Integr.</div>
<div class="nav-item" onclick="go('btn',this)"><span class="ico">🔘</span>Entradas</div>
<div class="nav-item" onclick="go('sys',this)"><span class="ico">⚙️</span>Sistema</div>
</nav>
</div>

<div id="netmodal" class="modal-bg" onclick="if(event.target===this)closeNet()">
<div class="modal">
<h2>📡 Info de Red</h2>
<div id="netbody"><div style="text-align:center;color:#6c757d;padding:20px">Cargando...</div></div>
<button class="btn btn-p" onclick="closeNet()">Cerrar</button>
</div>
</div>

<script>
var A='',cBtn=1,curP='';
function go(s,el){
document.querySelectorAll('.sec').forEach(function(x){x.classList.remove('act')});
document.querySelectorAll('.nav-item').forEach(function(x){x.classList.remove('act')});
document.getElementById('s-'+s).classList.add('act');el.classList.add('act');
}
function doLogin(){
var u=$('luser').value,p=$('lpass').value;
if(!u||!p){msg('lmsg',0,'Introduce credenciales');return;}
A='Basic '+btoa(u+':'+p);
fetch('/api/verify',{headers:{'Authorization':A}}).then(function(r){
if(r.ok){$('login').classList.add('hid');$('app').classList.remove('hid');$('auser').value=u;curP=p;loadBtn(1);loadSys();loadMqtt();}
else{A='';msg('lmsg',0,'Credenciales incorrectas');}
}).catch(function(){msg('lmsg',0,'Error de conexion')});
}
function af(u,o){o=o||{};o.headers=o.headers||{};o.headers['Authorization']=A;return fetch(u,o);}
function $(id){return document.getElementById(id);}
function swTab(n,el){cBtn=n;document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('act')});el.classList.add('act');loadBtn(n);}
function togVis(id){var p=$(id);p.type=p.type==='password'?'text':'password';}
function uiTog(){
var t=$('batype').value;
$('lbl-target').innerText=(t==='1')?'Topic MQTT':'URL';
$('btarget').placeholder=(t==='1')?'casa/boton1/estado':'http://...';
$('div-method').style.display=(t==='1')?'none':'block';
$('div-nocache').style.display=(t==='1')?'none':'block';
var post=($('bmethod').value==='1');
$('div-pl').style.display=(t==='1'||post)?'block':'none';
}
function msg(id,ok,t){var m=$(id);m.className='msg '+(ok?'ok':'err');m.textContent=t;setTimeout(function(){m.className='msg';},4000);}
function scanWifi(){
var l=$('wlist');l.innerHTML='<div style="text-align:center;padding:12px;color:#6c757d">Buscando...</div>';
af('/api/scan').then(function(r){return r.json()}).then(function(d){
if(!d.length){l.innerHTML='<div style="text-align:center;padding:12px;color:#6c757d">No se encontraron redes</div>';return;}
l.innerHTML='';d.forEach(function(ap){
var e=document.createElement('div');e.className='wifi-item';
var s=ap.rssi>-50?'▂▄▆█':ap.rssi>-70?'▂▄▆_':'▂▄__';
e.innerHTML='<span class="name">'+ap.ssid+'</span><span class="rssi">'+s+' '+ap.rssi+'dBm</span><span class="lock">'+(ap.auth>0?'🔒':'')+'</span>';
e.onclick=function(){document.querySelectorAll('.wifi-item').forEach(function(i){i.classList.remove('sel')});e.classList.add('sel');$('ssid').value=ap.ssid;$('wpass').focus();};
l.appendChild(e);});
}).catch(function(){l.innerHTML='';msg('wmsg',0,'Error al buscar');});
}
function saveWifi(){
var s=$('ssid').value,p=$('wpass').value;
if(!s){msg('wmsg',0,'Falta SSID');return;}
af('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s,pass:p})
}).then(function(r){return r.json()}).then(function(d){msg('wmsg',d.ok,d.ok?'Guardado. Reiniciando...':'Error');if(d.ok)setTimeout(function(){location.reload()},3000);
}).catch(function(){msg('wmsg',0,'Error de conexion')});
}
function saveMqtt(){
var h=$('mhost').value,p=$('mport').value,u=$('muser').value,pa=$('mpass').value,c=$('mclid').value,e=$('men').checked;
af('/api/mqtt',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({host:h,port:parseInt(p),user:u,pass:pa,clid:c,en:e})
}).then(function(r){return r.json()}).then(function(d){msg('mmsg',d.ok,d.ok?'Guardado':'Error');}).catch(function(){msg('mmsg',0,'Error');});
}
function loadMqtt(){
af('/api/mqtt').then(function(r){return r.json()}).then(function(d){
$('mhost').value=d.host||'';$('mport').value=d.port||1883;$('muser').value=d.user||'';$('mclid').value=d.clid||'';$('men').checked=d.en;
}).catch(function(){});
}
function gBD(){return{id:cBtn,gpio:parseInt($('bgpio').value),inmode:parseInt($('binmode').value),stab:parseInt($('bstab').value||'0')*1000,is_reset:$('bisreset').checked,enabled:$('benabled').checked,atype:parseInt($('batype').value),target:$('btarget').value,method:parseInt($('bmethod').value),payload:$('bpayload').value||'',timeout:parseInt($('btimeout').value||'5')*1000,cooldown:parseFloat($('bcooldown').value||'2')*1000,nocache:$('bnocache').checked};}
function saveBtn(){
var d=gBD();
af('/api/btn',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)
}).then(function(r){return r.json()}).then(function(d){
if(d.ok){msg('bmsg',1,'Guardado. Reiniciando...');setTimeout(function(){location.reload()},4000);}
else msg('bmsg',0,'Error');
}).catch(function(){msg('bmsg',0,'Error de conexion')});
}
function testBtn(){
var d=gBD();if(!d.target){msg('bmsg',0,'Falta destino');return;}
msg('bmsg',1,'Probando...');
af('/api/test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)
}).then(function(r){return r.json()}).then(function(d){
if(d.msg)msg('bmsg',d.ok,d.ok?'OK: '+d.msg:d.msg);
else if(d.status<0)msg('bmsg',0,'Error conexión (code: '+d.status+')');
else if(d.ok)msg('bmsg',1,'OK - Status: '+d.status);
else msg('bmsg',0,'Falló - Status: '+d.status);
}).catch(function(){msg('bmsg',0,'Error de conexion')});
}
function loadBtn(n){
af('/api/btn?id='+n).then(function(r){return r.json()}).then(function(d){
$('bgpio').value=d.gpio!==undefined?d.gpio:-1;
$('binmode').value=d.inmode||0;
$('bstab').value=Math.round((d.stab||0)/1000);
$('bisreset').checked=!!d.is_reset;
$('benabled').checked=d.enabled!==undefined?d.enabled:true;
$('batype').value=d.atype||0;$('btarget').value=d.target||'';$('bmethod').value=d.method||0;$('bpayload').value=d.payload||'';
$('btimeout').value=Math.round((d.timeout||5000)/1000);$('bcooldown').value=(d.cooldown||2000)/1000;$('bnocache').checked=!!d.nocache;uiTog();
}).catch(function(){});
}
function loadSys(){
af('/api/admin').then(function(r){return r.json()}).then(function(d){
if(d.user)$('auser').value=d.user;
if(d.ap_ssid !== undefined)$('ap_ssid').value=d.ap_ssid;
if(d.ap_pass !== undefined)$('ap_pass').value=d.ap_pass;
if(d.pure_client !== undefined)$('pure_client').checked=d.pure_client;
if(d.sta_max_retries !== undefined)$('sta_retries').value=d.sta_max_retries;
if(d.ap_channel !== undefined)$('ap_channel').value=d.ap_channel;
if(d.debounce !== undefined)$('debounce').value=d.debounce;
if(d.reset_time !== undefined)$('reset_time').value=d.reset_time;
}).catch(function(){});
}
function saveAdmin(){
var u=$('auser').value, p=$('apass').value;
var aps=$('ap_ssid').value, app=$('ap_pass').value, pc=$('pure_client').checked;
if(!u){msg('amsg',0,'Falta usuario');return;}
if(p && p.length<4){msg('amsg',0,'Clave muy corta');return;}
var body={user:u, ap_ssid:aps, ap_pass:app, pure_client:pc,
sta_max_retries:parseInt($('sta_retries').value), ap_channel:parseInt($('ap_channel').value),
debounce:parseInt($('debounce').value), reset_time:parseInt($('reset_time').value)};
if(p)body.pass=p;
af('/api/admin',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
.then(function(res){return res.json()}).then(function(d){
if(d.ok){
curP=p?p:curP; A='Basic '+btoa(u+':'+curP);
msg('amsg',1,'Guardado. Reiniciando...'); $('apass').value='';
setTimeout(function(){location.reload()},4000);
}else msg('amsg',0,'Error al guardar');
}).catch(function(){msg('amsg',0,'Error')});
}
function doFactoryReset(){
if(!confirm('¿Borrar todo?'))return;
msg('frmsg',1,'Reseteando...');
af('/api/factory_reset',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
if(d.ok)msg('frmsg',1,'Reiniciando...');else msg('frmsg',0,'Error');
}).catch(function(){msg('frmsg',0,'Error');});
}
function doOta(){
var f=$('otafile');if(!f.files||!f.files[0]){msg('omsg',0,'Elige archivo .bin');return;}
msg('omsg',1,'Subiendo firmware... NO APAGUES');
af('/api/ota',{method:'POST',body:f.files[0]}).then(function(r){return r.json()}).then(function(d){
if(d.ok){msg('omsg',1,'OK. Reiniciando...');setTimeout(function(){location.reload()},10000);}
else msg('omsg',0,'Error OTA');
}).catch(function(){msg('omsg',0,'Error conexión');});
}
function showNet(){
$('netmodal').classList.add('show');
$('netbody').innerHTML='<div style="text-align:center;color:#6c757d;padding:20px">Cargando...</div>';
af('/api/netinfo').then(function(r){return r.json()}).then(function(d){
var h='',rows=[['SSID',d.ssid||'-'],['RSSI',d.rssi||'-'],['IP',d.ip||'-'],['Mascara',d.mask||'-'],['GW',d.gw||'-'],['DNS',d.dns||'-'],['MAC',d.mac||'-']];
rows.forEach(function(r){h+='<div class="irow"><span class="il">'+r[0]+'</span><span class="iv">'+r[1]+'</span></div>';});
$('netbody').innerHTML=h;
}).catch(function(){$('netbody').innerHTML='<div style="text-align:center;color:#ea868f;padding:20px">Error</div>';});
}
function closeNet(){$('netmodal').classList.remove('show');}
$('lpass').addEventListener('keyup',function(e){if(e.key==='Enter')doLogin();});
</script>
</body></html>
)rawliteral";
