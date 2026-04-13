(function(){
  var statusTimer=null;
  var authHeader='';

  function getAuthHeaders(){return {'Authorization':authHeader}}
  function authFetch(url,opts){
    opts=opts||{};
    opts.headers=Object.assign(getAuthHeaders(),opts.headers||{});
    return fetch(url,opts);
  }

  // Login
  function showLogin(){
    document.getElementById('loginOverlay').style.display='flex';
    document.getElementById('loginUser').focus();
  }
  function hideLogin(){document.getElementById('loginOverlay').style.display='none'}

  window.doLogin=function(){
    var u=document.getElementById('loginUser').value.trim();
    var p=document.getElementById('loginPass').value;
    if(!u||!p){document.getElementById('loginError').style.display='block';return}
    authHeader='Basic '+btoa(u+':'+p);
    fetch('/api/auth/check',{headers:getAuthHeaders()}).then(function(r){
      if(r.ok){
        hideLogin();
        document.getElementById('loginError').style.display='none';
        sessionStorage.setItem('auth',authHeader);
        startApp();
      }else{
        document.getElementById('loginError').style.display='block';
        authHeader='';
      }
    }).catch(function(){document.getElementById('loginError').style.display='block';authHeader=''});
  };

  document.getElementById('loginPass').addEventListener('keydown',function(e){if(e.key==='Enter')doLogin()});

  // Tab navigation
  document.querySelectorAll('.tab').forEach(function(tab){
    tab.addEventListener('click',function(){
      document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('active')});
      document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('active')});
      tab.classList.add('active');
      document.getElementById(tab.dataset.tab).classList.add('active');
      if(tab.dataset.tab==='clients') loadClients();
      if(tab.dataset.tab==='logs') loadLogs();
    });
  });

  document.getElementById('eapEnabled').addEventListener('change',function(){
    document.getElementById('eapFields').style.display=this.checked?'block':'none';
  });

  function toast(msg,type){
    var t=document.getElementById('toast');
    t.textContent=msg;
    t.className='toast '+(type||'')+' show';
    setTimeout(function(){t.classList.remove('show')},3000);
  }

  function rssiToPercent(rssi){
    if(rssi>=-50) return 100;
    if(rssi<=-100) return 0;
    return 2*(rssi+100);
  }

  function rssiColor(rssi){
    if(rssi>=-60) return 'var(--green)';
    if(rssi>=-75) return 'var(--orange)';
    return 'var(--red)';
  }

  function authName(a){
    var names=['Open','WEP','WPA','WPA2','WPA/2','WPA2-E','WPA3','WPA2/3','WAPI','OWE'];
    return names[a]||'?';
  }

  function formatUptime(s){
    var d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),m=Math.floor((s%3600)/60);
    if(d>0) return d+'d '+h+'h '+m+'m';
    if(h>0) return h+'h '+m+'m';
    return m+'m '+Math.floor(s%60)+'s';
  }

  function updateStatus(){
    authFetch('/api/status').then(function(r){
      if(r.status===401){showLogin();return}
      return r.json();
    }).then(function(d){
      if(!d) return;
      var dot=document.getElementById('statusDot');
      if(d.sta_connected){dot.classList.add('connected');dot.title='Connected'}
      else{dot.classList.remove('connected');dot.title='Disconnected'}
      document.getElementById('staSSID').textContent=d.sta_ssid||'Not set';
      document.getElementById('staIP').textContent=d.sta_connected?d.sta_ip:'Disconnected';
      document.getElementById('staRSSI').textContent=d.sta_connected?d.sta_rssi+' dBm':'-- dBm';
      var pct=d.sta_connected?rssiToPercent(d.sta_rssi):0;
      var fill=document.getElementById('rssiFill');
      fill.style.width=pct+'%';
      fill.style.background=d.sta_connected?rssiColor(d.sta_rssi):'var(--surface2)';
      document.getElementById('apClients').textContent=d.ap_clients;
      document.getElementById('apIP').textContent=d.ap_ip;
      document.getElementById('freeHeap').textContent=Math.round(d.free_heap/1024)+' KB';
      document.getElementById('uptime').textContent='Uptime: '+formatUptime(d.uptime);
    }).catch(function(){});
  }

  window.doScan=function(){
    var btn=document.getElementById('btnScan');
    btn.disabled=true;
    btn.innerHTML='<span class="spinner"></span> Scanning...';
    var list=document.getElementById('scanResults');
    list.style.display='none';
    list.innerHTML='';

    authFetch('/api/scan').then(function(r){return r.json()}).then(function(aps){
      btn.disabled=false;
      btn.innerHTML='<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg> Scan Networks';
      if(!aps.length){toast('No networks found','error');return}
      list.style.display='block';
      aps.forEach(function(ap){
        var div=document.createElement('div');
        div.className='scan-item';
        div.innerHTML='<div class="scan-item-info"><span class="scan-item-ssid">'+(ap.ssid||'[Hidden]')+'</span><span class="scan-item-meta">CH '+ap.channel+' · '+authName(ap.auth)+'</span></div><span class="scan-item-rssi" style="color:'+rssiColor(ap.rssi)+'">'+ap.rssi+' dBm</span>';
        div.addEventListener('click',function(){
          document.getElementById('staSSIDInput').value=ap.ssid;
          document.getElementById('staPassInput').focus();
          list.style.display='none';
          toast('Selected: '+ap.ssid,'success');
        });
        list.appendChild(div);
      });
    }).catch(function(){
      btn.disabled=false;
      btn.innerHTML='<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg> Scan Networks';
      toast('Scan failed','error');
    });
  };

  window.saveConfig=function(){
    var data={
      sta_ssid:document.getElementById('staSSIDInput').value,
      sta_pass:document.getElementById('staPassInput').value,
      ap_ssid:document.getElementById('apSSIDInput').value,
      ap_pass:document.getElementById('apPassInput').value,
      ap_channel:parseInt(document.getElementById('apChannel').value),
      ap_max_conn:parseInt(document.getElementById('apMaxConn').value),
      sta_eap_enabled:document.getElementById('eapEnabled').checked?1:0,
      sta_eap_identity:document.getElementById('eapIdentity').value,
      sta_eap_username:document.getElementById('eapUser').value,
      sta_eap_password:document.getElementById('eapPass').value
    };
    // Add port forwarding rules as flat keys
    var rules=document.querySelectorAll('.pf-rule');
    rules.forEach(function(rule,i){
      data['pf'+i+'_enabled']=rule.querySelector('.pf-enabled').checked?1:0;
      data['pf'+i+'_proto']=parseInt(rule.querySelector('.pf-proto').value);
      data['pf'+i+'_ext_port']=parseInt(rule.querySelector('.pf-ext-port').value)||0;
      data['pf'+i+'_int_ip']=rule.querySelector('.pf-int-ip').value;
      data['pf'+i+'_int_port']=parseInt(rule.querySelector('.pf-int-port').value)||0;
    });
    authFetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
    .then(function(r){return r.json()}).then(function(d){
      toast(d.message||'Config saved!','success');
    }).catch(function(){toast('Save failed','error')});
  };

  window.restartDevice=function(){
    if(!confirm('Restart the device?')) return;
    authFetch('/api/restart',{method:'POST'}).then(function(){
      toast('Restarting...','success');
    }).catch(function(){toast('Restart failed','error')});
  };

  window.loadClients=function(){
    authFetch('/api/clients').then(function(r){return r.json()}).then(function(clients){
      var list=document.getElementById('clientList');
      if(!clients.length){list.innerHTML='<div class="empty-state">No clients connected</div>';return}
      list.innerHTML='';
      clients.forEach(function(c){
        var div=document.createElement('div');
        div.className='client-item';
        div.innerHTML='<div class="client-dot"></div><div class="client-info"><span class="client-mac">'+c.mac+'</span><span class="client-ip">'+c.ip+'</span></div>';
        list.appendChild(div);
      });
    }).catch(function(){});
  };

  window.doPing=function(){
    var target=document.getElementById('pingTarget').value.trim();
    if(!target){toast('Enter a target','error');return}
    var btn=document.getElementById('btnPing');
    var res=document.getElementById('pingResult');
    btn.disabled=true;
    btn.textContent='Pinging...';
    res.className='ping-result ping-wait';
    res.textContent='Sending ping to '+target+'...';

    authFetch('/api/ping',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:target})})
    .then(function(r){return r.json()}).then(function(d){
      btn.disabled=false;
      btn.textContent='Ping';
      if(d.success){
        res.className='ping-result ping-ok';
        res.textContent='✓ Reply from '+d.ip+' time='+d.time_ms+'ms';
      }else{
        res.className='ping-result ping-fail';
        res.textContent='✗ '+(d.reason==='dns_failed'?'DNS lookup failed for '+d.target:'Request timed out');
      }
    }).catch(function(){
      btn.disabled=false;
      btn.textContent='Ping';
      res.className='ping-result ping-fail';
      res.textContent='✗ Request failed';
    });
  };

  window.togglePass=function(id){
    var inp=document.getElementById(id);
    inp.type=inp.type==='password'?'text':'password';
  };

  window.factoryReset=function(){
    if(!confirm('⚠️ Factory Reset?\n\nAll settings will be erased:\n- WiFi configuration\n- AP settings\n- Web credentials\n\nThe device will reboot with defaults.')) return;
    if(!confirm('Are you sure? This cannot be undone.')) return;
    authFetch('/api/factory-reset',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
      toast(d.message||'Factory reset done!','success');
      setTimeout(function(){sessionStorage.removeItem('auth');location.reload()},3000);
    }).catch(function(){toast('Factory reset failed','error')});
  };

  window.changeCredentials=function(){
    var u=document.getElementById('newUser').value.trim();
    var p=document.getElementById('newPass').value;
    if(!u){toast('Username required','error');return}
    if(p.length<4){toast('Password min 4 characters','error');return}
    authFetch('/api/auth/change',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({new_user:u,new_pass:p})})
    .then(function(r){return r.json()}).then(function(d){
      if(d.status==='ok'){
        authHeader='Basic '+btoa(u+':'+p);
        sessionStorage.setItem('auth',authHeader);
        toast('Credentials updated!','success');
        document.getElementById('newPass').value='';
      }else{
        toast(d.message||'Failed','error');
      }
    }).catch(function(){toast('Failed to update credentials','error')});
  };

  window.addPortRule=function(cfg){
    var list=document.getElementById('portFwdList');
    if(list.querySelectorAll('.pf-rule').length>=5){toast('Max 5 rules','error');return}
    cfg=cfg||{enabled:true,proto:0,ext_port:'',int_ip:'',int_port:''};
    var div=document.createElement('div');
    div.className='pf-rule';
    div.innerHTML='<div class="pf-row">'+
      '<input type="checkbox" class="pf-enabled"'+(cfg.enabled?' checked':'')+'>'+
      '<select class="pf-proto"><option value="0"'+(cfg.proto==0?' selected':'')+'>TCP</option><option value="1"'+(cfg.proto==1?' selected':'')+'>UDP</option></select>'+
      '<input type="number" class="pf-ext-port" placeholder="Ext" min="1" max="65535" value="'+(cfg.ext_port||'')+'">'+
      '<span class="pf-arrow">→</span>'+
      '<input type="text" class="pf-int-ip" placeholder="192.168.4.x" value="'+(cfg.int_ip||'')+'">'+
      '<input type="number" class="pf-int-port" placeholder="Int" min="1" max="65535" value="'+(cfg.int_port||'')+'">'+
      '<button class="pf-del" onclick="this.closest(\'.pf-rule\').remove()">✕</button>'+
      '</div>';
    list.appendChild(div);
  };

  var logTimer=null;
  window.loadLogs=function(){
    authFetch('/api/logs').then(function(r){return r.text()}).then(function(txt){
      var el=document.getElementById('logContent');
      el.textContent=txt||'(empty)';
      el.scrollTop=el.scrollHeight;
    }).catch(function(){});
  };
  function startLogRefresh(){
    if(logTimer) clearInterval(logTimer);
    logTimer=setInterval(function(){
      if(document.getElementById('logAutoRefresh').checked &&
         document.getElementById('logs').classList.contains('active')){
        loadLogs();
      }
    },3000);
  }

  window.doOTA=function(){
    var fileInput=document.getElementById('otaFile');
    if(!fileInput.files.length){toast('Select a firmware file','error');return}
    var file=fileInput.files[0];
    if(!confirm('Upload '+file.name+' ('+Math.round(file.size/1024)+' KB)? Device will reboot.')) return;

    var btn=document.getElementById('btnOta');
    var prog=document.getElementById('otaProgress');
    var fill=document.getElementById('otaFill');
    var status=document.getElementById('otaStatus');
    btn.disabled=true;
    prog.style.display='block';
    fill.style.width='0%';
    status.textContent='Uploading...';

    var xhr=new XMLHttpRequest();
    xhr.open('POST','/api/ota',true);
    xhr.setRequestHeader('Authorization',authHeader);
    xhr.setRequestHeader('Content-Type','application/octet-stream');

    xhr.upload.onprogress=function(e){
      if(e.lengthComputable){
        var pct=Math.round(e.loaded/e.total*100);
        fill.style.width=pct+'%';
        status.textContent='Uploading... '+pct+'%';
      }
    };

    xhr.onload=function(){
      if(xhr.status===200){
        fill.style.width='100%';
        fill.style.background='var(--green)';
        status.textContent='OTA successful! Rebooting...';
        toast('Firmware updated, rebooting...','success');
      }else{
        fill.style.background='var(--red)';
        status.textContent='OTA failed: '+xhr.responseText;
        toast('OTA failed','error');
        btn.disabled=false;
      }
    };

    xhr.onerror=function(){
      fill.style.background='var(--red)';
      status.textContent='Upload failed';
      toast('OTA upload failed','error');
      btn.disabled=false;
    };

    xhr.send(file);
  };

  function startApp(){
    authFetch('/api/config').then(function(r){return r.json()}).then(function(cfg){
      document.getElementById('staSSIDInput').value=cfg.sta_ssid||'';
      document.getElementById('apSSIDInput').value=cfg.ap_ssid||'';
      document.getElementById('apChannel').value=cfg.ap_channel||0;
      document.getElementById('apMaxConn').value=cfg.ap_max_conn||4;
      // EAP
      var eapCb=document.getElementById('eapEnabled');
      eapCb.checked=cfg.sta_eap_enabled||false;
      document.getElementById('eapFields').style.display=eapCb.checked?'block':'none';
      document.getElementById('eapIdentity').value=cfg.sta_eap_identity||'';
      document.getElementById('eapUser').value=cfg.sta_eap_username||'';
      // Port forwarding
      document.getElementById('portFwdList').innerHTML='';
      if(cfg.port_fwd){
        cfg.port_fwd.forEach(function(r){
          if(r.ext_port>0||r.enabled) addPortRule(r);
        });
      }
    }).catch(function(){});
    updateStatus();
    if(statusTimer) clearInterval(statusTimer);
    statusTimer=setInterval(updateStatus,3000);
    startLogRefresh();
  }

  // Check saved session or show login
  var saved=sessionStorage.getItem('auth');
  if(saved){
    authHeader=saved;
    fetch('/api/auth/check',{headers:{'Authorization':authHeader}}).then(function(r){
      if(r.ok){startApp()}
      else{sessionStorage.removeItem('auth');authHeader='';showLogin()}
    }).catch(function(){showLogin()});
  }else{
    showLogin();
  }
})();
