#include "RuntimeWebUI.h"

#include <ArduinoJson.h>
#include <WiFi.h>

bool RuntimeWebUI::begin(StatusProvider statusProvider, ClearHandler clearHandler) {
    statusProvider_ = statusProvider;
    clearHandler_ = clearHandler;

    if (statusProvider_ == nullptr || clearHandler_ == nullptr) {
        Serial.println("RuntimeWebUI: missing callback.");
        return false;
    }

    server_.on("/", HTTP_GET, [this]() {
        handleRoot();
    });

    server_.on("/api/status", HTTP_GET, [this]() {
        handleStatus();
    });

    server_.on("/api/clear", HTTP_POST, [this]() {
        handleClear();
    });

    server_.onNotFound([this]() {
        server_.send(404, "text/plain", "Not found");
    });

    server_.begin();
    active_ = true;

    Serial.print("RuntimeWebUI: http://");
    Serial.println(WiFi.localIP());
    return true;
}

void RuntimeWebUI::loop() {
    if (active_) {
        server_.handleClient();
    }
}

bool RuntimeWebUI::isActive() const {
    return active_;
}

String RuntimeWebUI::htmlEscape(const String& value) {
    String result;
    result.reserve(value.length() + 8);

    for (size_t i = 0; i < value.length(); ++i) {
        switch (value.charAt(i)) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case 39: result += "&#39;"; break;
            default: result += value.charAt(i); break;
        }
    }

    return result;
}

void RuntimeWebUI::handleRoot() {
    static const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SpoolSense Dryer</title>
<style>
:root{color-scheme:dark;font-family:Arial,sans-serif}body{margin:0;background:#111;color:#eee}.wrap{max-width:820px;margin:auto;padding:20px}h1{margin:0 0 16px}.summary,.station{background:#1d1d1d;border:1px solid #333;border-radius:12px;padding:16px;margin-bottom:14px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}.station.selected{outline:2px solid #ddd}.title{display:flex;justify-content:space-between;align-items:center}.badge{font-size:12px;padding:4px 8px;border-radius:999px;background:#333}.row{display:flex;justify-content:space-between;gap:12px;padding:5px 0;border-bottom:1px solid #2b2b2b}.row:last-child{border-bottom:0}.label{color:#aaa}.value{text-align:right;overflow-wrap:anywhere}button{width:100%;margin-top:14px;padding:10px;border:0;border-radius:7px;font-weight:bold;font-size:15px;cursor:pointer}.muted{color:#aaa;font-size:13px}.ok{color:#8bd48b}.bad{color:#ff8d8d}.waiting{color:#ffd27a}.drying{color:#8bc9ff}.complete{color:#8bd48b;font-weight:bold}
</style>
</head>
<body><div class="wrap">
<h1>SpoolSense Dryer</h1>
<div class="summary">
  <div class="row"><span class="label">Chamber</span><span id="temp" class="value">--.- &deg;C</span></div>
  <div class="row"><span class="label">Wi-Fi</span><span id="wifi" class="value">...</span></div>
  <div class="row"><span class="label">Spoolman</span><span id="spoolman" class="value">...</span></div>
  <div class="row"><span class="label">NFC</span><span id="nfc" class="value">...</span></div>
</div>
<div class="grid">
  <div id="topCard" class="station">
    <div class="title"><h2>TOP</h2><span id="topSelected" class="badge">Selected</span></div>
    <div class="row"><span class="label">Spool</span><span id="topName" class="value">Empty</span></div>
    <div class="row"><span class="label">Vendor</span><span id="topVendor" class="value">&mdash;</span></div>
    <div class="row"><span class="label">Material</span><span id="topMaterial" class="value">&mdash;</span></div>
    <div class="row"><span class="label">Dry temp</span><span id="topDryTemp" class="value">&mdash;</span></div>
    <div class="row"><span class="label">Countdown starts</span><span id="topThreshold" class="value">&mdash;</span></div>
    <div class="row"><span class="label">Session</span><span id="topSession" class="value">Inactive</span></div>
    <div class="row"><span class="label">Remaining</span><span id="topRemaining" class="value">&mdash;</span></div>
    <div class="row"><span class="label">UID</span><span id="topUid" class="value">&mdash;</span></div>
    <div id="topError" class="muted"></div>
    <button onclick="clearStation('top')">Clear TOP</button>
  </div>
  <div id="bottomCard" class="station">
    <div class="title"><h2>BOTTOM</h2><span id="bottomSelected" class="badge">Selected</span></div>
    <div class="row"><span class="label">Spool</span><span id="bottomName" class="value">Empty</span></div>
    <div class="row"><span class="label">Vendor</span><span id="bottomVendor" class="value">&mdash;</span></div>
    <div class="row"><span class="label">Material</span><span id="bottomMaterial" class="value">&mdash;</span></div>
    <div class="row"><span class="label">Dry temp</span><span id="bottomDryTemp" class="value">&mdash;</span></div>
    <div class="row"><span class="label">Countdown starts</span><span id="bottomThreshold" class="value">&mdash;</span></div>
    <div class="row"><span class="label">Session</span><span id="bottomSession" class="value">Inactive</span></div>
    <div class="row"><span class="label">Remaining</span><span id="bottomRemaining" class="value">&mdash;</span></div>
    <div class="row"><span class="label">UID</span><span id="bottomUid" class="value">&mdash;</span></div>
    <div id="bottomError" class="muted"></div>
    <button onclick="clearStation('bottom')">Clear BOTTOM</button>
  </div>
</div>
<p class="muted">Countdown begins automatically when chamber temperature reaches 5 &deg;C below the spool's drying temperature.</p>
</div>
<script>
const el=id=>document.getElementById(id);
const DASH='\u2014';
const DEG_C=' \u00B0C';
function setHealth(id,ok,good,bad){const n=el(id);n.textContent=ok?good:bad;n.className='value '+(ok?'ok':'bad')}
function formatRemaining(seconds){
  if(seconds===null||seconds===undefined)return DASH;
  const h=Math.floor(seconds/3600),m=Math.floor((seconds%3600)/60),s=seconds%60;
  return h+':'+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0');
}
function updateStation(prefix,s,selected){
  el(prefix+'Card').classList.toggle('selected',selected);
  el(prefix+'Selected').style.visibility=selected?'visible':'hidden';
  el(prefix+'Name').textContent=s.occupied?(s.name||s.material||'Unknown spool'):'Empty';
  el(prefix+'Vendor').textContent=s.vendor||DASH;
  el(prefix+'Material').textContent=s.material||DASH;
  el(prefix+'DryTemp').textContent=s.dry_temp_c>0?s.dry_temp_c+DEG_C:DASH;
  el(prefix+'Threshold').textContent=s.start_threshold_c>0?s.start_threshold_c+DEG_C:DASH;
  const session=el(prefix+'Session');
  session.textContent=s.session_status||'Inactive';
  session.className='value '+(s.session_status==='Waiting for temp'?'waiting':s.session_status==='Drying'?'drying':s.session_status==='Complete'?'complete':'');
  el(prefix+'Remaining').textContent=(s.session_status&&s.session_status!=='Inactive')?formatRemaining(s.remaining_seconds):DASH;
  el(prefix+'Uid').textContent=s.uid||DASH;
  el(prefix+'Error').textContent=s.lookup_error||'';
}
async function refresh(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error();const s=await r.json();
    el('temp').textContent=s.temperature_valid?s.temperature_c.toFixed(2)+DEG_C:'--.-'+DEG_C;
    setHealth('wifi',s.wifi_connected,s.ip_address||'Connected','Offline');
    setHealth('spoolman',s.spoolman_configured,'Configured','Not configured');
    setHealth('nfc',s.nfc_available,'Ready','Unavailable');
    updateStation('top',s.top,s.selected==='top');
    updateStation('bottom',s.bottom,s.selected==='bottom');
  }catch(e){setHealth('wifi',false,'Connected','WebUI connection lost')}
}
async function clearStation(station){
  await fetch('/api/clear?station='+encodeURIComponent(station),{method:'POST'});refresh();
}
refresh();setInterval(refresh,1000);
</script>
</body>
</html>
)rawliteral";

    server_.send_P(200, "text/html; charset=utf-8", PAGE);
}

void RuntimeWebUI::handleStatus() {
    DryerRuntimeStatus status = statusProvider_();

    JsonDocument doc;
    doc["temperature_valid"] = status.temperatureValid;
    doc["temperature_c"] = status.chamberTempC;
    doc["wifi_connected"] = status.wifiConnected;
    doc["ip_address"] = status.ipAddress;
    doc["spoolman_configured"] = status.spoolmanConfigured;
    doc["nfc_available"] = status.nfcAvailable;
    doc["selected"] = status.selectedStation == DryerStation::TOP ? "top" : "bottom";

    auto addStation = [](JsonObject target, const DryerStationView& station) {
        target["occupied"] = station.occupied;
        target["uid"] = station.uid;
        target["lookup_error"] = station.lookupError;
        target["found"] = station.spool.found;
        target["spool_id"] = station.spool.spoolId;
        target["name"] = station.spool.name;
        target["vendor"] = station.spool.vendor;
        target["material"] = station.spool.material;
        target["dry_temp_c"] = station.spool.dryTempC;
        target["dry_time_hours"] = station.spool.dryTimeHours;
        target["session_status"] = station.sessionStatus;
        target["remaining_seconds"] = station.remainingSeconds;
        target["start_threshold_c"] = station.startThresholdC;
    };

    addStation(doc["top"].to<JsonObject>(), status.top);
    addStation(doc["bottom"].to<JsonObject>(), status.bottom);

    String payload;
    serializeJson(doc, payload);
    server_.send(200, "application/json; charset=utf-8", payload);
}

void RuntimeWebUI::handleClear() {
    if (!server_.hasArg("station")) {
        server_.send(400, "text/plain", "Missing station.");
        return;
    }

    String station = server_.arg("station");
    station.toLowerCase();

    if (station == "top") {
        clearHandler_(true);
    } else if (station == "bottom") {
        clearHandler_(false);
    } else {
        server_.send(400, "text/plain", "Station must be top or bottom.");
        return;
    }

    server_.send(204, "text/plain", "");
}
