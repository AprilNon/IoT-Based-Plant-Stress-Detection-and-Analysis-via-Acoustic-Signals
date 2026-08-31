// ============================================================
//  Plant Monitor — Google Apps Script Backend  v4
//  _config structure:
//    record_flag       | 0 หรือ 1
//    reset_flag        | 0 หรือ 1
//    session_active    | 0 หรือ 1
//    session_end       | ISO string เวลาหยุด
//    session_sheet     | ชื่อ sheet ที่กำลังอัด
//    time_slots        | JSON array
//    last_slot_trigger | yyyy-MM-dd_HH:mm
// ============================================================

const SHEET_URL   = "..."; // SHEET_URL
const CONFIG_SHEET = "_config"; // sheetName
const HEADERS      = ["timestamp","temp","hum","sd","record","filename","amplitude","rms","db"];

// ── Helpers ───────────────────────────────────────────────────
function getSS() { return SpreadsheetApp.openByUrl(SHEET_URL); }

function dateToSheetName(date) {
  const d = date || new Date();
  const dd   = String(d.getDate()).padStart(2,'0');
  const mm   = String(d.getMonth()+1).padStart(2,'0');
  const yyyy = d.getFullYear() + 543;
  return "RAW_" + dd + "-" + mm + "-" + yyyy;
}

function getOrCreateSheet(name) {
  const ss = getSS();
  let s = ss.getSheetByName(name);
  if (!s) { s = ss.insertSheet(name); ensureHeader(s); }
  return s;
}

function getConfigSheet() {
  const ss = getSS();
  let s = ss.getSheetByName(CONFIG_SHEET);
  if (!s) { s = ss.insertSheet(CONFIG_SHEET); }
  return s;
}

function ensureHeader(sheet) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADERS);
    sheet.getRange(1,1,1,HEADERS.length).setFontWeight("bold");
  }
}

function toObj(r) {
  return {
    timestamp: r[0] ? new Date(r[0]).toISOString() : "",
    temp: r[1], hum: r[2], sd: r[3], record: r[4],
    filename: r[5], amplitude: r[6], rms: r[7], db: r[8]
  };
}

function json200(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function include(filename) {
  return HtmlService.createHtmlOutputFromFile(filename).getContent();
}

// ── Config key-value store ────────────────────────────────────
function configGet(key) {
  const data = getConfigSheet().getDataRange().getValues();
  for (let i = 0; i < data.length; i++) {
    if (!data[i][0] || typeof data[i][0] !== 'string') continue;
    if (data[i][0].trim() === key) return data[i][1];
  }
  return null;
}

function configSet(key, value) {
  const s    = getConfigSheet();
  const data = s.getDataRange().getValues();
  for (let i = 0; i < data.length; i++) {
    if (!data[i][0] || typeof data[i][0] !== 'string') continue;
    if (data[i][0].trim() === key) { s.getRange(i+1,2).setValue(value); return; }
  }
  s.appendRow([key, value]);
}

// ── Session helpers ───────────────────────────────────────────
// เริ่ม session: บันทึก active=1, end time, sheet name
function startSession(durationHours, sheetName) {
  const endTime = new Date(Date.now() + durationHours * 3600000);
  const today   = Utilities.formatDate(new Date(), "Asia/Bangkok", "dd-MM-yyyy");
  const lastDay = String(configGet("current_day") || "");

  // นับ session num เฉพาะตอนวันใหม่ หรือวันเดิมแต่ session num ยังไม่เคย set
  let sessNum = 1;
  if (lastDay === today) {
    sessNum = parseInt(String(configGet("current_session_num") || "0")) + 1;
  }

  configSet("session_active",      "1");
  configSet("session_end",         endTime.toISOString());
  configSet("session_sheet",       sheetName);
  configSet("current_day",         today);
  configSet("current_session_num", String(sessNum));
  configSet("last_slot_trigger",   String(configGet("last_slot_trigger") || "")); // ไม่เคลียร์
}

// หยุด session — เคลียร์ทุก flag ที่เกี่ยวข้อง
function stopSession() {
  configSet("session_active",    "0");
  configSet("session_end",       "");
  configSet("record_flag",       "0");
  configSet("last_slot_trigger", "");  // ← เคลียร์ด้วย ให้ slot trigger ได้ใหม่
  configSet("session_sheet",     "");
}

// เช็คว่า session ยังทำงานอยู่ไหม
function isSessionActive() {
  if (String(configGet("session_active")) !== "1") return false;
  const endStr = configGet("session_end");
  if (!endStr) return false;
  const stillActive = new Date() < new Date(endStr);
  if (!stillActive) {
    configSet("session_active", "0");
    configSet("session_end",    "");
    // ไม่เคลียร์ last_slot_trigger — checkTimeSlots จัดการเอง
  }
  return stillActive;
}

// ── รับ POST จาก ESP32 ───────────────────────────────────────
function doPost(e) {
  try {
    const d         = JSON.parse(e.postData.contents);
    const now       = new Date();
    // ใช้ sheet ที่ session กำหนด หรือ sheet วันนี้
    const sheetName = (isSessionActive() ? configGet("session_sheet") : null) || d.sheet || dateToSheetName(now);
    const sheet     = getOrCreateSheet(sheetName);
    ensureHeader(sheet);
    const fname = d.filename || ("plant_sounds_" + now.getTime() + ".wav");
    sheet.appendRow([now, d.temp||"", d.hum||"", d.sd||"", d.record||"",
                     fname, d.amplitude||0, d.rms||0, d.db||0]);
    return json200({ status: "ok", sheet: sheetName });
  } catch(err) {
    return json200({ status: "error", message: err.toString() });
  }
}

// ── GET handler ───────────────────────────────────────────────
function doGet(e) {
  const action = (e && e.parameter && e.parameter.action) || "dashboard";

  // ── poll ─────────────────────────────────────────────────────
  if (action === "poll") {
    const recordFlag = configGet("record_flag");
    const resetFlag  = configGet("reset_flag");
    const recOn = (String(recordFlag) === "1");
    const rstOn = (String(resetFlag)  === "1");
    if (recOn) configSet("record_flag", "0");
    if (rstOn) configSet("reset_flag",  "0");

    const sessActive = isSessionActive();
    const sessSheet  = configGet("session_sheet") || dateToSheetName(new Date());
    const slotTrigger = checkTimeSlots();
    const shouldRecord = recOn || sessActive || slotTrigger;

    // slot trigger → startSession ให้ครอบคลุมถึงเวลาสิ้นสุด slot
    if (slotTrigger && !sessActive) {
      // หาเวลาสิ้นสุดของ slot ที่ match
      let endHour = 2; // default 2 ชั่วโมง
      try {
        const slots = JSON.parse(configGet("time_slots") || "[]");
        const now   = new Date();
        const hhmm  = String(now.getHours()).padStart(2,'0')+":"+String(now.getMinutes()).padStart(2,'0');
        for (let i = 0; i < slots.length; i++) {
          if (hhmm >= slots[i].start && hhmm < slots[i].end) {
            const [eh, em] = slots[i].end.split(":").map(Number);
            const [sh, sm] = slots[i].start.split(":").map(Number);
            endHour = ((eh*60+em) - (sh*60+sm)) / 60;
            break;
          }
        }
      } catch(e) {}
      startSession(endHour, dateToSheetName(new Date()));
    }

    // record flag → startSession ใหม่ (2 ชม. ปกติ, 24 ชม. ถ้าเป็น manual จากบอร์ด)
    if (recOn && !sessActive) {
      const isManual = String(configGet("pending_manual")) === "1";
      startSession(isManual ? 24 : 2, dateToSheetName(new Date()));
      configSet("pending_manual", "0");
   }

    // บันทึก last_seen ทุกครั้งที่ ESP32 poll
    configSet("last_seen", new Date().toISOString());

    return json200({
      status             : "ok",
      record             : shouldRecord,
      reset              : rstOn,
      sheet              : configGet("session_sheet") || sessSheet,
      session_active     : isSessionActive(),
      session_end        : configGet("session_end") || "",
      current_day        : configGet("current_day") || "",
      current_session_num: configGet("current_session_num") || "1"
    });
  }

  // ── getStatus (dashboard เช็ค WiFi + Recording) ──────────────
  if (action === "getStatus") {
    const lastSeen    = configGet("last_seen") || "";
    const sessActive  = isSessionActive();
    let   wifiOnline  = false;
    if (lastSeen !== "") {
      const diff = (new Date() - new Date(lastSeen)) / 1000;
      wifiOnline = diff < 25;   
    }
    return json200({
      status      : "ok",
      wifi_online : wifiOnline,
      recording   : sessActive,
      last_seen   : lastSeen
    });
  }
  if (action === "triggerRecord") {
  configSet("record_flag", "1");
  configSet("pending_manual", (e.parameter && e.parameter.manual === "1") ? "1" : "0");
  return json200({ status: "ok" });
  }
 
  // ── stopSession (กดปุ่มหยุดจาก dashboard) ───────────────────
  if (action === "stopSession") {
    stopSession();
    return json200({ status: "ok", message: "session stopped" });
  }

  // ── resetBoard ───────────────────────────────────────────────
  if (action === "resetBoard") {
    configSet("reset_flag", "1");
    return json200({ status: "ok" });
  }

  // ── getConfig (dashboard โหลดสถานะ) ─────────────────────────
  if (action === "getConfig") {
    const endStr = configGet("session_end") || "";
    return json200({
      status         : "ok",
      session_active : String(configGet("session_active")) === "1",
      session_end    : endStr,
      session_sheet  : configGet("session_sheet") || "",
      slots          : (() => { try { return JSON.parse(configGet("time_slots")||"[]"); } catch(e){ return []; } })()
    });
  }

  // ── setSlots ─────────────────────────────────────────────────
  if (action === "setSlots") {
    const slots = e.parameter.slots || "[]";
    configSet("time_slots", slots);
    return json200({ status: "ok", slots: JSON.parse(slots) });
  }

  // ── latest / history ─────────────────────────────────────────
  if (action === "latest") {
    const sh = (e.parameter && e.parameter.sheet) || dateToSheetName(new Date());
    return json200(getLatestData(sh));
  }
  if (action === "history") {
    const sh = (e.parameter && e.parameter.sheet) || dateToSheetName(new Date());
    return json200(getHistoryData(parseInt((e.parameter && e.parameter.rows)||"1000"), sh));
  }

  // ── sheetList ─────────────────────────────────────────────────
  if (action === "sheetList") {
    const sheets = getSS().getSheets().map(s => s.getName())
      .filter(n => n.startsWith("RAW_") || (!n.startsWith("_") && n !== "sheet1" && n !== "sheet2"));
    return json200({ status: "ok", sheets: sheets });
  }

  // ── dashboard ─────────────────────────────────────────────────
  return HtmlService.createTemplateFromFile("dashboard").evaluate()
    .setTitle("Plant Monitor")
    .addMetaTag("viewport","width=device-width,initial-scale=1")
    .setXFrameOptionsMode(HtmlService.XFrameOptionsMode.ALLOWALL);
}

// ── checkTimeSlots ────────────────────────────────────────────
function checkTimeSlots() {
  const raw = configGet("time_slots");
  if (!raw) return false;
  let slots;
  try { slots = JSON.parse(raw); } catch(e) { return false; }

  const now   = new Date();
  const hhmm  = String(now.getHours()).padStart(2,'0') + ":" + String(now.getMinutes()).padStart(2,'0');
  const today = Utilities.formatDate(now, "Asia/Bangkok", "yyyy-MM-dd");
  const last  = String(configGet("last_slot_trigger") || "");

  for (let i = 0; i < slots.length; i++) {
    const s = slots[i];
    if (hhmm >= s.start && hhmm < s.end) {
      const key     = today + "_" + s.start;
      const sessOn  = isSessionActive();

      if (last === key && sessOn) {
        // session ทำงานอยู่ → ไม่ trigger ซ้ำ
        return false;
      }
      if (last === key && !sessOn) {
        // session หมดแล้ว แต่ยังอยู่ในช่วง slot
        // ใช้ key พิเศษกัน trigger ซ้ำทุก 10 วิ
        const retryKey = key + "_done";
        if (String(configGet("last_slot_trigger")) === retryKey) return false;
        configSet("last_slot_trigger", retryKey);
        return true;
      }
      // key ใหม่ → trigger ได้
      configSet("last_slot_trigger", key);
      return true;
    }
  }
  return false;
}

// ── Client-callable (google.script.run) ───────────────────────
function setRecordFlag() {
  try { configSet("record_flag","1"); return { status:"ok" }; }
  catch(e) { return { status:"error", message:e.toString() }; }
}

function setResetFlag() {
  try { configSet("reset_flag","1"); return { status:"ok" }; }
  catch(e) { return { status:"error", message:e.toString() }; }
}

function stopSessionClient() {
  try { stopSession(); return { status:"ok" }; }
  catch(e) { return { status:"error", message:e.toString() }; }
}

function getConfig() {
  try {
    const endStr  = configGet("session_end") || "";
    const sessAct = isSessionActive();
    const lastSeen= configGet("last_seen") || "";
    const wifiOn  = lastSeen !== "" && (new Date() - new Date(lastSeen)) / 1000 < 30;
    return {
      status        : "ok",
      sessionActive : sessAct,
      sessionEnd    : endStr,
      sessionSheet  : configGet("session_sheet") || "",
      slots         : (() => { try { return JSON.parse(configGet("time_slots")||"[]"); } catch(e){ return []; } })(),
      wifiOnline    : wifiOn,
      recording     : sessAct,
      lastSeen      : lastSeen
    };
  } catch(e) { return { status:"error" }; }
}

function setSlots(slotsJson) {
  try { configSet("time_slots", slotsJson); return { status:"ok", slots: JSON.parse(slotsJson) }; }
  catch(e) { return { status:"error", message:e.toString() }; }
}

function getSheetList() {
  try {
    const sheets = getSS().getSheets().map(s => s.getName())
      .filter(n => n.startsWith("RAW_") || (!n.startsWith("_") && n !== "sheet1" && n !== "sheet2"));
    return { status:"ok", sheets:sheets };
  } catch(e) { return { status:"error", message:e.toString() }; }
}

function createNewSheet(name) {
  try {
    if (!name || !name.trim()) return { status:"error", message:"ชื่อว่าง" };
    name = name.trim();
    const ss = getSS();
    if (ss.getSheetByName(name)) return { status:"error", message:"มีชีตนี้แล้ว" };
    const s = ss.insertSheet(name);
    ensureHeader(s);
    return { status:"ok", sheet:name };
  } catch(e) { return { status:"error", message:e.toString() }; }
}

function getLatestData(sheetName) {
  const s = getSS().getSheetByName(sheetName);
  if (!s) return { status:"error", message:"Sheet not found: "+sheetName };
  const row = s.getLastRow();
  if (row <= 1) return { status:"empty" };
  return { status:"ok", data: toObj(s.getRange(row,1,1,9).getValues()[0]) };
}

function getHistoryData(n, sheetName) {
  const s = getSS().getSheetByName(sheetName);
  if (!s) return { status:"error", rows:[] };
  const row = s.getLastRow();
  if (row <= 1) return { status:"empty", rows:[] };
  const start = Math.max(2, row-(n||1000)+1);
  const vals  = s.getRange(start,1,row-start+1,9).getValues();
  return { status:"ok", rows: vals.map(toObj) };
}