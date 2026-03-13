(function(){
  var statusTimer=null;

  // Tab navigation
  document.querySelectorAll('.tab').forEach(function(tab){
    tab.addEventListener('click',function(){
      document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('active')});
      document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('active')});
      tab.classList.add('active');
      document.getElementById(tab.dataset.tab).classList.add('active');
      if(tab.dataset.tab==='clients') loadClients();
    });
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
    fetch('/api/status').then(function(r){return r.json()}).then(function(d){
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

    fetch('/api/scan').then(function(r){return r.json()}).then(function(aps){
      btn.disabled=false;
      btn.innerHTML='<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg> Scan Networks';
      if(!aps.length){toast('No networks found','error');return}
      list.style.display='block';
      aps.forEach(function(ap){
        var div=document.createElement('div');
        div.className='scan-item';
        var pct=rssiToPercent(ap.rssi);
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
      ap_max_conn:parseInt(document.getElementById('apMaxConn').value)
    };
    fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
    .then(function(r){return r.json()}).then(function(d){
      toast(d.message||'Config saved!','success');
    }).catch(function(){toast('Save failed','error')});
  };

  window.restartDevice=function(){
    if(!confirm('Restart the device?')) return;
    fetch('/api/restart',{method:'POST'}).then(function(){
      toast('Restarting...','success');
    }).catch(function(){toast('Restart failed','error')});
  };

  window.loadClients=function(){
    fetch('/api/clients').then(function(r){return r.json()}).then(function(clients){
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

    fetch('/api/ping',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:target})})
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

  // Load current config
  fetch('/api/config').then(function(r){return r.json()}).then(function(cfg){
    document.getElementById('staSSIDInput').value=cfg.sta_ssid||'';
    document.getElementById('apSSIDInput').value=cfg.ap_ssid||'';
    document.getElementById('apChannel').value=cfg.ap_channel||0;
    document.getElementById('apMaxConn').value=cfg.ap_max_conn||4;
  }).catch(function(){});

  // Start status polling
  updateStatus();
  statusTimer=setInterval(updateStatus,3000);
})();
