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

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/api/clear", HTTP_POST, [this]() { handleClear(); });
    server_.onNotFound([this]() { server_.send(404, "text/plain", "Not found"); });

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

void RuntimeWebUI::handleRoot() {
    static const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SpoolSense Dryer</title>
<style>
:root{color-scheme:dark;font-family:Arial,sans-serif}body{margin:0;background:#111;color:#eee}.wrap{max-width:980px;margin:auto;padding:20px}h1{margin:0 0 16px}.summary,.station,.zone{background:#1d1d1d;border:1px solid #333;border-radius:12px;padding:16px;margin-bottom:14px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:14px}.station.selected{outline:2px solid #ddd}.title{display:flex;justify-content:space-between;align-items:center}.badge{font-size:12px;padding:4px 8px;border-radius:999px;background:#333}.row{display:flex;justify-content:space-between;gap:12px;padding:5px 0;border-bottom:1px solid #2b2b2b}.row:last-child{border-bottom:0}.label{color:#aaa}.value{text-align:right;overflow-wrap:anywhere}button{width:100%;margin-top:14px;padding:10px;border:0;border-radius:7px;font-weight:bold;font-size:15px;cursor:pointer}.muted{color:#aaa;font-size:13px}.ok{color:#8bd48b}.bad{color:#ff8d8d}.waiting{color:#ffd27a}.drying{color:#8bc9ff}.complete{color:#8bd48b;font-weight:bold}.warning{color:#ffd27a;font-weight:bold}
</style>
</head>
<body><div class="wrap">
<h1>SpoolSense Dryer</h1>
<div class="summary">
  <div class="row"><span class="label">Wi-Fi</span><span id="wifi" class="value">...</span></div>
  <div class="row"><span class="label">Spoolman</span><span id="spoolman" class="value">...</span></div>
  <div class="row"><span class="label">NFC</span><span id="nfc" class="value">...</span></div>
  <div class="row"><span class="label">Hardware</span><span id="shape" class="value">...</span></div>
</div>
<div id="zones" class="grid"></div>
<div id="stations" class="grid"></div>
<p class="muted">Each thermal zone continuously recalculates its shared target when a spool is added or cleared. Countdown starts once chamber temperature reaches 5 &deg;C below that target; being above target does not pause the timer.</p>
</div>
<script>
const el=id=>document.getElementById(id);
const DASH='\u2014';
const DEG_C=' \u00B0C';
function setHealth(id,ok,good,bad){const n=el(id);n.textContent=ok?good:bad;n.className='value '+(ok?'ok':'bad')}
function formatRemaining(seconds){if(seconds===null||seconds===undefined)return DASH;const h=Math.floor(seconds/3600),m=Math.floor((seconds%3600)/60),s=seconds%60;return h+':'+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0')}
function formatDryTemp(s){const p=s.dry_temp_c||0,min=s.dry_temp_min_c||0,max=s.dry_temp_max_c||0;if(!p)return DASH;if(min>0&&max>0&&min!==max)return min+'-'+max+DEG_C+' (pref '+p+DEG_C+')';return p+DEG_C}
function row(label,value,id){const r=document.createElement('div');r.className='row';const l=document.createElement('span');l.className='label';l.textContent=label;const v=document.createElement('span');v.className='value';if(id)v.id=id;v.textContent=value;r.append(l,v);return r}
function renderZones(zones){const root=el('zones');root.replaceChildren();for(const z of zones){const card=document.createElement('div');card.className='zone';const h=document.createElement('h2');h.textContent=z.label;card.append(h);card.append(row('Chamber',z.temperature_valid?Number(z.temperature_c).toFixed(2)+DEG_C:'--.-'+DEG_C));const p=z.temperature_plan||{};const ps=p.status||'No drying profile';const pr=row('Drying plan',ps);pr.lastChild.className='value '+(p.automatic_usable?'ok':(p.spool_count>1?'warning':''));card.append(pr);card.append(row('Shared target',p.recommended_target_c>0?p.recommended_target_c+DEG_C+(p.automatic_usable?'':' (advisory)'):DASH));let range=DASH;if(p.common_min_c>0&&p.common_max_c>0){range=p.common_min_c<=p.common_max_c?p.common_min_c+'-'+p.common_max_c+DEG_C:'No overlap; gap '+p.compromise_gap_c+DEG_C}card.append(row('Shared range',range));root.append(card)}}
function renderStations(stations,selected){const root=el('stations');root.replaceChildren();for(const s of stations){const card=document.createElement('div');card.className='station'+(s.id===selected?' selected':'');const title=document.createElement('div');title.className='title';const h=document.createElement('h2');h.textContent=s.label;const badge=document.createElement('span');badge.className='badge';badge.textContent='Selected';badge.style.visibility=s.id===selected?'visible':'hidden';title.append(h,badge);card.append(title);card.append(row('Zone',''+(s.zone+1)));card.append(row('Spool',s.occupied?(s.name||s.material||'Unknown spool'):'Empty'));card.append(row('Vendor',s.vendor||DASH));card.append(row('Material',s.material||DASH));card.append(row('Dry temp',formatDryTemp(s)));card.append(row('Countdown starts',s.start_threshold_c>0?s.start_threshold_c+DEG_C:DASH));const sr=row('Session',s.session_status||'Inactive');sr.lastChild.className='value '+(s.session_status==='Waiting for temp'?'waiting':s.session_status==='Drying'?'drying':s.session_status==='Complete'?'complete':'');card.append(sr);card.append(row('Remaining',s.session_status&&s.session_status!=='Inactive'?formatRemaining(s.remaining_seconds):DASH));card.append(row('UID',s.uid||DASH));if(s.lookup_error){const e=document.createElement('div');e.className='muted';e.textContent=s.lookup_error;card.append(e)}const b=document.createElement('button');b.textContent='Clear '+s.label;b.onclick=()=>clearStation(s.id);card.append(b);root.append(card)}}
async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error();const s=await r.json();setHealth('wifi',s.wifi_connected,s.ip_address||'Connected','Offline');setHealth('spoolman',s.spoolman_configured,'Configured','Not configured');setHealth('nfc',s.nfc_available,'Ready','Unavailable');el('shape').textContent=s.station_count+' station'+(s.station_count===1?'':'s')+' / '+s.zone_count+' zone'+(s.zone_count===1?'':'s');renderZones(s.zones||[]);renderStations(s.stations||[],s.selected_station)}catch(e){setHealth('wifi',false,'Connected','WebUI connection lost')}}
async function clearStation(station){await fetch('/api/clear?station='+encodeURIComponent(station),{method:'POST'});refresh()}
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
    doc["station_count"] = DRYER_STATION_COUNT;
    doc["zone_count"] = DRYER_ZONE_COUNT;
    doc["selected_station"] = status.selectedStation;
    doc["wifi_connected"] = status.wifiConnected;
    doc["ip_address"] = status.ipAddress;
    doc["spoolman_configured"] = status.spoolmanConfigured;
    doc["nfc_available"] = status.nfcAvailable;

    JsonArray zones = doc["zones"].to<JsonArray>();
    for (DryerZoneId i = 0; i < DRYER_ZONE_COUNT; ++i) {
        const DryerZoneView& zone = status.zones[i];
        JsonObject target = zones.add<JsonObject>();
        target["id"] = zone.id;
        target["label"] = zone.label;
        target["temperature_valid"] = zone.temperatureValid;
        target["temperature_c"] = zone.chamberTempC;

        JsonObject plan = target["temperature_plan"].to<JsonObject>();
        plan["status"] = zone.temperaturePlan.status;
        plan["spool_count"] = zone.temperaturePlan.spoolCount;
        plan["common_min_c"] = zone.temperaturePlan.commonMinC;
        plan["common_max_c"] = zone.temperaturePlan.commonMaxC;
        plan["recommended_target_c"] = zone.temperaturePlan.recommendedTargetC;
        plan["compromise_gap_c"] = zone.temperaturePlan.compromiseGapC;
        plan["automatic_usable"] = zone.temperaturePlan.automaticPlanUsable;
    }

    JsonArray stations = doc["stations"].to<JsonArray>();
    for (DryerStationId i = 0; i < DRYER_STATION_COUNT; ++i) {
        const DryerStationView& station = status.stations[i];
        JsonObject target = stations.add<JsonObject>();
        target["id"] = station.id;
        target["zone"] = station.zone;
        target["label"] = station.label;
        target["occupied"] = station.occupied;
        target["uid"] = station.uid;
        target["lookup_error"] = station.lookupError;
        target["found"] = station.spool.found;
        target["spool_id"] = station.spool.spoolId;
        target["name"] = station.spool.name;
        target["vendor"] = station.spool.vendor;
        target["material"] = station.spool.material;
        target["dry_temp_c"] = station.spool.dryTempC;
        target["dry_temp_min_c"] = station.spool.dryTempMinC;
        target["dry_temp_max_c"] = station.spool.dryTempMaxC;
        target["dry_time_hours"] = station.spool.dryTimeHours;
        target["session_status"] = station.sessionStatus;
        target["remaining_seconds"] = station.remainingSeconds;
        target["start_threshold_c"] = station.startThresholdC;
    }

    String payload;
    serializeJson(doc, payload);
    server_.send(200, "application/json; charset=utf-8", payload);
}

void RuntimeWebUI::handleClear() {
    if (!server_.hasArg("station")) {
        server_.send(400, "text/plain", "Missing station.");
        return;
    }

    String value = server_.arg("station");
    int station = value.toInt();
    if (station < 0 || station >= DRYER_STATION_COUNT) {
        server_.send(400, "text/plain", "Invalid station.");
        return;
    }

    clearHandler_(static_cast<DryerStationId>(station));
    server_.send(204, "text/plain", "");
}
