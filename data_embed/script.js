
// ---- IARU band plans (70 cm / 33 cm) used by the LoRa / FSK profile cards ----
// Amateur allocation limits (MHz) per IARU region. National rules always prevail.
const IARU_REGIONS = {
    1: { name: "Region 1 (Europe, Africa, Middle East)", bands: [[430.0, 440.0]] },
    2: { name: "Region 2 (Americas)",                    bands: [[420.0, 450.0], [902.0, 928.0]] },
    3: { name: "Region 3 (Asia-Pacific)",                bands: [[430.0, 440.0]] }
};
// Channel presets: [label, MHz]. Region 1 digital segments from the IARU R1 UHF band plan; Region 3 from the WIA 70 cm plan.
const IARU_CHANNELS = {
    1: [
        ["433.625  Digital communications", 433.625], ["433.650  Digital communications", 433.65],
        ["433.675  Digital communications", 433.675], ["433.700  Digital communications", 433.7],
        ["433.725  Digital communications", 433.725], ["433.750  Digital communications", 433.75],
        ["433.775  LoRa APRS / Digital communications", 433.775],
        ["434.450  Digital communications", 434.45], ["434.475  Digital communications", 434.475],
        ["434.500  Digital communications", 434.5], ["434.525  Digital communications", 434.525],
        ["434.550  Digital communications", 434.55], ["434.575  Digital communications", 434.575],
        ["434.855  LoRa APRS Poland", 434.855],
        ["438.025  Digital communications", 438.025], ["438.050  Digital communications", 438.05],
        ["438.075  Digital communications", 438.075], ["438.100  Digital communications", 438.1],
        ["438.125  Digital communications", 438.125], ["438.150  Digital communications", 438.15],
        ["438.175  Digital communications", 438.175],
        ["439.9125 LoRa APRS UK", 439.9125]
    ],
    2: [
        ["433.775  LoRa APRS (check local plan)", 433.775]
    ],
    3: [
        ["433.775  LoRa APRS (check local plan)", 433.775],
        ["439.100  APRS (Australia)", 439.1],
        ["439.125  Internet gateways (Australia)", 439.125],
        ["439.150  Internet gateways (Australia)", 439.15]
    ]
};
// APRS FSK 9600 (G3RUH, ~12 kHz): same digital channels plus the IARU R1 narrow-band APRS frequency 432.500.
const IARU_CHANNELS_FSK = {
    1: [["432.500  APRS (IARU R1, 12 kHz)", 432.5]].concat(IARU_CHANNELS[1].filter(c => c[1] < 439)),
    2: IARU_CHANNELS[2], 3: IARU_CHANNELS[3]
};
// CW / telegraphy per region, from the official plans: R1 UHF-Bandplan (432.000-432.100 MGM and telegraphy, centre 432.050),
// R2 Band plan (432.000-432.100 CW EME / weak signal, calling 432.100; 33 cm weak-signal calling 902.100 / 903.100),
// R3-004 (431.9-432.24 EME / weak signal; no CW or beacon sub-segments are defined).
const IARU_CHANNELS_CW = {
    1: [["432.025  CW (weak-signal / EME)", 432.025], ["432.050  Telegraphy centre of activity (IARU R1)", 432.05],
        ["432.075  CW / telegraphy", 432.075], ["432.100  CW / SSB calling", 432.1]],
    2: [["432.025  CW (EME / weak signal, IARU R2)", 432.025], ["432.050  CW weak signal (IARU R2)", 432.05],
        ["432.100  CW / SSB calling (IARU R2)", 432.1], ["902.100  33 cm CW / SSB calling (IARU R2)", 902.1],
        ["903.100  33 cm CW / SSB calling (IARU R2)", 903.1]],
    3: [["432.050  CW weak signal (IARU R3 431.9-432.24)", 432.05], ["432.100  CW / SSB weak signal (IARU R3)", 432.1]]
};
// CW Beacon profile = telegraphy channels + beacon segments. The plans fix no channels inside the beacon segments,
// so these points are suggestions; ask your national beacon coordinator (e.g. URE) before choosing one.
//   R1: 432.400-432.490 beacons exclusive.  R2: 432.300-432.400 CW beacons, 432.400-432.420 digital beacons, 903.000-903.100 (33 cm).  R3: none defined.
const IARU_CHANNELS_CWB = {
    1: [["432.410  Beacon segment (IARU R1, exclusive)", 432.41], ["432.430  Beacon segment (IARU R1, exclusive)", 432.43],
        ["432.450  Beacon segment (IARU R1, exclusive)", 432.45], ["432.470  Beacon segment (IARU R1, exclusive)", 432.47]].concat(IARU_CHANNELS_CW[1]),
    2: [["432.320  CW beacon segment (IARU R2 432.3-432.4)", 432.32], ["432.350  CW beacon segment (IARU R2 432.3-432.4)", 432.35],
        ["432.380  CW beacon segment (IARU R2 432.3-432.4)", 432.38], ["903.050  33 cm beacon segment (IARU R2 903.0-903.1)", 903.05]].concat(IARU_CHANNELS_CW[2]),
    3: IARU_CHANNELS_CW[3]
};
// APRS AFSK 1200 on 2 m (LilyGO T-TWR, SA868 radio), per country: sigidwiki.com/wiki/APRS, VK2KFJ (VK / ZL), Japanese APRS frequency table.
const IARU_VHF_BANDS = { 1: [144.0, 146.0], 2: [144.0, 148.0], 3: [144.0, 148.0] };
const IARU_CHANNELS_VHF = {
    1: [["144.800  APRS (Europe, Africa, Russia)", 144.8]],
    2: [["144.390  APRS (North America, Chile, Colombia)", 144.39], ["144.930  APRS (Argentina, Uruguay)", 144.93], ["145.570  APRS (Brazil)", 145.57]],
    3: [["144.390  APRS (Indonesia, Malaysia, Thailand)", 144.39], ["145.175  APRS (Australia)", 145.175], ["144.575  APRS (New Zealand)", 144.575],
        ["144.640  APRS (China)", 144.64], ["144.660  APRS 1200 baud (Japan)", 144.66]]
};
// 70 cm (T-TWR UHF): 432.500 IARU R1 70 cm plan ("APRS frequency"), 439.100 WIA 70 cm plan (Australia), 431.040 Japanese table. Region 2: no common channel.
const IARU_CHANNELS_UHF = {
    1: [["432.500  APRS (IARU Region 1)", 432.5]],
    2: [],
    3: [["439.100  APRS (Australia)", 439.1], ["431.040  APRS 1200 baud (Japan)", 431.04]]
};
let SA868_BAND = "";                                        // "VHF" / "UHF" from /api/info; until then, from the frequency
let sa868Refill = null;
function sa868IaruWire() {
    const g = id => document.getElementById(id);
    const reg = g("sa868.iaruRegion"), preset = g("sa868.preset"), rx = g("sa868.rxMHz"), tx = g("sa868.txMHz"), info = g("sa868.bandinfo");
    const key = v => String(Math.round(parseFloat(v) * 1e4));
    const uhf = () => SA868_BAND ? SA868_BAND === "UHF" : parseFloat(rx.value) >= 300;
    const table = () => uhf() ? IARU_CHANNELS_UHF : IARU_CHANNELS_VHF;
    const refresh = () => {
        const r = parseInt(reg.value, 10), f = parseFloat(rx.value);
        const bands = uhf() ? IARU_REGIONS[r].bands.filter(b => b[1] < 500) : [IARU_VHF_BANDS[r]];
        const ok = bands.some(b => f >= b[0] && f <= b[1]);
        info.className = ok ? "text-muted" : "text-danger fw-semibold";
        info.textContent = (uhf() ? "70 cm" : "2 m") + " amateur allocation, " + IARU_REGIONS[r].name + ": " + bands.map(b => b[0] + "\u2013" + b[1]).join(" / ") + " MHz"
            + (ok ? "" : " \u2014 the frequency is OUTSIDE it") + ". National rules prevail."
            + (table()[r].length ? "" : " No common APRS channel in this region: enter the local one.");
    };
    const fill = () => {
        const r = parseInt(reg.value, 10);
        preset.innerHTML = ['<option value="">Custom</option>'].concat(table()[r].map(c => '<option value="' + c[1] + '">' + c[0] + '</option>')).join("");
        const cur = [...preset.options].find(o => o.value && key(o.value) === key(rx.value));
        preset.value = cur ? cur.value : "";
    };
    preset.addEventListener("change", () => { if (preset.value) { rx.value = preset.value; tx.value = preset.value; } refresh(); });
    reg.addEventListener("change", () => {
        // a channel of the old region that does not exist in the new one becomes the new region's first channel (as the automatic region does)
        const t = table(), all = [].concat(...Object.values(t)).map(c => key(c[1])), next = t[parseInt(reg.value, 10)];
        if (next.length && all.includes(key(rx.value)) && !next.some(c => key(c[1]) === key(rx.value))) { rx.value = next[0][1].toFixed(4); tx.value = rx.value; }
        fill(); refresh();
    });
    rx.addEventListener("input", () => { fill(); refresh(); });
    sa868Refill = () => { fill(); refresh(); };
    fill(); refresh();
}
function iaruList(o, region) {
    const t = o && o.kind === "cwb" ? IARU_CHANNELS_CWB : o && o.kind === "cw" ? IARU_CHANNELS_CW : (o && o.kind === "fsk" ? IARU_CHANNELS_FSK : IARU_CHANNELS);
    return t[region] || [];
}
function iaruCheckFrequency(region, hz) {
    const mhz = hz / 1e6;
    const r = IARU_REGIONS[region] || IARU_REGIONS[1];
    return r.bands.some(b => mhz >= b[0] && mhz <= b[1]);
}
function iaruBandText(region) {
    const r = IARU_REGIONS[region] || IARU_REGIONS[1];
    return r.bands.map(b => b[0] + "–" + b[1] + " MHz").join(" / ");
}
// Reusable "IARU region + band plan channel + frequency + power" group (FSK profiles and the two CW profiles).
// o: { regionName, freqName, powerName, region, freq, power }  (ids = names)
function iaruProfileHtml(o) {
    return `
        <div class="form-floating col-6 col-md-4 px-1 mb-2">
            <select class="form-select form-select-sm" name="${o.regionName}" id="${o.regionName}">
                ${[1, 2, 3].map(r => `<option value="${r}" ${(o.region || 1) == r ? 'selected' : ''}>${IARU_REGIONS[r].name}</option>`).join('')}
            </select>
            <label for="${o.regionName}">IARU region</label>
        </div>
        <div class="form-floating col-12 col-md-4 px-1 mb-2">
            <select class="form-select form-select-sm" id="${o.regionName}.preset"></select>
            <label for="${o.regionName}.preset">IARU band plan channel</label>
        </div>
        <div class="col-12 px-1 mb-1"><small class="text-muted" id="${o.regionName}.bandinfo"></small></div>
        <div class="form-floating col-6 col-md-3 px-1 mb-2">
            <input type="number" class="form-control form-control-sm" name="${o.freqName}" id="${o.freqName}" min="137000000" max="1020000000" required value="${o.freq}">
            <label for="${o.freqName}">Frequency (Hz)</label>
        </div>
        <div class="form-floating col-4 col-md-2 px-1 mb-2">
            <input type="number" class="form-control form-control-sm" name="${o.powerName}" id="${o.powerName}" min="2" max="22" required value="${o.power}">
            <label for="${o.powerName}">Power (dBm)</label>
        </div>`;
}
function iaruProfileWire(root, o) {
    const $ = id => root.querySelector('[id="' + id + '"]');
    const regSel = $(o.regionName), preset = $(o.regionName + ".preset"), freq = $(o.freqName), info = $(o.regionName + ".bandinfo");
    const refresh = () => {
        const region = parseInt(regSel.value, 10);
        const ok = iaruCheckFrequency(region, parseFloat(freq.value));
        info.className = ok ? "text-muted" : "text-danger fw-semibold";
        info.textContent = "Amateur allocation, " + IARU_REGIONS[region].name + ": " + iaruBandText(region)
            + (ok ? "" : " \u2014 the frequency is OUTSIDE this region's amateur allocation") + ". National rules prevail.";
    };
    const fillPresets = () => {
        const region = parseInt(regSel.value, 10);
        preset.innerHTML = ['<option value="">Custom</option>'].concat(iaruList(o, region).map(c => '<option value="' + Math.round(c[1] * 1e6) + '">' + c[0] + '</option>')).join("");
        const cur = String(Math.round(parseFloat(freq.value)));
        if ([...preset.options].some(x => x.value === cur)) preset.value = cur;
    };
    preset.addEventListener("change", () => { if (preset.value) freq.value = preset.value; refresh(); });
    regSel.addEventListener("change", () => { fillPresets(); refresh(); });
    freq.addEventListener("input", () => {
        const cur = String(Math.round(parseFloat(freq.value)));
        preset.value = [...preset.options].some(x => x.value === cur) ? cur : "";
        refresh();
    });
    fillPresets();
    refresh();
}

// Custom scripts

let currentSettings = null;

function backupSettings() {
    const data =
        "data:text/json;charset=utf-8," +
        encodeURIComponent(JSON.stringify(currentSettings));
    const a = document.createElement("a");
    a.setAttribute("href", data);
    a.setAttribute("download", "TrackerConfigurationBackup.json");
    a.click();
}

document.getElementById("backup").onclick = backupSettings;

document.getElementById("restore").onclick = function (e) {
    e.preventDefault();

    document.querySelector("input[type=file]").click();
};

document.querySelector("input[type=file]").onchange = function () {
    const files = document.querySelector("input[type=file]").files;

    if (!files.length) return;

    const file = files.item(0);

    const reader = new FileReader();
    reader.readAsText(file);
    reader.onload = () => {
        const data = JSON.parse(reader.result);

        loadSettings(data);
    };
};

function fetchSettings() {
    fetch("/configuration.json")
        .then((response) => response.json())
        .then((settings) => {
            console.log(settings);
            loadSettings(settings);
        })
        .catch((err) => {
            console.error(err);

            alert(`Failed to load configuration`);
        });
}

// AIS nautical profile (4th beacon): vessel types. Must match src/ais_vessel.cpp
const AIS_VESSELS = [
    { id: 0,  label: "Sailing yacht (recreational)",   cls: "B-CS", ssid: 5, overlay: "/",  symbol: "Y", group: "Recreational (AIS Class B)" },
    { id: 1,  label: "Power boat (recreational)",      cls: "B-SO", ssid: 8, overlay: "/",  symbol: "s", group: "Recreational (AIS Class B)" },
    { id: 2,  label: "Cargo ship",                     cls: "A",    ssid: 8, overlay: "C",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 3,  label: "Tanker",                         cls: "A",    ssid: 8, overlay: "\\", symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 4,  label: "Passenger ship / ferry",         cls: "A",    ssid: 8, overlay: "X",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 5,  label: "Fishing vessel",                 cls: "A",    ssid: 8, overlay: "F",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 6,  label: "Tug",                            cls: "A",    ssid: 8, overlay: "T",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 7,  label: "Pilot vessel",                   cls: "A",    ssid: 8, overlay: "P",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 8,  label: "High-speed craft",               cls: "A",    ssid: 8, overlay: "H",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 9,  label: "Law enforcement",                cls: "A",    ssid: 8, overlay: "L",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 10, label: "Search and rescue",              cls: "A",    ssid: 8, overlay: "S",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 11, label: "Emergency / medical transport",  cls: "A",    ssid: 8, overlay: "E",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 12, label: "Military",                       cls: "A",    ssid: 8, overlay: "M",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 13, label: "Diving vessel",                  cls: "A",    ssid: 8, overlay: "D",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 14, label: "Sailing ship (large)",           cls: "A",    ssid: 8, overlay: "Y",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 15, label: "Wing-in-ground / hovercraft",    cls: "A",    ssid: 8, overlay: "W",  symbol: "s", group: "Commercial (AIS Class A)" },
    { id: 16, label: "Other ship",                     cls: "A",    ssid: 8, overlay: "\\", symbol: "s", group: "Commercial (AIS Class A)" }
];

function aisVesselInfoText(vesselId) {
    const v = AIS_VESSELS.find(x => x.id == vesselId) || AIS_VESSELS[0];
    const clsText = v.cls === "A" ? "Class A" : (v.cls === "B-SO" ? 'Class B "SO"' : 'Class B "CS"');
    const symbolText = v.overlay === "/" ? "/" + v.symbol : (v.overlay === "\\" ? "\\" + v.symbol : "\\" + v.symbol + " (overlay " + v.overlay + ")");
    return `AIS ${clsText} &middot; SSID -${v.ssid} &middot; APRS symbol ${symbolText} &middot; SmartBeacon AIS ${clsText} &middot; LoRa profile: AIS (SF9, CR 4/5)`;
}

function aisVesselChanged(index) {
    const select = document.getElementById(`beacons.${index}.aisVesselType`);
    const v = AIS_VESSELS.find(x => x.id == select.value) || AIS_VESSELS[0];
    document.getElementById(`beacons.${index}.ssidLabel`).textContent = "-" + v.ssid;
    document.getElementById(`beacons.${index}.aisInfo`).innerHTML = aisVesselInfoText(select.value);
    const len = document.getElementById(`beacons.${index}.aisLength`);
    if (len) {
        const b = v.cls !== "A";
        len.min = b ? 6 : 0;
        len.max = b ? 24 : 999;
        const n = Number(len.value) || 0;
        if (n > 0) len.value = Math.min(Math.max(n, len.min), len.max);
    }
}

function renderAisBeacon(beacons, index) {
    const baseCall = String(beacons.callsign || "").replace(/-.*$/, "");
    const v = AIS_VESSELS.find(x => x.id == beacons.aisVesselType) || AIS_VESSELS[0];
    let options = "";
    let group = "";
    AIS_VESSELS.forEach(item => {
        if (item.group !== group) {
            if (group !== "") options += "</optgroup>";
            options += `<optgroup label="${item.group}">`;
            group = item.group;
        }
        options += `<option value="${item.id}" ${item.id == v.id ? "selected" : ""}>${item.label}</option>`;
    });
    options += "</optgroup>";
    return `
            <div class="d-flex align-items-start pt-3" style="flex: 0 0 50px; width: 50px;">
                <strong>${index + 1})</strong>
            </div>
            <div class="col">
                <div class="row g-2 mb-2">
                    <div class="col-12"><span class="badge text-bg-primary">AIS nautical profile</span>
                        <small class="text-muted ms-1">Callsign, SSID, symbol, SmartBeacon and LoRa profile follow the vessel type.</small></div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="col-12 col-md-6">
                        <div class="input-group input-group-sm">
                            <div class="form-floating">
                                <input type="text" class="form-control form-control-sm"
                                    name="beacons.${index}.callsign" id="beacons.${index}.callsign"
                                    required maxlength="6" pattern="[A-Za-z0-9]{3,6}"
                                    value="${baseCall}"
                                    oninput="this.value = this.value.toUpperCase().replace(/[^A-Z0-9]/g, '');">
                                <label for="beacons.${index}.callsign">Callsign (amateur radio)</label>
                            </div>
                            <span class="input-group-text" id="beacons.${index}.ssidLabel">-${v.ssid}</span>
                        </div>
                    </div>
                    <div class="col-12 col-md-6">
                        <label for="beacons.${index}.aisVesselType" class="form-label"><small>Vessel type</small></label>
                        <select name="beacons.${index}.aisVesselType" id="beacons.${index}.aisVesselType" class="form-control" onchange="aisVesselChanged(${index})">${options}</select>
                    </div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="form-floating col-12 col-md-6">
                        <input type="text" class="form-control form-control-sm" maxlength="20"
                            name="beacons.${index}.aisName" id="beacons.${index}.aisName"
                            value="${beacons.aisName || ""}"
                            oninput="this.value = this.value.toUpperCase().replace(/[^A-Z0-9 .\\/&'()+#-]/g, '');">
                        <label for="beacons.${index}.aisName">Vessel name (max. 20)</label>
                    </div>
                    <div class="form-floating col-12 col-md-6">
                        <input type="text" inputmode="numeric" class="form-control form-control-sm" maxlength="9" pattern="(|[2-7][0-9]{8})"
                            name="beacons.${index}.aisMmsi" id="beacons.${index}.aisMmsi"
                            value="${beacons.aisMmsi ? beacons.aisMmsi : ""}"
                            oninput="this.value = this.value.replace(/[^0-9]/g, '');">
                        <label for="beacons.${index}.aisMmsi">MMSI (9 digits; empty = not sent)</label>
                    </div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="form-floating col-4">
                        <input type="number" class="form-control form-control-sm" min="${v.cls !== "A" ? 6 : 0}" max="${v.cls !== "A" ? 24 : 999}" step="1"
                            name="beacons.${index}.aisLength" id="beacons.${index}.aisLength" value="${beacons.aisLength || 0}">
                        <label for="beacons.${index}.aisLength">Length (m) - Class B: 6-24</label>
                    </div>
                    <div class="form-floating col-4">
                        <input type="number" class="form-control form-control-sm" min="0" max="126" step="1"
                            name="beacons.${index}.aisBeam" id="beacons.${index}.aisBeam" value="${beacons.aisBeam || 0}">
                        <label for="beacons.${index}.aisBeam">Beam (m)</label>
                    </div>
                    <div class="form-floating col-4">
                        <input type="number" class="form-control form-control-sm" min="0" max="25.5" step="0.1"
                            name="beacons.${index}.aisDraft" id="beacons.${index}.aisDraft" value="${Number(beacons.aisDraft || 0).toFixed(1)}">
                        <label for="beacons.${index}.aisDraft">Draft (m)</label>
                    </div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="col-12"><small class="text-muted" id="beacons.${index}.aisInfo">${aisVesselInfoText(v.id)}</small></div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="form-floating col-12">
                        <input type="text" class="form-control form-control-sm"
                            name="beacons.${index}.comment" id="beacons.${index}.comment" value="${beacons.comment}">
                        <label for="beacons.${index}.comment">Comment</label>
                    </div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="col-12 col-sm-6">
                        <div class="form-check form-switch">
                            <input class="form-check-input" type="checkbox"
                                name="beacons.${index}.gpsEcoMode" id="beacons.${index}.gpsEcoMode" value="1" ${beacons.gpsEcoMode ? 'checked' : ''}>
                            <label class="form-check-label" for="beacons.${index}.gpsEcoMode">GPS Eco Mode</label>
                        </div>
                    </div>
                </div>
                <div class="row g-2">
                    <div class="form-floating col-12 col-sm-6">
                        <input type="text" class="form-control form-control-sm"
                            name="beacons.${index}.status" id="beacons.${index}.status" value="${beacons.status}">
                        <label for="beacons.${index}.status">Status</label>
                    </div>
                    <div class="form-floating col-12 col-sm-6">
                        <input type="text" class="form-control form-control-sm"
                            name="beacons.${index}.profileLabel" id="beacons.${index}.profileLabel" value="${beacons.profileLabel}">
                        <label for="beacons.${index}.profileLabel">Profile Label</label>
                    </div>
                </div>
            </div>
    `;
}

function loadSettings(settings) {
    currentSettings = settings;
    
    // BEACONS
    const beaconContainer = document.getElementById("beacon-settings");
    beaconContainer.innerHTML = ""; // Clear previous content

    settings.beacons.forEach((beacons, index) => {
        const beaconElement = document.createElement("div");
        beaconElement.classList.add("row", "g-0", "beacons", "border-bottom", "py-2");

        if (beacons.aisProfile) {
            beaconElement.innerHTML = renderAisBeacon(beacons, index);
            beaconContainer.appendChild(beaconElement);
            return;
        }

        beaconElement.innerHTML = `
            <div class="d-flex align-items-start pt-3" style="flex: 0 0 50px; width: 50px;">
                <strong>${index + 1})</strong>
            </div>
            <div class="col">
                <div class="row g-2 mb-2">
                    <div class="form-floating col-12 col-md-6">
                        <input 
                            type="text" 
                            class="form-control form-control-sm" 
                            name="beacons.${index}.callsign" 
                            id="beacons.${index}.callsign" 
                            required
                            value="${beacons.callsign}"
                            oninput="this.value = this.value.toUpperCase();">
                        <label for="beacons.${index}.callsign">Callsign</label>
                    </div>
                    <div class="form-floating col-4 col-md-2">
                        <input 
                            type="text" 
                            class="form-control form-control-sm" 
                            name="beacons.${index}.symbol" 
                            id="beacons.${index}.symbol" 
                            value="${beacons.symbol}">
                        <label for="beacons.${index}.symbol">Symbol</label>
                    </div>
                    <div class="form-floating col-4 col-md-2">
                        <input 
                            type="text" 
                            class="form-control form-control-sm" 
                            name="beacons.${index}.overlay" 
                            id="beacons.${index}.overlay" 
                            value="${beacons.overlay}">
                        <label for="beacons.${index}.overlay">Overlay</label>
                    </div>
                    <div class="form-floating col-4 col-md-2">
                        <input 
                            type="text" 
                            class="form-control form-control-sm" 
                            name="beacons.${index}.micE" 
                            id="beacons.${index}.micE" 
                            value="${beacons.micE}">
                        <label for="beacons.${index}.micE">Mic-E</label>
                    </div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="form-floating col-12">
                        <input 
                            type="text" 
                            class="form-control form-control-sm" 
                            name="beacons.${index}.comment" 
                            id="beacons.${index}.comment" 
                            value="${beacons.comment}">
                        <label for="beacons.${index}.comment">Comment</label>
                    </div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="col-12 col-sm-6">
                        <div class="form-check form-switch">
                            <input 
                                class="form-check-input" 
                                type="checkbox" 
                                name="beacons.${index}.smartBeaconActive" 
                                id="beacons.${index}.smartBeaconActive" 
                                value="1" 
                                ${beacons.smartBeaconActive ? 'checked' : ''}>
                            <label class="form-check-label" for="beacons.${index}.smartBeaconActive">
                                Smart Beacon Active
                            </label>
                        </div>
                    </div>
                    <div class="col-12 col-sm-6">
                        <div class="form-check form-switch">
                            <input 
                                class="form-check-input" 
                                type="checkbox" 
                                name="beacons.${index}.gpsEcoMode" 
                                id="beacons.${index}.gpsEcoMode"
                                value="1"
                                ${beacons.gpsEcoMode ? 'checked' : ''}>
                            <label class="form-check-label" for="beacons.${index}.gpsEcoMode">
                                GPS Eco Mode
                            </label>
                        </div>
                    </div>
                </div>
                <div class="row g-2 mb-2">
                    <div class="col-12 col-md-6">
                        <label for="beacons.${index}.smartBeaconSetting" class="form-label"><small>Smart Beacon Setting</small></label>
                        <select name="beacons.${index}.smartBeaconSetting" id="beacons.${index}.smartBeaconSetting" class="form-control">
                            <optgroup label="SmartBeacon presets">
                                <option value="0" ${beacons.smartBeaconSetting == 0 ? 'selected' : ''}>Human/Runner (Slow Speed)</option>
                                <option value="1" ${beacons.smartBeaconSetting == 1 ? 'selected' : ''}>Bicycle (Mid Speed)</option>
                                <option value="2" ${beacons.smartBeaconSetting == 2 ? 'selected' : ''}>Car/Motorcycle (Fast Speed)</option>
                                <option value="3" ${beacons.smartBeaconSetting == 3 ? 'selected' : ''}>AIS Class A (ITU-R M.1371-6)</option>
                                <option value="4" ${beacons.smartBeaconSetting == 4 ? 'selected' : ''}>AIS Class B "SO" (ITU-R M.1371-6)</option>
                                <option value="5" ${beacons.smartBeaconSetting == 5 ? 'selected' : ''}>AIS Class B "CS" (ITU-R M.1371-6)</option>
                            </optgroup>
                        </select>
                    </div>
                </div>
                <div class="row g-2">
                    <div class="form-floating col-12 col-sm-6">
                        <input 
                            type="text" 
                            class="form-control form-control-sm" 
                            name="beacons.${index}.status" 
                            id="beacons.${index}.status" 
                            value="${beacons.status}">
                        <label for="beacons.${index}.status">Status</label>
                    </div>
                    <div class="form-floating col-12 col-sm-6">
                        <input 
                            type="text" 
                            class="form-control form-control-sm" 
                            name="beacons.${index}.profileLabel" 
                            id="beacons.${index}.profileLabel" 
                            value="${beacons.profileLabel}">
                        <label for="beacons.${index}.profileLabel">Profile Label</label>
                    </div>
                </div>
            </div>
        `;
        beaconContainer.appendChild(beaconElement);
    });

    // ADITIONAL STATION CONFIG
    document.getElementById("simplifiedTrackerMode").checked            = settings.other.simplifiedTrackerMode;
    document.getElementById("sendCommentAfterXBeacons").value           = settings.other.sendCommentAfterXBeacons;
    document.getElementById("path").value                               = settings.other.path;
    document.getElementById("nonSmartBeaconRate").value                 = settings.other.nonSmartBeaconRate;
    document.getElementById("rememberStationTime").value                = settings.other.rememberStationTime;
    document.getElementById("standingUpdateTime").value                 = settings.other.standingUpdateTime;
    document.getElementById("sendAltitude").checked                     = settings.other.sendAltitude ;
    document.getElementById("disableGPS").checked                       = settings.other.disableGPS;
    {
        const cw = settings.cw || { wpm: 15, messages: [] };
        document.getElementById("cw.wpm").value = cw.wpm || 15;
        document.getElementById("cw.trainerLevel").value = cw.trainerLevel ?? 4;
        document.getElementById("cw.trainerGapWpm").value = cw.trainerGapWpm ?? 10;
        document.getElementById("cw.squelchDbm").value = cw.squelchDbm ?? 0;
        document.getElementById("cw.beaconMin").value = cw.beaconMin ?? 0;
        const cwProfiles = document.getElementById("cw-profiles");
        const baseLora = (settings.lora && settings.lora[0]) || { frequency: 433775000, power: 17 };
        const cwDefs = [
            { key: "beacon", title: "1) CW Beacon profile", note: "Used by the autonomous CW beacon." },
            { key: "key",    title: "2) Telegraphy profile", note: "Used by the key / paddles, the fixed messages and the CW trainer." }
        ];
        cwProfiles.innerHTML = "";
        cwDefs.forEach(d => {
            const o = {
                kind: d.key === "beacon" ? "cwb" : "cw", regionName: "cw." + d.key + "Region", freqName: "cw." + d.key + "Freq", powerName: "cw." + d.key + "Power",
                region: cw[d.key + "Region"] || 1, freq: cw[d.key + "Freq"] || 432050000, power: cw[d.key + "Power"] || baseLora.power
            };
            const row = document.createElement("div");
            row.className = "row border-bottom py-2";
            row.innerHTML = `<div class="col-12 px-1 mb-1"><strong>${d.title}</strong> <small class="text-muted">${d.note}</small></div>` + iaruProfileHtml(o);
            cwProfiles.appendChild(row);
            iaruProfileWire(row, o);
        });
        document.getElementById("cw.beaconText").value = cw.beaconText ?? "DE {CALL}/B {LOC} V{VBAT}";
        const wspr = settings.wspr || {};
        const ft = settings.ft || {};
        document.getElementById("ft.ft8Region").value = String(ft.ft8Region ?? 1);
        document.getElementById("ft.ft4Region").value = String(ft.ft4Region ?? 1);
        document.getElementById("ft.ft8FreqMHz").value = ((ft.ft8FreqHz ?? 432174000) / 1e6).toFixed(6).replace(/0+$/, "").replace(/\.$/, "");
        document.getElementById("ft.ft4FreqMHz").value = (ft.ft4FreqHz > 0) ? (ft.ft4FreqHz / 1e6).toFixed(6).replace(/0+$/, "").replace(/\.$/, "") : "";
        document.getElementById("ft.fstRegion").value = String(ft.fstRegion ?? 1);
        document.getElementById("ft.fstFreqMHz").value = (ft.fstFreqHz > 0) ? (ft.fstFreqHz / 1e6).toFixed(6).replace(/0+$/, "").replace(/\.$/, "") : "";
        document.getElementById("ft.fstSeconds").value = String(ft.fstSeconds ?? 120);
        document.getElementById("ft.fstEvery").value = ft.fstEvery ?? 4;
        document.getElementById("ft.powerDbm").value = ft.powerDbm ?? 17;
        document.getElementById("ft.everyCycles").value = ft.everyCycles ?? 4;
        document.getElementById("ft.corrHz").value = ft.corrHz ?? 0;
        document.getElementById("ft.locator").value = ft.locator ?? "";
        document.getElementById("ft.text").value = ft.text ?? "";
        document.getElementById("ft.screenOff").checked = ft.screenOff ?? true;
        ftWire();
        const lr = settings.lrfhss || {};
        document.getElementById("lrfhss.enabled").checked = lr.enabled ?? false;
        document.getElementById("lrfhss.freqHz").value = lr.freqHz ?? 434000000;
        document.getElementById("lrfhss.bwIdx").value = String(lr.bwIdx ?? 0);
        document.getElementById("lrfhss.crIdx").value = String(lr.crIdx ?? 3);
        document.getElementById("lrfhss.narrowGrid").checked = lr.narrowGrid ?? true;
        document.getElementById("lrfhss.powerDbm").value = lr.powerDbm ?? 10;
        document.getElementById("lrfhss.intervalMin").value = String(lr.intervalMin ?? 10);
        document.getElementById("lrfhss.locator").value = lr.locator ?? "";
        document.getElementById("lrfhss.locatorLen").value = String(lr.locatorLen ?? 6);
        lrfhssWire();
        document.getElementById("wspr.freqHz").value = 432301500;      // fixed: not editable
        document.getElementById("wspr.corrHz").value = wspr.corrHz ?? 0;
        document.getElementById("wspr.useGlobalPpm").checked = wspr.useGlobalPpm ?? false;
        document.getElementById("radio.freqCorrPpm").value = ((settings.radioPpm1000 ?? 0) / 1000).toFixed(3);
        document.getElementById("freqcal.apply").onclick = () => {
            const f = parseFloat(document.getElementById("freqcal.freqMHz").value);
            const e = parseFloat(document.getElementById("freqcal.errHz").value);
            const cur = parseFloat(document.getElementById("radio.freqCorrPpm").value) || 0;
            if (!(f > 0) || isNaN(e)) return;
            const next = Math.max(-50, Math.min(50, cur - e / f));
            document.getElementById("radio.freqCorrPpm").value = next.toFixed(3);
            document.getElementById("freqcal.errHz").value = 0;
        };
        const rot = settings.rotor || {};
        {   // objetivos fijos del rotor: 8 filas
            const box = document.getElementById("rotor-targets"), sel = document.getElementById("rotor.targetIndex");
            const tg = rot.targets || [];
            box.innerHTML = ""; sel.innerHTML = "";
            for (let i = 0; i < 8; i++) {
                const t = tg[i] || {};
                const row = document.createElement("div");
                row.className = "row g-1 mb-1";
                row.innerHTML = '<div class="col-3"><input type="text" class="form-control form-control-sm" name="rotor.t' + i + '.n" maxlength="20" placeholder="Name ' + (i + 1) + '"></div>' +
                    '<div class="col-3"><input type="number" class="form-control form-control-sm" name="rotor.t' + i + '.lat" min="-90" max="90" step="0.00001" placeholder="Lat"></div>' +
                    '<div class="col-3"><input type="number" class="form-control form-control-sm" name="rotor.t' + i + '.lon" min="-180" max="180" step="0.00001" placeholder="Lon"></div>' +
                    '<div class="col-1"><input type="number" class="form-control form-control-sm" name="rotor.t' + i + '.alt" min="-500" max="9000" step="1" placeholder="Alt"></div>' +
                    '<div class="col-2"><input type="number" class="form-control form-control-sm" name="rotor.t' + i + '.f" min="0" step="0.0001" placeholder="MHz"></div>';
                box.appendChild(row);
                const q = n => row.querySelector('[name="rotor.t' + i + '.' + n + '"]');
                q("n").value = t.n || "";
                if (t.n) { q("lat").value = t.lat ?? ""; q("lon").value = t.lon ?? ""; q("alt").value = t.alt ?? 0; q("f").value = t.f ? (t.f / 1e6).toFixed(4) : ""; }
                const o = document.createElement("option"); o.value = String(i); o.textContent = String(i + 1); sel.appendChild(o);
            }
            sel.value = String(rot.targetIndex ?? 0);
            document.getElementById("rotor.source").value = String(rot.source ?? 0);
            document.getElementById("rotor.followFilter").value = rot.followFilter ?? "";
            document.getElementById("rotor.followMaxMin").value = rot.followMaxMin ?? 30;
            document.getElementById("rotor.predict").checked = rot.predict ?? true;
            document.getElementById("rotor.predictMaxS").value = rot.predictMaxS ?? 300;
            document.getElementById("rotor.mount").value = String(rot.mount ?? 0);
            document.getElementById("rotor.tilt").value = String(rot.tilt ?? 0);
        }
        document.getElementById("rotor.enabled").checked = rot.enabled ?? false;
        document.getElementById("rotor.transport").value = String(rot.transport ?? 0);
        document.getElementById("rotor.host").value = rot.host ?? "";
        document.getElementById("rotor.port").value = rot.port ?? 4533;
        document.getElementById("rotor.btName").value = rot.btName ?? "";
        document.getElementById("rotor.stepTenths").value = rot.stepTenths ?? 10;
        document.getElementById("rotor.preSec").value = rot.preSec ?? 60;
        document.getElementById("rotor.azOffset").value = rot.azOffset ?? 0;
        document.getElementById("rotor.elOffset").value = rot.elOffset ?? 0;
        document.getElementById("rotor.park").checked = rot.park ?? false;
        document.getElementById("rotor.parkAz").value = rot.parkAz ?? 0;
        document.getElementById("rotor.parkEl").value = rot.parkEl ?? 0;
        document.getElementById("rotor.bleEvents").checked = rot.bleEvents ?? true;
        document.getElementById("rotor.serverEnabled").checked = rot.serverEnabled ?? false;
        document.getElementById("rotor.serverPort").value = rot.serverPort ?? 4533;
        const sat = settings.sat || {};
        document.getElementById("sat.posSource").value = String(sat.posSource ?? 0);
        document.getElementById("sat.minEl").value = sat.minEl ?? 10;
        document.getElementById("sat.lat").value = sat.lat ?? 0;
        document.getElementById("sat.lon").value = sat.lon ?? 0;
        document.getElementById("sat.txEnabled").checked = sat.txEnabled ?? false;
        document.getElementById("sat.txMinEl").value = sat.txMinEl ?? 15;
        document.getElementById("sat.txPower").value = sat.txPower ?? 10;
        document.getElementById("sat.txMinIntervalS").value = sat.txMinIntervalS ?? 30;
        document.getElementById("sat.txText").value = sat.txText ?? "";
        document.getElementById("sat.lnaPin").value = sat.lnaPin ?? -1;
        document.getElementById("sat.paPin").value = sat.paPin ?? -1;
        document.getElementById("sat.pttPin").value = sat.pttPin ?? -1;
        document.getElementById("sat.lnaRev").checked = sat.lnaRev ?? false;
        document.getElementById("sat.paRev").checked = sat.paRev ?? false;
        document.getElementById("sat.pttRev").checked = sat.pttRev ?? false;
        document.getElementById("sat.seqLeadMs").value = sat.seqLeadMs ?? 20;
        document.getElementById("sat.seqPreMs").value = sat.seqPreMs ?? 50;
        document.getElementById("sat.seqPostMs").value = sat.seqPostMs ?? 20;
        document.getElementById("sat.seqTailMs").value = sat.seqTailMs ?? 50;
        if (settings.sa868) {                       // LilyGO T-TWR: radio FM SA868
            const sa = settings.sa868;
            document.getElementById("sa868.rxMHz").value = ((sa.rxFreqHz ?? 144800000) / 1e6).toFixed(4);
            document.getElementById("sa868.txMHz").value = ((sa.txFreqHz ?? 144800000) / 1e6).toFixed(4);
            document.getElementById("sa868.wide").value = sa.wide === true ? "1" : "0";        // APRS: narrow by default
            document.getElementById("sa868.highPower").value = sa.highPower ? "1" : "0";
            ["volume", "squelch", "txLevel", "txDelayMs", "txTailMs"].forEach(k => { if (sa[k] !== undefined) document.getElementById("sa868." + k).value = sa[k]; });
            document.getElementById("sa868.revision").value = String(sa.revision ?? 0);
            document.getElementById("sa868.iaruRegion").value = String(sa.iaruRegion ?? 1);
            sa868IaruWire();
            document.getElementById("sa868.filters").checked = sa.filters !== false;
            document.getElementById("sa868.speaker").checked = sa.speaker !== false;
        }
        const dap = settings.dapnet || {};
        document.getElementById("dapnet.freqMHz").value = ((dap.freqHz ?? 439987500) / 1e6).toFixed(4);
        document.getElementById("dapnet.baud").value = String(dap.baud ?? 1200);
        document.getElementById("dapnet.devKHz").value = ((dap.devTenthsKHz ?? 45) / 10).toFixed(1);
        document.getElementById("dapnet.rxBwTenthsKHz").value = String(dap.rxBwTenthsKHz ?? 146);
        document.getElementById("dapnet.rics").value = dap.rics ?? "";
        document.getElementById("dapnet.charset").value = String(dap.charset ?? 1);
        document.getElementById("dapnet.monitorAll").checked = dap.monitorAll ?? false;
        document.getElementById("dapnet.timeSync").checked = dap.timeSync ?? false;
        document.getElementById("wspr.powerDbm").value = String(wspr.powerDbm ?? 10);
        document.getElementById("wspr.intervalMin").value = String(wspr.intervalMin ?? 10);
        document.getElementById("wspr.locator").value = wspr.locator ?? "";
        document.getElementById("wspr.dither").checked = wspr.dither ?? true;
        document.getElementById("wspr.screenOff").checked = wspr.screenOff ?? false;
        document.getElementById("wspr.driftTenths").value = wspr.driftTenths ?? 0;
        document.getElementById("wspr.perPower").checked = wspr.perPower ?? false;
        for (let i = 0; i < 7; i++) {
            document.getElementById("wspr.ppCorrHz" + i).value = (wspr.ppCorrHz && wspr.ppCorrHz[i] !== undefined) ? wspr.ppCorrHz[i] : (wspr.corrHz ?? 0);
            document.getElementById("wspr.ppDriftTenths" + i).value = (wspr.ppDriftTenths && wspr.ppDriftTenths[i] !== undefined) ? wspr.ppDriftTenths[i] : (wspr.driftTenths ?? 0);
        }
        document.getElementById("cw.keyMode").value = String(cw.keyMode ?? 0);
        document.getElementById("cw.keyPin").value = cw.keyPin ?? -1;
        document.getElementById("cw.dahPin").value = cw.dahPin ?? -1;
        document.getElementById("cw.keypadPin").value = cw.keypadPin ?? -1;
        for (let k = 0; k < 5; k++) document.getElementById("cw.keypadMv" + k).value = (cw.keypadMv && cw.keypadMv[k]) || 0;
        document.getElementById("cw.keyTx").checked = cw.keyTx ?? true;
        document.getElementById("cw.sidetone").checked = cw.sidetone ?? true;
        document.getElementById("cw.swapPaddles").checked = cw.swapPaddles ?? false;
        const box = document.getElementById("cw-messages");
        box.innerHTML = "";
        for (let i = 0; i < 5; i++) {
            const div = document.createElement("div");
            div.className = "form-floating px-1 mb-2";
            const input = document.createElement("input");
            input.type = "text"; input.maxLength = 60; input.className = "form-control form-control-sm";
            input.name = "cw.message." + i; input.id = "cw.message." + i;
            input.value = (cw.messages && cw.messages[i]) || "";
            const label = document.createElement("label");
            label.htmlFor = input.id; label.textContent = "CW message " + (i + 1);
            div.appendChild(input); div.appendChild(label);
            box.appendChild(div);
        }
    }
    document.getElementById("cadActive").checked                        = settings.other.cadActive;
    document.getElementById("cadWaitSec").value                         = settings.other.cadWaitSec ?? 10;
    document.getElementById("cadFskRssi").value                         = settings.other.cadFskRssi ?? -95;
    document.getElementById("email").value                              = settings.other.email;

    // DISPLAY
    document.getElementById("display.ecoMode").checked                  = settings.display.ecoMode;
    document.getElementById("display.turn180").checked                  = settings.display.turn180;
    document.getElementById("display.timeout").value                    = settings.display.timeout;
    document.getElementById("display.showSymbol").checked               = settings.display.showSymbol;
    DisplayEcoModeCheckbox.checked  = settings.display.ecoMode;
    DisplayTimeout.disabled         = !DisplayEcoModeCheckbox.checked;

    // BLUETOOTH
    document.getElementById("bluetooth.active").checked                 = settings.bluetooth.active;
    document.getElementById("bluetooth.deviceName").value               = settings.bluetooth.deviceName;
    document.getElementById("bluetooth.useBLE").value                   = settings.bluetooth.useBLE ? "1" : "0";
    document.getElementById("bluetooth.useKISS").value                  = settings.bluetooth.useKISS ? "1" : "0";
    document.getElementById("bluetooth.controlPin").value               = String(settings.bluetooth.controlPin === undefined ? 123456 : settings.bluetooth.controlPin).padStart(6, "0");
    BluetoothActiveCheckbox.checked = settings.bluetooth.active;
    BluetoothDeviceName.disabled    = !BluetoothActiveCheckbox.checked;
    BluetoothUseBle.disabled        = !BluetoothActiveCheckbox.checked;
    BluetoothUseKiss.disabled       = !BluetoothActiveCheckbox.checked;
    document.getElementById("bluetooth.controlPin").disabled = !BluetoothActiveCheckbox.checked;

    // LORA
    const loraContainer = document.getElementById("lora-settings");
    loraContainer.innerHTML = ""; // Clear previous content
    const fskContainer = document.getElementById("fsk-settings");
    fskContainer.innerHTML = "";

    settings.lora.forEach((lora, index) => {
        const loraElement = document.createElement("div");
        loraElement.classList.add("row", "lora", "border-bottom", "py-2");

        loraElement.innerHTML = `
            <div class="col-1 px-1 mb-2 d-flex align-items-center">
                <strong>${index + 1})</strong> <!-- Adding numbering here -->
            </div>
            ${lora.ais ? '<div class="col-12 px-1 mb-1"><span class="badge text-bg-primary">AIS nautical</span> <small class="text-muted">Used automatically by the AIS nautical beacon profile. SF 9 and CR 4/5 are fixed.</small></div>' : ''}
            ${lora.ais ? '' : `
            <div class="form-floating col-6 col-md-4 px-1 mb-2">
                <select class="form-select form-select-sm" name="lora.${index}.iaruRegion" id="lora.${index}.iaruRegion">
                    ${[1, 2, 3].map(r => `<option value="${r}" ${(lora.iaruRegion || 1) == r ? 'selected' : ''}>${IARU_REGIONS[r].name}</option>`).join('')}
                </select>
                <label for="lora.${index}.iaruRegion">IARU region</label>
            </div>
            <div class="form-floating col-12 col-md-4 px-1 mb-2">
                <select class="form-select form-select-sm" id="lora.${index}.preset"></select>
                <label for="lora.${index}.preset">IARU band plan channel</label>
            </div>
            <div class="col-12 px-1 mb-1"><small class="text-muted" id="lora.${index}.bandinfo"></small></div>
            `}
            <div class="form-floating col-6 col-md-3 px-1 mb-2">
                <input 
                    type="number" 
                    class="form-control form-control-sm" 
                    name="lora.${index}.frequency" 
                    id="lora.${index}.frequency" 
                    min="137000000"
                    max="1020000000"
                    required
                    value="${lora.frequency}">
                <label for="lora.${index}.frequency">Frequency</label>
            </div>
            <div class="form-floating col-4 col-md-2 px-1 mb-2">
                <input 
                    type="number" 
                    class="form-control form-control-sm" 
                    name="lora.${index}.spreadingFactor" 
                    id="lora.${index}.spreadingFactor" 
                    value="${lora.spreadingFactor}"
                    min="7"
                    max="12"
                    ${lora.ais ? 'readonly' : ''}
                    required>
                <label for="lora.${index}.spreadingFactor">SF</label>
            </div>
            <div class="form-floating col-4 col-md-2 px-1 mb-2">
                <input 
                    type="number" 
                    class="form-control form-control-sm" 
                    name="lora.${index}.codingRate4" 
                    id="lora.${index}.codingRate4" 
                    value="${lora.codingRate4}"
                    min="5"
                    max="8"
                    ${lora.ais ? 'readonly' : ''}
                    required>
                <label for="lora.${index}.codingRate4">CR4</label>
            </div>
            <div class="form-floating col-4 col-md-2 px-1 mb-2">
                <input 
                    type="number" 
                    class="form-control form-control-sm" 
                    name="lora.${index}.signalBandwidth" 
                    id="lora.${index}.signalBandwidth" 
                    value="${lora.signalBandwidth}"
                    min="62500"
                    max="500000"
                    required>
                <label for="lora.${index}.signalBandwidth">BW</label>
            </div>
            <div class="form-floating col-4 col-md-2 px-1 mb-2">
                <input
                    type="number"
                    class="form-control form-control-sm"
                    name="lora.${index}.power"
                    id="lora.${index}.power"
                    value="${lora.power}"
                    min="2"
                    max="22"
                    required>
                <label for="lora.${index}.power">Power (dBm)</label>
            </div>
        `;
        loraContainer.appendChild(loraElement);

        if (!lora.ais) {
            const fskElement = document.createElement("div");
            fskElement.classList.add("row", "fsk", "border-bottom", "py-2");
            fskElement.innerHTML = `
            <div class="col-1 px-1 mb-2 d-flex align-items-center">
                <strong>${index + 1})</strong>
            </div>
            ${iaruProfileHtml({ kind: "fsk", regionName: `lora.${index}.fskIaruRegion`, freqName: `lora.${index}.fskFrequency`, powerName: `lora.${index}.fskPower`, region: lora.fskIaruRegion || lora.iaruRegion || 1, freq: lora.fskFrequency || lora.frequency, power: lora.fskPower || lora.power })}
            <div class="form-floating col-4 col-md-3 px-1 mb-2">
                <input type="number" step="0.1" min="1.2" max="100" class="form-control form-control-sm" name="lora.${index}.fskBitRate" id="lora.${index}.fskBitRate" value="${lora.fskBitRate ?? 9.6}">
                <label for="lora.${index}.fskBitRate">FSK bit rate (kbps)</label>
            </div>
            <div class="form-floating col-4 col-md-3 px-1 mb-2">
                <input type="number" step="0.1" min="0.6" max="200" class="form-control form-control-sm" name="lora.${index}.fskFreqDev" id="lora.${index}.fskFreqDev" value="${lora.fskFreqDev ?? 3.0}">
                <label for="lora.${index}.fskFreqDev">FSK deviation (kHz)</label>
            </div>
            <div class="form-floating col-4 col-md-3 px-1 mb-2">
                <input type="number" step="0.1" min="4.8" max="500" class="form-control form-control-sm" name="lora.${index}.fskRxBandwidth" id="lora.${index}.fskRxBandwidth" value="${lora.fskRxBandwidth ?? 39.0}">
                <label for="lora.${index}.fskRxBandwidth">FSK Rx bandwidth (kHz)</label>
            </div>
            <div class="form-floating col-12 col-md-5 px-1 mb-2">
                <select class="form-select form-select-sm" name="lora.${index}.fskShaping" id="lora.${index}.fskShaping" title="Gaussian filter of the FSK transmitter. GFSK BT 0.5 is the G3RUH standard. For true GMSK use deviation = bit rate / 4 (2.4 kHz at 9.6 kbps).">
                    <option value="1">GFSK, BT 0.5 (G3RUH, default)</option>
                    <option value="2">GMSK, BT 0.3</option>
                    <option value="3">GFSK, BT 0.7</option>
                    <option value="4">GFSK, BT 1.0</option>
                    <option value="0">FSK (no filter)</option>
                </select>
                <label for="lora.${index}.fskShaping">Modulation / Gaussian filter (TX)</label>
            </div>
            <div class="form-floating col-12 col-md-5 px-1 mb-2">
                <select class="form-select form-select-sm" name="lora.${index}.fskFec" id="lora.${index}.fskFec" title="Error correction added to the transmitted packet. FX.25 is still plain AX.25 for stations that do not know it. The receiver always understands AX.25 and FX.25.">
                    <option value="0">AX.25 (no error correction)</option>
                    <option value="1">FX.25, 16 check bytes (corrects 8)</option>
                    <option value="2">FX.25, 32 check bytes (corrects 16)</option>
                    <option value="3">FX.25, 64 check bytes (corrects 32)</option>
                    <option value="4">IL2P (only IL2P stations hear it)</option>
                </select>
                <label for="lora.${index}.fskFec">Packet protocol (TX)</label>
            </div>
            <div class="col-12 col-md-7 px-1 mb-2 d-flex align-items-center"><small class="text-muted">FX.25 adds Reed-Solomon error correction and any AX.25 station can still read the packet; frames longer than about 120 bytes are sent as plain AX.25. IL2P is more compact but not compatible with AX.25: use it only with IL2P receivers (Dire Wolf 1.7 or later).</small></div>
            <div class="col-12 col-md-7 px-1 mb-2 d-flex align-items-center"><small class="text-muted">GMSK = minimum shift keying with a Gaussian filter: set the deviation to bit rate / 4 (2.4 kHz at 9.6 kbps). The receiver does not depend on this setting. SX127x radios have no BT 0.7 (BT 0.5 is used instead); LLCC68 boards have no FSK.</small></div>
            `;
            fskContainer.appendChild(fskElement);
            iaruProfileWire(fskElement, { kind: "fsk", regionName: `lora.${index}.fskIaruRegion`, freqName: `lora.${index}.fskFrequency`, powerName: `lora.${index}.fskPower` });
            fskElement.querySelector('[id="lora.' + index + '.fskShaping"]').value = String(lora.fskShaping ?? 1);
            fskElement.querySelector('[id="lora.' + index + '.fskFec"]').value = String(lora.fskFec ?? 0);
        }

        if (!lora.ais) {
            const $ = id => loraElement.querySelector('[id="lora.' + index + '.' + id + '"]');
            const regSel = $("iaruRegion"), preset = $("preset"), freq = $("frequency"), info = $("bandinfo");
            const refresh = () => {
                const region = parseInt(regSel.value, 10);
                const ok = iaruCheckFrequency(region, parseFloat(freq.value));
                info.className = ok ? "text-muted" : "text-danger fw-semibold";
                info.textContent = "Amateur allocation, " + IARU_REGIONS[region].name + ": " + iaruBandText(region)
                    + (ok ? "" : " — the frequency is OUTSIDE this region's amateur allocation") + ". National rules prevail.";
            };
            const fillPresets = () => {
                const region = parseInt(regSel.value, 10);
                const opts = ['<option value="">Custom</option>'].concat(IARU_CHANNELS[region].map(c => '<option value="' + Math.round(c[1] * 1e6) + '">' + c[0] + '</option>'));
                preset.innerHTML = opts.join("");
                const cur = String(Math.round(parseFloat(freq.value)));
                if ([...preset.options].some(o => o.value === cur)) preset.value = cur;
            };
            preset.addEventListener("change", () => { if (preset.value) { freq.value = preset.value; } refresh(); });
            regSel.addEventListener("change", () => { fillPresets(); refresh(); });
            freq.addEventListener("input", () => {
                const cur = String(Math.round(parseFloat(freq.value)));
                preset.value = [...preset.options].some(o => o.value === cur) ? cur : "";
                refresh();
            });
            fillPresets();
            refresh();
        }
    });

    // GPS receiver
    const gpsCfg = settings.gps || {};
    const gpsMask = gpsCfg.gnssMask ?? 35;
    GPS_GNSS_BITS.forEach(([id, bit]) => { document.getElementById(id).checked = (gpsMask & bit) !== 0; });
    document.getElementById("gps.assistNow").checked        = gpsCfg.assistNow ?? true;
    document.getElementById("gps.dynModel").value           = String(gpsCfg.dynModel ?? 0);
    document.getElementById("gps.rateMs").value             = String(gpsCfg.rateMs ?? 1000);
    document.getElementById("gps.nmeaLean").checked         = gpsCfg.nmeaLean ?? true;
    document.getElementById("gps.minElev").value            = gpsCfg.minElev ?? 5;
    document.getElementById("gps.staticHoldCms").value      = gpsCfg.staticHoldCms ?? 0;
    document.getElementById("gps.staticHoldDistM").value    = gpsCfg.staticHoldDistM ?? 0;
    document.getElementById("gps.sbasDiffCorr").checked     = gpsCfg.sbasDiffCorr ?? true;
    document.getElementById("gps.sbasRanging").checked      = gpsCfg.sbasRanging ?? true;
    document.getElementById("gps.sbasIntegrity").checked    = gpsCfg.sbasIntegrity ?? false;
    document.getElementById("gps.ppsEnabled").checked       = gpsCfg.ppsEnabled ?? true;
    document.getElementById("gps.ppsFreqHz").value          = String(gpsCfg.ppsFreqHz ?? 1);
    document.getElementById("gps.ppsLenMs").value           = gpsCfg.ppsLenMs ?? 100;
    document.getElementById("gps.ppsRising").value          = (gpsCfg.ppsRising ?? true) ? "1" : "0";
    document.getElementById("gps.ppsCableDelayNs").value    = gpsCfg.ppsCableDelayNs ?? 50;
    document.getElementById("gps.ppsUserDelayNs").value     = gpsCfg.ppsUserDelayNs ?? 0;
    gpsCheckGnss();
    fetchGpsInfo();

    // BATTERY
    document.getElementById("battery.sendVoltage").checked              = settings.battery.sendVoltage;
    document.getElementById("battery.voltageAsTelemetry").checked       = settings.battery.voltageAsTelemetry;
    document.getElementById("battery.sendVoltageAlways").checked        = settings.battery.sendVoltageAlways;
    BatterySendVoltageCheckbox.checked  = settings.battery.sendVoltage;
    BatteryVoltageAsTelemetry.disabled  = !BatterySendVoltageCheckbox.checked;
    BatteryForceSendVoltage.disabled    = !BatterySendVoltageCheckbox.checked;

    document.getElementById("battery.monitorVoltage").checked           = settings.battery.monitorVoltage;
    document.getElementById("battery.sleepVoltage").value               = settings.battery.sleepVoltage.toFixed(1);
    BatteryMonitorVoltageCheckbox.checked   = settings.battery.monitorVoltage;
    BatteryMonitorSleepVoltage.disabled     = !BatteryMonitorVoltageCheckbox.checked;

    // TELEMETRY (WX Sensor)
    document.getElementById("telemetry.active").checked                  = settings.telemetry.active;
    document.getElementById("telemetry.sendTelemetry").checked           = settings.telemetry.sendTelemetry;
    document.getElementById("telemetry.temperatureCorrection").value     = settings.telemetry.temperatureCorrection.toFixed(1);
    TelemetryCheckbox.checked           = settings.telemetry.active;
    TelemetrySendCheckbox.disabled      = !TelemetryCheckbox.checked;
    TelemetryTempCorrection.disabled    = !TelemetryCheckbox.checked;

    // WINLINK
    document.getElementById("winlink.password").value                   = settings.winlink.password;

    // WiFi AP
    document.getElementById("wifiAP.password").value                    = settings.wifiAP.password;

    // KISS OVER WIFI
    if (settings.kissNet) {
        document.getElementById("kissNet.enabled").checked              = settings.kissNet.enabled;
        document.getElementById("kissNet.tcpPort").value                = settings.kissNet.tcpPort;
        document.getElementById("kissNet.udpPort").value                = settings.kissNet.udpPort;
        document.getElementById("kissNet.allowed").value                = settings.kissNet.allowed;
        kissNetUpdate();
    }

    // NMEA OUTPUT (UART)
    if (settings.nmeaUart) {
        document.getElementById("nmeaUart.txPin").value                 = settings.nmeaUart.txPin;
        document.getElementById("nmeaUart.baud").value                  = settings.nmeaUart.baud;
    }

    // IARU REGION (AUTO)
    if (settings.iaruAuto) {
        document.getElementById("iaruAuto.mode").value          = String(settings.iaruAuto.mode ?? 0);
        document.getElementById("iaruAuto.confirm").checked     = settings.iaruAuto.confirm ?? true;
    }
    iaruAutoUpdate();

    // CAT OVER WIFI
    if (settings.cat) {
        document.getElementById("cat.enabled").checked                  = settings.cat.enabled;
        document.getElementById("cat.usb").checked                      = !!settings.cat.usb;
        document.getElementById("cat.port").value                       = settings.cat.port;
        document.getElementById("cat.tuneKHz").value                    = settings.cat.tuneKHz;
        document.getElementById("cat.allowed").value                    = settings.cat.allowed;
        document.getElementById("cat.pttBeacon").checked                = settings.cat.pttBeacon;
        catUpdate();
    }

    if (typeof boardPinRefresh === "function") boardPinRefresh();      // pin hints of the board

    // GROUP WATCH / REMOTE EMERGENCY
    if (settings.team) {
        document.getElementById("team.watchActive").checked             = settings.team.watchActive;
        document.getElementById("team.watchCalls").value                = settings.team.watchCalls;
        document.getElementById("team.timeoutMin").value                = settings.team.timeoutMin;
        document.getElementById("team.maxKm").value                     = settings.team.maxKm;
        document.getElementById("team.pollSec").value                   = settings.team.pollSec;
        document.getElementById("team.eaiActive").checked               = settings.team.eaiActive;
        document.getElementById("team.eaiCalls").value                  = settings.team.eaiCalls;
        document.getElementById("team.eaiCode").value                   = settings.team.eaiCode;
        document.getElementById("team.eaiMinutes").value                = settings.team.eaiMinutes;
    }

    // FIRMWARE UPDATE
    if (settings.fwUpdate) {
        document.getElementById("fwUpdate.ssid").value                  = settings.fwUpdate.ssid;
        document.getElementById("fwUpdate.password").value              = settings.fwUpdate.password;
    }

    // NOTIFICATION
    document.getElementById("notification.ledTx").checked               = settings.notification.ledTx;
    document.getElementById("notification.ledTxPin").value              = settings.notification.ledTxPin;
    NotificationLedTxCheckbox.checked   = settings.notification.ledTx;
    NotificationLedTxPin.disabled       = !NotificationLedTxCheckbox.checked;

    document.getElementById("notification.ledMessage").checked          = settings.notification.ledMessage;
    document.getElementById("notification.ledMessagePin").value         = settings.notification.ledMessagePin;
    document.getElementById("notification.ledRxBeacon").checked         = !!settings.notification.ledRxBeacon;
    document.getElementById("notification.flashEmergency").checked      = !!settings.notification.flashEmergency;
    document.getElementById("notification.ledTxCw").checked             = !!settings.notification.ledTxCw;
    document.getElementById("notification.ledRxCw").checked             = !!settings.notification.ledRxCw;
    document.getElementById("notification.flashCw").checked             = !!settings.notification.flashCw;
    NotificationLedMessageCheckbox.checked  = settings.notification.ledMessage;
    ledMessagePinUpdate();
    
    document.getElementById("notification.buzzerActive").checked        = settings.notification.buzzerActive;
    document.getElementById("notification.buzzerPinTone").value         = settings.notification.buzzerPinTone;
    document.getElementById("notification.buzzerPinVcc").value          = settings.notification.buzzerPinVcc;
    document.getElementById("notification.bootUpBeep").checked          = settings.notification.bootUpBeep;
    document.getElementById("notification.txBeep").checked              = settings.notification.txBeep;
    document.getElementById("notification.messageRxBeep").checked       = settings.notification.messageRxBeep;
    document.getElementById("notification.stationBeep").checked         = settings.notification.stationBeep;
    document.getElementById("notification.lowBatteryBeep").checked      = settings.notification.lowBatteryBeep;
    document.getElementById("notification.shutDownBeep").checked        = settings.notification.shutDownBeep;
    document.getElementById("notification.bellRings").checked           = settings.notification.bellRings ?? true;
    NotificationBuzzerCheckbox.checked          = settings.notification.buzzerActive;
    NotificationBuzzerTonePin.disabled          = !NotificationBuzzerCheckbox.checked;
    NotificationBuzzerVccPin.disabled           = !NotificationBuzzerCheckbox.checked;
    NotificationBuzzerBootUpBeep.disabled       = !NotificationBuzzerCheckbox.checked;
    NotificationBuzzerTxBeep.disabled           = !NotificationBuzzerCheckbox.checked;
    NotificationBuzzerMessageBeep.disabled      = !NotificationBuzzerCheckbox.checked;
    NotificationBuzzerStationBeep.disabled      = !NotificationBuzzerCheckbox.checked;
    NotificationBuzzerLowBatteryBeep.disabled   = !NotificationBuzzerCheckbox.checked;
    NotificationBuzzerShutDownBeep.disabled     = !NotificationBuzzerCheckbox.checked;

    document.getElementById("notification.ledFlashlight").checked       = settings.notification.ledFlashlight;
    document.getElementById("notification.ledFlashlightPin").value      = settings.notification.ledFlashlightPin;
    NotificationLedFlashlightCheckbox.checked   = settings.notification.ledFlashlight;
    NotificationLedFlashlightPin.disabled       = !NotificationLedFlashlightCheckbox.checked;
    
    //  PTT Trigger
    document.getElementById("ptt.active").checked                       = settings.pttTrigger.active;
    document.getElementById("ptt.reverse").checked                      = settings.pttTrigger.reverse;
    document.getElementById("ptt.preDelay").value                       = settings.pttTrigger.preDelay;
    document.getElementById("ptt.postDelay").value                      = settings.pttTrigger.postDelay;
    document.getElementById("ptt.io_pin").value                         = settings.pttTrigger.io_pin;
    pttTriggerCheckbox.checked  = settings.pttTrigger.active;
    pttReverseCheckbox.disabled = !pttTriggerCheckbox.checked;
    pttPreDelayInput.disabled   = !pttTriggerCheckbox.checked;
    pttPostDelayInput.disabled  = !pttTriggerCheckbox.checked;
    pttPinInput.disabled        = !pttTriggerCheckbox.checked;

    clampOutOfRangeInputs();
}

function showToast(message) {
    const el = document.querySelector('#toast');

    el.querySelector('.toast-body').innerHTML = message;

    (el._t || new bootstrap.Toast(el)).show();
}

function clampOutOfRangeInputs() {
    const fixed = [];

    document.querySelectorAll("#configuration input[type=number]").forEach((el) => {
        if (el.disabled || el.value === "") return;

        const value = parseFloat(el.value);
        if (isNaN(value)) return;

        const min = el.min !== "" ? parseFloat(el.min) : null;
        const max = el.max !== "" ? parseFloat(el.max) : null;

        let clamped = value;
        if (max !== null && value > max) clamped = max;
        if (min !== null && value < min) clamped = min;

        if (clamped !== value) {
            el.value = clamped;
            fixed.push((el.name || el.id) + ": " + value + " &rarr; " + clamped);
        }
    });

    if (fixed.length) {
        showToast("Stored values outside the allowed range, adjusted:<br>" + fixed.join("<br>")
                  + "<br><small>Save to keep them.</small>");
    }
}

document.getElementById('reboot').addEventListener('click', function (e) {
    e.preventDefault();

    fetch("/action?type=reboot", { method: "POST" });

    showToast("Your device will be rebooted in a while");
});



// ---- Firmware update (board information and upload of a file; the update from the internet is done by the Android app) ----
function fwEsc(t) { return String(t).replace(/[&<>"]/g, c => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;"}[c])); }

function fwLoadInfo() {
    fetch("/api/info?_t=" + Date.now())
        .then(r => r.json())
        .then(i => {
            document.getElementById("fw-info").innerHTML = "Board: <strong>" + fwEsc(i.board) + "</strong> &middot; version <strong>" + fwEsc(i.version)
                + "</strong> &middot; " + fwEsc(i.chip) + " &middot; flash " + Math.round(i.flashSize / 1048576) + " MB";
            document.getElementById("fw-ota-box").style.display  = i.ota ? "" : "none";
            document.getElementById("fw-usb-note").style.display = i.ota ? "none" : "";
        })
        .catch(() => { document.getElementById("fw-info").textContent = "Board information not available."; });
}

document.getElementById("fw-upload").addEventListener("click", () => {
    const f = document.getElementById("fw-file").files[0];
    if (!f) { showToast("Choose the .bin file first"); return; }
    if (!confirm("Install " + f.name + " now? The tracker will restart.")) return;
    const wrap = document.getElementById("fw-progress-wrap"), bar = document.getElementById("fw-progress");
    wrap.style.display = "";
    const xhr = new XMLHttpRequest();
    const fd = new FormData();
    fd.append("firmware", f, f.name);
    xhr.open("POST", "/api/update");
    xhr.upload.onprogress = e => { if (e.lengthComputable) { const p = Math.round(100 * e.loaded / e.total); bar.style.width = p + "%"; bar.textContent = p + " %"; } };
    xhr.onload = () => {
        let r = {};
        try { r = JSON.parse(xhr.responseText); } catch (e) {}
        if (r.ok) showToast("Firmware installed. The tracker is restarting.");
        else showToast("Update failed: " + fwEsc(r.error || xhr.status));
    };
    xhr.onerror = () => showToast("Connection lost during the upload. If the tracker restarts, check the version.");
    xhr.send(fd);
});

fwLoadInfo();

// ---- Yaesu FH-2 keypad: read the ADC pin to calibrate keys 1-5 ----
function kpRead(cb) {
    fetch("/api/keypad?pin=" + encodeURIComponent(document.getElementById("cw.keypadPin").value) + "&_t=" + Date.now())
        .then(r => r.json())
        .then(j => cb(j))
        .catch(() => cb(null));
}
document.querySelectorAll(".kp-read").forEach(btn => btn.addEventListener("click", () => {
    kpRead(j => {
        if (!j || !j.ok) { showToast("Choose a valid ADC GPIO first"); return; }
        if (j.mv > 3000) { showToast("No key pressed (" + j.mv + " mV). Hold the key while clicking Read."); return; }
        document.getElementById("cw.keypadMv" + btn.dataset.k).value = j.mv;
        showToast("Key " + (Number(btn.dataset.k) + 1) + ": " + j.mv + " mV. Save the configuration to keep it.");
    });
}));
setInterval(() => {
    if (document.getElementById("cw.keypadPin").value < 0 || document.hidden) { document.getElementById("kp-live").textContent = "Live: -"; return; }
    kpRead(j => { document.getElementById("kp-live").textContent = (j && j.ok) ? "Live: " + j.mv + " mV" : "Live: -"; });
}, 1500);

// Display Switches
const DisplayEcoModeCheckbox    = document.querySelector('input[name="display.ecoMode"]');
const DisplayTimeout            = document.querySelector('input[name="display.timeout"]');
DisplayEcoModeCheckbox.addEventListener("change", function () {
    DisplayTimeout.disabled     = !this.checked;
});

// Bluetooth Switches
// KISS over WiFi: like Bluetooth, the switch enables or disables its fields
function kissNetUpdate() {
    const on = document.getElementById("kissNet.enabled").checked;
    ["kissNet.tcpPort", "kissNet.udpPort", "kissNet.allowed"].forEach((id) => { document.getElementById(id).disabled = !on; });
}
document.getElementById("kissNet.enabled").addEventListener("change", kissNetUpdate);

// IARU region (auto): the confirm switch only matters when the mode is not Off; "Detect now" asks the tracker to check the GPS position
function iaruAutoUpdate() {
    const on = document.getElementById("iaruAuto.mode").value !== "0";
    document.getElementById("iaruAuto.confirm").disabled = !on;
}
function iaruStatus(s) {
    const el = document.getElementById("iaru-status");
    if (!el) return;
    const reg = s && s.region ? "Detected: " + IARU_REGIONS[s.region].name : (s && s.fix ? "GPS fix, region not stable yet" : "No GPS fix");
    el.textContent = reg + (s && s.asking ? " \u2014 waiting for your answer on the tracker screen" : "");
}
document.getElementById("iaruAuto.mode").addEventListener("change", iaruAutoUpdate);
document.getElementById("iaru-detect").addEventListener("click", () => {
    fetch("/action?type=iaru-detect", { method: "POST" }).then(r => r.json()).then(iaruStatus).catch(() => {});
});
fetch("/api/iaru").then(r => r.json()).then(iaruStatus).catch(() => {});

// CAT over WiFi: same behaviour
function catUpdate() {
    const wifi = document.getElementById("cat.enabled").checked, usb = document.getElementById("cat.usb").checked;
    ["cat.port", "cat.allowed"].forEach((id) => { document.getElementById(id).disabled = !wifi; });
    ["cat.tuneKHz", "cat.pttBeacon"].forEach((id) => { document.getElementById(id).disabled = !(wifi || usb); });     // shared by both links
}
document.getElementById("cat.enabled").addEventListener("change", catUpdate);
document.getElementById("cat.usb").addEventListener("change", catUpdate);

const BluetoothActiveCheckbox   = document.querySelector('input[name="bluetooth.active"]');
const BluetoothDeviceName       = document.querySelector('input[name="bluetooth.deviceName"]');
const BluetoothUseBle           = document.querySelector('select[name="bluetooth.useBLE"]');
const BluetoothUseKiss          = document.querySelector('select[name="bluetooth.useKISS"]');
const BluetoothControlPin       = document.querySelector('input[name="bluetooth.controlPin"]');
BluetoothActiveCheckbox.addEventListener("change", function () {
    BluetoothDeviceName.disabled    = !this.checked;
    BluetoothUseBle.disabled        = !this.checked;
    BluetoothUseKiss.disabled       = !this.checked;
    BluetoothControlPin.disabled    = !this.checked;
});

// Battery Switches
const BatterySendVoltageCheckbox    = document.querySelector('input[name="battery.sendVoltage"]');
const BatteryVoltageAsTelemetry     = document.querySelector('input[name="battery.voltageAsTelemetry"]');
const BatteryForceSendVoltage       = document.querySelector('input[name="battery.sendVoltageAlways"]');
BatterySendVoltageCheckbox.addEventListener("change", function () {
    BatteryVoltageAsTelemetry.disabled  = !this.checked;
    BatteryForceSendVoltage.disabled    = !this.checked;
});

const BatteryMonitorVoltageCheckbox = document.querySelector('input[name="battery.monitorVoltage"]');
const BatteryMonitorSleepVoltage    = document.querySelector('input[name="battery.sleepVoltage"]');
BatteryMonitorVoltageCheckbox.addEventListener("change", function () {
    BatteryMonitorSleepVoltage.disabled = !this.checked;
});

// Telemetry Switches
const TelemetryCheckbox         = document.querySelector('input[name="telemetry.active"]');
const TelemetrySendCheckbox     = document.querySelector('input[name="telemetry.sendTelemetry"]');
const TelemetryTempCorrection   = document.querySelector('input[name="telemetry.temperatureCorrection"]');
TelemetryCheckbox.addEventListener("change", function () {
    TelemetrySendCheckbox.disabled      = !this.checked;
    TelemetryTempCorrection.disabled    = !this.checked;
});

// Notifications Switches
const NotificationLedTxCheckbox         = document.querySelector('input[name="notification.ledTx"]');
const NotificationLedTxPin              = document.querySelector('input[name="notification.ledTxPin"]');
NotificationLedTxCheckbox.addEventListener("change", function () {
    NotificationLedTxPin.disabled       = !this.checked;
});

const NotificationLedMessageCheckbox    = document.querySelector('input[name="notification.ledMessage"]');
const NotificationLedMessagePin         = document.querySelector('input[name="notification.ledMessagePin"]');
function ledMessagePinUpdate() {
    NotificationLedMessagePin.disabled = !(NotificationLedMessageCheckbox.checked || document.getElementById("notification.ledRxBeacon").checked || document.getElementById("notification.ledRxCw").checked);
}
document.getElementById("notification.ledRxBeacon").addEventListener("change", ledMessagePinUpdate);
document.getElementById("notification.ledRxCw").addEventListener("change", ledMessagePinUpdate);
NotificationLedMessageCheckbox.addEventListener("change", function () {
    ledMessagePinUpdate();
});

const NotificationBuzzerCheckbox        = document.querySelector('input[name="notification.buzzerActive"]');
const NotificationBuzzerTonePin         = document.querySelector('input[name="notification.buzzerPinTone"]');
const NotificationBuzzerVccPin          = document.querySelector('input[name="notification.buzzerPinVcc"]');
const NotificationBuzzerBootUpBeep      = document.querySelector('input[name="notification.bootUpBeep"]');
const NotificationBuzzerTxBeep          = document.querySelector('input[name="notification.txBeep"]');
const NotificationBuzzerMessageBeep     = document.querySelector('input[name="notification.messageRxBeep"]');
const NotificationBuzzerStationBeep     = document.querySelector('input[name="notification.stationBeep"]');
const NotificationBuzzerLowBatteryBeep  = document.querySelector('input[name="notification.lowBatteryBeep"]');
const NotificationBuzzerShutDownBeep    = document.querySelector('input[name="notification.shutDownBeep"]');
const NotificationBuzzerBellRings       = document.querySelector('input[name="notification.bellRings"]');
NotificationBuzzerCheckbox.addEventListener("change", function () {
    NotificationBuzzerTonePin.disabled          = !this.checked;
    NotificationBuzzerVccPin.disabled           = !this.checked;
    NotificationBuzzerBootUpBeep.disabled       = !this.checked;
    NotificationBuzzerTxBeep.disabled           = !this.checked;
    NotificationBuzzerMessageBeep.disabled      = !this.checked;
    NotificationBuzzerStationBeep.disabled      = !this.checked;
    NotificationBuzzerLowBatteryBeep.disabled   = !this.checked;
    NotificationBuzzerShutDownBeep.disabled     = !this.checked;
    NotificationBuzzerBellRings.disabled        = !this.checked;
});

const NotificationLedFlashlightCheckbox = document.querySelector('input[name="notification.ledFlashlight"]');
const NotificationLedFlashlightPin      = document.querySelector('input[name="notification.ledFlashlightPin"]');
NotificationLedFlashlightCheckbox.addEventListener("change", function () {
    NotificationLedFlashlightPin.disabled  = !this.checked;
});

// PTT Switches
const pttTriggerCheckbox    = document.querySelector('input[name="ptt.active"]');
const pttReverseCheckbox    = document.querySelector('input[name="ptt.reverse"]');
const pttPreDelayInput      = document.querySelector('input[name="ptt.preDelay"]');
const pttPostDelayInput     = document.querySelector('input[name="ptt.postDelay"]');
const pttPinInput           = document.querySelector('input[name="ptt.io_pin"]');
pttTriggerCheckbox.addEventListener("change", function () {
    pttReverseCheckbox.disabled = !this.checked;
    pttPreDelayInput.disabled   = !this.checked;
    pttPostDelayInput.disabled  = !this.checked;
    pttPinInput.disabled        = !this.checked;
});




const form = document.querySelector("form");

const saveModal = new bootstrap.Modal(document.getElementById("saveModal"), {
    backdrop: "static",
    keyboard: false,
});

const savedModal = new bootstrap.Modal(
    document.getElementById("savedModal"),
    {}
);

function checkConnection() {
    const controller = new AbortController();

    setTimeout(() => controller.abort(), 2000);

    fetch("/status?_t=" + Date.now(), { signal: controller.signal })
        .then(() => {
            saveModal.hide();

            savedModal.show();

            setTimeout(function () {
                savedModal.hide();
            }, 3000);

            fetchSettings();
        })
        .catch((err) => {
            setTimeout(checkConnection, 0);
        });
}


// ---- GPS receiver section ----
const GPS_GNSS_BITS = [["gps.useGps", 1], ["gps.useGalileo", 2], ["gps.useBeidou", 4], ["gps.useGlonass", 8], ["gps.useQzss", 16], ["gps.useSbas", 32]];
let gpsModuleKind = -1;   // -1 unknown, 0 no answer, 1 u-blox, 2 NMEA without UBX (L76K?), 3 board does not configure the receiver

// Returns the message for an invalid constellation combination ("" when valid)
function gpsGnssProblem() {
    const on = {};
    GPS_GNSS_BITS.forEach(([id, bit]) => { on[bit] = document.getElementById(id).checked; });
    if (gpsModuleKind === 2) {      // L76K: cualquier combinacion de GPS / BeiDou / GLONASS
        return (on[1] || on[4] || on[8]) ? "" : "Enable at least one of GPS, BeiDou or GLONASS.";
    }
    if ((on[16] || on[32]) && !on[1]) return "SBAS and QZSS need GPS enabled.";
    if (on[4] && on[8]) return "BeiDou and GLONASS cannot be used together.";
    if (!(on[1] || on[2] || on[4] || on[8])) return "Enable at least one of GPS, Galileo, BeiDou or GLONASS.";
    return "";
}

function gpsCheckGnss() {
    const el = document.getElementById("gps-gnss-warning");
    if (el) el.textContent = gpsGnssProblem();
}

function fetchGpsInfo() {
    fetch("/gpsinfo?_t=" + Date.now())
        .then(r => r.json())
        .then(info => applyGpsModule(info))
        .catch(() => applyGpsModule(null));
}

function applyGpsModule(info) {
    const el = document.getElementById("gps-module-info");
    const panel = document.getElementById("gps-panel");
    const l76k = document.getElementById("gps-l76k");
    const rate = document.getElementById("gps.rateMs");
    const mark = (id, on) => { const e = document.getElementById(id); const box = e && e.closest(".form-check, .form-floating"); if (box) box.classList.toggle("opt-na", on); };
    const setBox = (id, checked, disabled) => { const e = document.getElementById(id); e.checked = checked; e.disabled = disabled; };
    const rateOptions = (isL76k) => {       // L76K: 200 ms - 1 s (max 5 Hz); u-blox M10: 500 ms - 10 s
        [...rate.options].forEach(o => { const v = parseInt(o.value, 10); o.disabled = isL76k ? v > 1000 : v < 500; });
        if (rate.selectedOptions[0] && rate.selectedOptions[0].disabled) rate.value = isL76k ? "1000" : "500";
    };
    panel.classList.remove("opt-na");
    l76k.hidden = true;
    panel.querySelectorAll(".m10-only").forEach(b => b.classList.remove("opt-na"));
    ["gps.useGalileo", "gps.useQzss", "gps.useSbas", "gps.dynModel", "gps.minElev"].forEach(id => mark(id, false));
    GPS_GNSS_BITS.forEach(([id]) => { document.getElementById(id).disabled = false; });
    const cfg = (currentSettings && currentSettings.gps) || {};
    if (!info) {
        gpsModuleKind = -1;
        rateOptions(false);
        el.innerHTML = '<span class="badge text-bg-secondary">Receiver unknown</span> <span class="text-muted">The tracker did not answer, so all options are shown.</span>';
    } else if (!info.configurable) {
        gpsModuleKind = 3;
        panel.classList.add("opt-na");
        el.innerHTML = '<span class="badge text-bg-secondary">Not configurable</span> <span class="text-muted">This board does not configure the GNSS receiver. The values are saved but not applied.</span>';
    } else if (info.module === 1) {
        gpsModuleKind = 1;
        rateOptions(false);
        el.innerHTML = '<span class="badge text-bg-success">u-blox M10</span> <span class="text-muted">Answered the UBX configuration at start-up: all options apply.</span>';
    } else if (info.module === 2) {
        gpsModuleKind = 2;
        const mask = (cfg.cnMask ?? 5) & 13;
        setBox("gps.useGps", (mask & 1) !== 0, false);
        setBox("gps.useBeidou", (mask & 4) !== 0, false);
        setBox("gps.useGlonass", (mask & 8) !== 0, false);
        setBox("gps.useGalileo", false, true);      // the L76K has no Galileo
        setBox("gps.useSbas", false, true);         // ... and no SBAS
        setBox("gps.useQzss", true, true);          // QZSS is always on
        panel.querySelectorAll(".m10-only").forEach(b => b.classList.add("opt-na"));
        ["gps.useGalileo", "gps.useQzss", "gps.useSbas", "gps.dynModel", "gps.minElev"].forEach(id => mark(id, true));
        document.getElementById("gps.nmeaLean").checked = cfg.nmeaLean ?? true;
        rateOptions(true);
        l76k.hidden = false;
        el.innerHTML = '<span class="badge text-bg-info">Quectel L76K (CASIC)</span> <span class="text-muted">It answers NMEA but not UBX, so the CASIC $PCAS commands are used: GPS / BeiDou / GLONASS in any combination, 1-5 Hz, reduced NMEA output and antenna status (not yet tested on hardware).</span>';
        const ant = document.getElementById("gps-antenna");
        const names = { 1: ['success', 'Antenna OK'], 2: ['warning', 'Antenna OPEN: no antenna detected'], 3: ['danger', 'Antenna SHORT circuit: check the antenna cable'] };
        const n = names[info.antenna];
        ant.innerHTML = n ? '<span class="badge text-bg-' + n[0] + '">' + n[1] + '</span>' : '<span class="text-muted">Antenna status: no report yet (the receiver sends it once the GPS has started).</span>';
    } else {
        gpsModuleKind = 0;
        rateOptions(false);
        el.innerHTML = '<span class="badge text-bg-secondary">No answer</span> <span class="text-muted">The receiver did not answer at start-up (or GPS is disabled). All options are shown.</span>';
    }
    gpsCheckGnss();
}

GPS_GNSS_BITS.forEach(([id]) => document.getElementById(id).addEventListener("change", gpsCheckGnss));

form.addEventListener("submit", async (event) => {
    event.preventDefault();

    const gpsProblem = gpsGnssProblem();
    if (gpsProblem) { showToast(gpsProblem); document.querySelector('[data-target="sec-gps"]').click(); return; }

    fetch(form.action, {
        method: form.method,
        body: new FormData(form),
    });
    saveModal.show();
    setTimeout(checkConnection, 2000);
});

let invalidFieldReported = false;

form.addEventListener("invalid", function (event) {
    const el = event.target;

    if (!invalidFieldReported) {          // only the first one: the rest would pile up toasts
        invalidFieldReported = true;
        setTimeout(() => { invalidFieldReported = false; }, 500);

        const section = el.closest(".panel-section");
        if (section) {
            const link = document.querySelector('.side-link[data-target="' + section.id + '"]');
            if (link) link.click();       // reuses the sidebar switcher
        }

        showToast("<b>" + (el.name || el.id) + "</b>: " + (el.validationMessage || "invalid value"));

        setTimeout(() => {
            el.focus();
            el.scrollIntoView({ block: "center", behavior: "smooth" });
        }, 0);
    }
}, true);

fetchSettings();

// ---- FT8 / FT4 beacon: segment note for the entered dial frequency (signal sits 1.5 kHz above the dial; FT8 50 Hz, FT4 83 Hz wide) ----
function ftSegmentNote(region, dialMHz) {
    if (!(dialMHz > 0)) return "";
    const f = dialMHz + 0.0015;
    const ok = iaruCheckFrequency(region, dialMHz * 1e6);
    if (!ok) return "OUTSIDE the amateur allocation of this IARU region.";
    if (region === 1) {
        if (f >= 432.000 && f < 432.400) return "R1 432.000-432.400: all modes, weak-signal segment (2700 Hz). Suitable.";
        if (f >= 432.400 && f < 432.490) return "R1 432.400-432.490: beacons only. Do not use for FT8/FT4.";
        if (f >= 432.490 && f < 432.510) return "R1: 432.491-432.493 EMGM and 432.500 APRS. Avoid.";
        if (f >= 433.400 && f < 433.600) return "R1 433.400-433.575: FM / digital voice. Avoid.";
        if (f >= 433.600 && f < 434.000) return "R1 433.600-434.000: all modes, digital channels 433.625-433.775. Check the national plan.";
        if (f >= 434.600 && f < 434.980) return "R1 434.600-434.975: repeater outputs. Do not use.";
        return "Check the IARU R1 and the national band plan for this segment.";
    }
    if (region === 2) {
        if (f >= 432.100 && f < 432.300) return "R2 432.100-432.300: weak signal. Suitable.";
        if (f >= 432.025 && f < 432.100) return "R2 432.025-432.100: CW / EME. Avoid.";
        if (f >= 432.300 && f < 432.420) return "R2 432.300-432.420: beacons. Do not use.";
        if (f >= 433.100 && f <= 435.000) return "R2 433.100-435.000: local option. Check the national plan.";
        return "Check the IARU R2 and the national band plan for this segment.";
    }
    if (f >= 431.900 && f < 432.240) return "R3 431.9-432.24: EME / weak signal (2700 Hz). Suitable.";
    if (f >= 432.240 && f <= 435.000) return "R3 432.24-435.0: all modes, max 25 kHz. Check the national plan.";
    return "Check the IARU R3 and the national band plan for this segment.";
}
function ftWire() {
    const upd = (p) => {
        const reg = parseInt(document.getElementById("ft." + p + "Region").value, 10) || 1;
        const raw = document.getElementById("ft." + p + "FreqMHz").value;
        const info = document.getElementById("ft." + p + "Info");
        const note = raw === "" ? (p === "ft4" ? "FT4 has no standard 70 cm dial frequency: it will not transmit until one is entered." : (p === "fst" ? "FST4W has no standard 70 cm dial frequency: it will not transmit until one is entered. Use a weak-signal segment (WSPR neighbours), never a beacon segment." : "")) : ftSegmentNote(reg, parseFloat(raw));
        info.textContent = note;
        info.className = /OUTSIDE|Do not use|Avoid|only\./.test(note) ? "text-danger fw-semibold" : "text-muted";
    };
    ["ft8", "ft4", "fst"].forEach(p => {
        document.getElementById("ft." + p + "Region").addEventListener("change", () => upd(p));
        document.getElementById("ft." + p + "FreqMHz").addEventListener("input", () => upd(p));
        upd(p);
    });
}

// ---- LR-FHSS: same frequency rule as LRFHSS::freqCheck() in the firmware (occupied width = grid + 2 kHz per side) ----
function lrfhssWire() {
    const BW = [39.06, 85.94, 136.72, 183.59, 335.94, 386.72, 722.66, 773.44, 1523.4, 1574.2];
    const f = document.getElementById("lrfhss.freqHz"), b = document.getElementById("lrfhss.bwIdx"), info = document.getElementById("lrfhss.info");
    const upd = () => {
        const hz = parseFloat(f.value), bw = BW[parseInt(b.value, 10)] || BW[0];
        const half = (bw / 2 + 2) * 1000, lo = hz - half, hi = hz + half;
        let t = "Occupies " + (lo / 1e6).toFixed(4) + " - " + (hi / 1e6).toFixed(4) + " MHz.", bad = true;
        if (!(hz > 0) || lo < 430e6 || hi > 440e6) t += " OUTSIDE 430-440 MHz: the firmware will refuse to transmit.";
        else if (hi > 432e6 && lo < 432.5e6) t += " Overlaps 432.000-432.500 MHz (weak signal / beacons): refused.";
        else if (hi > 435e6 && lo < 438e6) t += " Overlaps 435-438 MHz (satellite service): refused.";
        else if (bw > 125) t += " Wider than 125 kHz: the IARU R1 note (p) for LoRa-like data does not cover it.";
        else { t += " Within the 125 kHz data-like-LoRa limit (IARU R1, centre 434.000 MHz)."; bad = false; }
        info.textContent = t;
        info.className = bad ? "text-danger fw-semibold" : "text-muted";
    };
    f.addEventListener("input", upd); b.addEventListener("change", upd); upd();
}

// ---- Pin guide for every board: which GPIO is free, which one the board uses, and a suggested assignment ----
// state: ok = free, strap = usable with care (strapping, console UART, PSRAM), in = input only, used = taken by the board or the chip (the firmware refuses it), nohdr = not a GPIO of the chip
// The T-Beam S3 Supreme has its own table (header labels of the expansion connector). Every other board gets its list from the firmware (/api/pins),
// which builds it from the board's own pin definitions, so that every firmware shows the pins of the board it runs on.
const SUPREME_PIN_INFO = {
    2:  { hdr: "IO2 · R9",   st: "ok",    txt: "Free (ADC1: the only free analog pin; use it for the FH-2 keypad)" },
    3:  { hdr: "IO3 · R10",  st: "strap", txt: "Strapping pin (JTAG select): avoid" },
    6:  { hdr: "IO6 · R11",  st: "used",  txt: "GPS PPS" },
    21: { hdr: "IO21 · R12", st: "ok",    txt: "Free" },
    38: { hdr: "IO38 · R8",  st: "ok",    txt: "Free" },
    39: { hdr: "IO39 · R7",  st: "ok",    txt: "Free (JTAG clock, not used by the firmware)" },
    43: { hdr: "TXD / QWIIC",  st: "ok",    txt: "Free (UART1 TX, also on the QWIIC socket)" },
    44: { hdr: "RXD / QWIIC",  st: "ok",    txt: "Free (UART1 RX, also on the QWIIC socket)" },
    45: { hdr: "IO45 · R6",  st: "strap", txt: "Strapping pin: only with a pull-down or a contact to GND" },
    46: { hdr: "IO46 · R5",  st: "strap", txt: "Strapping pin: only with a pull-down or a contact to GND" },
    48: { hdr: "IO48 · R13", st: "ok",    txt: "Free" },
    17: { hdr: "SDA · R1",   st: "used",  txt: "I2C: OLED, BME280, magnetometer" },
    18: { hdr: "SCL · R2",   st: "used",  txt: "I2C: OLED, BME280, magnetometer" },
    0:  { hdr: "-",            st: "used",  txt: "BOOT button" },
    1:  { hdr: "-",            st: "used",  txt: "LoRa DIO1" },
    4:  { hdr: "-",            st: "used",  txt: "LoRa BUSY" },
    5:  { hdr: "-",            st: "used",  txt: "LoRa RESET" },
    7:  { hdr: "-",            st: "used",  txt: "GPS wake-up" },
    8:  { hdr: "-",            st: "used",  txt: "GPS TX" },
    9:  { hdr: "-",            st: "used",  txt: "GPS RX" },
    10: { hdr: "-",            st: "used",  txt: "LoRa CS" },
    11: { hdr: "-",            st: "used",  txt: "LoRa MOSI" },
    12: { hdr: "-",            st: "used",  txt: "LoRa SCLK" },
    13: { hdr: "-",            st: "used",  txt: "LoRa MISO" },
    14: { hdr: "-",            st: "used",  txt: "RTC interrupt" },
    33: { hdr: "-",            st: "used",  txt: "IMU interrupt" },
    34: { hdr: "-",            st: "used",  txt: "IMU chip select" },
    35: { hdr: "-",            st: "used",  txt: "IMU / SD MOSI" },
    36: { hdr: "-",            st: "used",  txt: "IMU / SD SCK" },
    37: { hdr: "-",            st: "used",  txt: "IMU / SD MISO" },
    40: { hdr: "-",            st: "used",  txt: "PMU interrupt" },
    41: { hdr: "-",            st: "used",  txt: "PMU / RTC I2C SCL" },
    42: { hdr: "-",            st: "used",  txt: "PMU / RTC I2C SDA" },
    47: { hdr: "-",            st: "used",  txt: "SD card chip select" }
};
for (let g = 26; g <= 32; g++) SUPREME_PIN_INFO[g] = { hdr: "-", st: "used", txt: "Flash / PSRAM" };
SUPREME_PIN_INFO[19] = { hdr: "-", st: "used", txt: "USB D-" };
SUPREME_PIN_INFO[20] = { hdr: "-", st: "used", txt: "USB D+" };

// kind: out = output, in = input with pull-up (contact to GND), adc = analog input
const PIN_ROLES_BASE = [
    ["notification.ledTxPin", "Tx LED", "out"], ["notification.ledMessagePin", "Message LED", "out"], ["notification.ledFlashlightPin", "Flash", "out"],
    ["notification.buzzerPinTone", "Buzzer tone", "out"], ["notification.buzzerPinVcc", "Buzzer Vcc", "out"],
    ["cw.keyPin", "Key tip (dit)", "in"], ["cw.dahPin", "Key ring (dah)", "in"], ["cw.keypadPin", "FH-2 keypad", "adc"]
];
const SUPREME_PIN_SUGGEST = { "notification.ledTxPin": 38, "notification.ledMessagePin": 21, "notification.ledFlashlightPin": 45, "notification.buzzerPinTone": 39, "notification.buzzerPinVcc": 48, "cw.keyPin": 44, "cw.dahPin": 46, "cw.keypadPin": 2, "nmeaUart.txPin": 43 };
let PIN_ROLES = [], PIN_INFO = null, PIN_SUGGEST = {}, PIN_ROWS = [], PIN_ADC = [1, 10], PIN_NAME = "", PIN_SUPREME = false;

function boardPinRefresh() {
    if (!PIN_INFO) return;
    const roles = {};                       // gpio -> roles that use it
    PIN_ROLES.forEach(([id, name]) => {
        const v = parseInt(document.getElementById(id).value, 10);
        if (!isNaN(v) && v >= 0) (roles[v] = roles[v] || []).push(name);
    });
    PIN_ROLES.forEach(([id, name, kind]) => {
        const hint = document.getElementById("pinhint-" + id.replace("notification.", ""));
        if (!hint) return;
        const v = parseInt(document.getElementById(id).value, 10);
        let cls = "text-muted", msg;
        if (isNaN(v) || v < 0) { msg = "Not used"; }
        else {
            const info = PIN_INFO[v];
            const dup = roles[v].length > 1 ? " &middot; also used by " + roles[v].filter(n => n !== name).join(", ") : "";
            const where = PIN_SUPREME && info ? " &middot; header " + info.hdr : "";
            if (!info)                  { cls = "text-warning"; msg = "GPIO " + v + ": not a GPIO of " + (PIN_SUPREME ? "the Supreme header" : "this chip"); }
            else if (info.st === "ok")  { cls = "text-success"; msg = "GPIO " + v + where + " &middot; " + info.txt; }
            else if (info.st === "strap"){ cls = "text-warning"; msg = "GPIO " + v + where + " &middot; " + info.txt; }
            else if (info.st === "in")  {
                if (kind === "adc") { cls = "text-success"; msg = "GPIO " + v + " &middot; input only: fine for the analog keypad"; }
                else                { cls = "text-danger";  msg = "GPIO " + v + " is input only: it cannot drive this function"; }
            }
            else                        { cls = "text-danger";  msg = "GPIO " + v + " is used by the board (" + info.txt + "): the firmware will switch this function off"; }
            if (kind === "adc" && info && info.st !== "used" && !(v >= PIN_ADC[0] && v <= PIN_ADC[1])) { cls = "text-warning"; msg += " &middot; the keypad needs an ADC1 pin (GPIO " + PIN_ADC[0] + "-" + PIN_ADC[1] + ")"; }
            if (dup) cls = "text-danger";
            msg += dup;
        }
        hint.className = "form-text pin-hint " + cls;
        hint.innerHTML = msg;
    });
    const tbody = document.querySelector("#pin-guide-table tbody");
    let html = "";
    PIN_ROWS.forEach(g => {
        const info = PIN_INFO[g];
        const badge = info.st === "ok" ? "text-bg-success" : (info.st === "strap" || info.st === "in" ? "text-bg-warning" : "text-bg-danger");
        const label = info.st === "ok" ? "free" : (info.st === "strap" ? "care" : (info.st === "in" ? "input" : "used"));
        html += "<tr><td class=\"text-nowrap\">" + g + "</td>" + (PIN_SUPREME ? "<td class=\"text-nowrap\">" + info.hdr + "</td>" : "")
              + "<td><span class=\"badge " + badge + "\">" + label + "</span>" + (info.txt === "Free" ? "" : " <small>" + info.txt + "</small>")
              + "</td><td>" + (roles[g] ? "<strong>" + roles[g].join(", ") + "</strong>" : "") + "</td></tr>";
    });
    tbody.innerHTML = html;
}

// Suggested assignment for the boards other than the Supreme: only free pins (never strapping ones); the keypad goes to an ADC1 pin
// (input-only pins are fine for it, so they are kept for it), the key contacts to pins with an internal pull-up.
function boardPinSuggest(pins, lo, hi, skip) {
    const free = [], inOnly = [];
    for (let g = lo; g <= hi; g++) {
        if (skip.indexOf(g) >= 0) continue;
        const e = pins[g];
        if (!e) free.push(g); else if (e[0] === "i") inOnly.push(g);
    }
    const sug = {};
    const adcOk = g => g >= PIN_ADC[0] && g <= PIN_ADC[1];
    let kp = inOnly.filter(adcOk).pop();
    if (kp === undefined) { const f = free.filter(adcOk); kp = f.length ? f[f.length - 1] : undefined; }
    if (kp !== undefined) { sug["cw.keypadPin"] = kp; const i = free.indexOf(kp); if (i >= 0) free.splice(i, 1); }
    const pool = free;
    ["notification.ledTxPin", "notification.ledMessagePin", "cw.keyPin", "cw.dahPin", "notification.ledFlashlightPin", "notification.buzzerPinTone", "notification.buzzerPinVcc"].forEach(id => {
        sug[id] = pool.length ? pool.shift() : -1;          // no free pin left: the function stays unassigned
    });
    return sug;
}

function boardPinInit() {
    fetch("/api/info?_t=" + Date.now())
        .then(r => r.json())
        .then(i => {
            const name = i.board || "";
            if (i.sat === false) ["grp-sat", "grp-rotor"].forEach(id => { const e = document.getElementById(id); if (e) e.classList.add("d-none"); });       // modo SAT solo en las placas que lo admiten
            if (i.radio === "sa868") {                  // T-TWR: radio FM, sin los modos del SX126x
                document.getElementById("grp-sa868").classList.remove("d-none");
                ["grp-dapnet", "grp-ft", "grp-lrfhss", "grp-freqcal"].forEach(id => { const e = document.getElementById(id); if (e) e.classList.add("d-none"); });
                if (i.sa868) { SA868_BAND = i.sa868.band || ""; if (sa868Refill) sa868Refill(); }
                if (i.sa868) document.getElementById("sa868-status").textContent = "Detected: " + (i.sa868.ok ? "Rev" + (i.sa868.rev / 10).toFixed(1) + ", " + i.sa868.band + ", " + i.sa868.fw + " firmware" : "no answer from the radio module");
            }
            const isSupreme = name.toLowerCase().indexOf("supreme") >= 0;
            const show = () => {
                PIN_NAME = name;
                PIN_SUPREME = isSupreme;
                document.getElementById("pin-guide").classList.remove("d-none");
                document.getElementById("pin-guide-title").textContent = isSupreme ? "T-Beam S3 Supreme: expansion header pins" : "Board pins: " + (name || "this board");
                document.querySelectorAll(".pin-guide-supreme").forEach(e => e.classList.toggle("d-none", !isSupreme));
                document.querySelectorAll(".pin-guide-generic").forEach(e => e.classList.toggle("d-none", isSupreme));
                PIN_ROLES.forEach(([id]) => document.getElementById(id).addEventListener("input", boardPinRefresh));
                document.getElementById("pin-guide-suggest").addEventListener("click", () => {
                    Object.keys(PIN_SUGGEST).forEach(id => { document.getElementById(id).value = PIN_SUGGEST[id]; });
                    boardPinRefresh();
                });
                const sg = Object.keys(PIN_SUGGEST).map(id => (PIN_ROLES.filter(r => r[0] === id)[0] || [id, id])[1] + " " + (PIN_SUGGEST[id] < 0 ? "-" : PIN_SUGGEST[id])).join(", ");
                document.getElementById("pin-guide-suggest").textContent = "Use the suggested pins (" + sg + ")";
                boardPinRefresh();
            };
            if (isSupreme) {
                PIN_ROLES = PIN_ROLES_BASE.concat([["nmeaUart.txPin", "NMEA TX", "out"]]);
                PIN_INFO = SUPREME_PIN_INFO;
                PIN_SUGGEST = SUPREME_PIN_SUGGEST;
                PIN_ROWS = [2, 3, 6, 21, 38, 39, 43, 44, 45, 46, 48, 17, 18];
                PIN_ADC = [1, 10];
                document.getElementById("grp-nmea").classList.remove("d-none");
                show();
                return;
            }
            return fetch("/api/pins?_t=" + Date.now()).then(r => r.json()).then(d => {
                const info = {}, rows = [];
                const skip = d.skip || [];
                for (let g = d.lo; g <= d.hi; g++) {
                    if (skip.indexOf(g) >= 0) continue;
                    const e = d.pins[g];
                    const st = !e ? "ok" : (e[0] === "u" ? "used" : (e[0] === "i" ? "in" : "strap"));
                    info[g] = { hdr: "-", st: st, txt: e ? e[1] : "Free" };
                    rows.push(g);
                }
                PIN_ROLES = PIN_ROLES_BASE;
                PIN_INFO = info;
                PIN_ROWS = rows;
                PIN_ADC = d.adc1 || [1, 10];
                PIN_SUGGEST = boardPinSuggest(d.pins, d.lo, d.hi, skip);
                show();
            });
        })
        .catch(() => {});
}
window.addEventListener("load", () => setTimeout(boardPinInit, 300));
