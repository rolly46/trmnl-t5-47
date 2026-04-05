#pragma once

#include <pgmspace.h>

const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TRMNL Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#fff;color:#1a1a1a;padding:20px;max-width:480px;margin:0 auto}
h1{font-size:24px;margin-bottom:8px;text-align:center}
p.sub{color:#666;font-size:14px;text-align:center;margin-bottom:32px}
label{display:block;font-size:14px;font-weight:600;margin-bottom:6px;margin-top:20px}
select,input[type=text],input[type=password]{width:100%;padding:14px;font-size:16px;border:2px solid #ddd;border-radius:10px;outline:none;transition:border-color .2s}
select:focus,input:focus{border-color:#1a1a1a}
.pw-wrap{position:relative}
.pw-wrap input{padding-right:60px}
.pw-toggle{position:absolute;right:14px;top:50%;transform:translateY(-50%);background:none;border:none;font-size:14px;color:#666;cursor:pointer}
.note{font-size:12px;color:#888;margin-top:6px}
button{width:100%;padding:16px;font-size:18px;font-weight:600;background:#1a1a1a;color:#fff;border:none;border-radius:10px;margin-top:32px;cursor:pointer}
button:active{background:#333}
#status{text-align:center;margin-top:20px;font-size:16px;display:none}
.manual{display:none;margin-top:8px}
.manual.show{display:block}
</style>
</head>
<body>
<h1>TRMNL Setup</h1>
<p class="sub">Configure your display</p>
<form id="f" action="/save" method="POST">
<label for="ssid">WiFi Network</label>
<select id="ssid" name="wifi_ssid" onchange="toggleManual()">
<option value="">Scanning...</option>
</select>
<div id="manual" class="manual">
<label for="mssid">Network Name</label>
<input type="text" id="mssid" name="manual_ssid" placeholder="Enter SSID">
</div>

<label for="pass">WiFi Password</label>
<div class="pw-wrap">
<input type="password" id="pass" name="wifi_pass" placeholder="Enter password">
<button type="button" class="pw-toggle" onclick="togglePw()">Show</button>
</div>

<label for="api">Server URL</label>
<input type="text" id="api" name="api_base" value="https://usetrmnl.com">
<p class="note">Change only if using a self-hosted BYOS server</p>

<button type="submit">Save &amp; Connect</button>
</form>
<div id="status"></div>

<script>
function togglePw(){
  var p=document.getElementById('pass');
  var b=p.nextElementSibling;
  if(p.type==='password'){p.type='text';b.textContent='Hide';}
  else{p.type='password';b.textContent='Show';}
}
function toggleManual(){
  var s=document.getElementById('ssid');
  var m=document.getElementById('manual');
  m.className=s.value==='__manual__'?'manual show':'manual';
}
function loadNetworks(){
  fetch('/scan').then(r=>r.json()).then(function(nets){
    var s=document.getElementById('ssid');
    s.innerHTML='';
    nets.forEach(function(n){
      var o=document.createElement('option');
      o.value=n.ssid;
      o.textContent=n.ssid+' ('+n.rssi+' dBm)';
      s.appendChild(o);
    });
    var o=document.createElement('option');
    o.value='__manual__';
    o.textContent='-- Enter manually --';
    s.appendChild(o);
  }).catch(function(){
    var s=document.getElementById('ssid');
    s.innerHTML='<option value="__manual__">-- Enter manually --</option>';
    document.getElementById('manual').className='manual show';
  });
}
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  var fd=new FormData(this);
  var ssid=fd.get('wifi_ssid');
  if(ssid==='__manual__') ssid=fd.get('manual_ssid');
  if(!ssid){alert('Please select or enter a WiFi network');return;}
  var body='wifi_ssid='+encodeURIComponent(ssid)+'&wifi_pass='+encodeURIComponent(fd.get('wifi_pass'))+'&api_base='+encodeURIComponent(fd.get('api_base'));
  var st=document.getElementById('status');
  st.style.display='block';
  st.textContent='Saving... Device will restart.';
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body}).catch(function(){});
});
loadNetworks();
</script>
</body>
</html>
)rawliteral";
