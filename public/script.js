import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js";
import { getAuth, signInAnonymously, onAuthStateChanged } from "https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js";
import {
  getDatabase,
  ref,
  onValue,
  set
} from "https://www.gstatic.com/firebasejs/10.12.0/firebase-database.js";
import { firebaseConfig, DEVICE_ID } from "./firebase-config.js";

const app = initializeApp(firebaseConfig);
const auth = getAuth(app);
const db = getDatabase(app);

const stateRef = ref(db, `devices/${DEVICE_ID}/state`);
const commandRef = ref(db, `devices/${DEVICE_ID}/command`);
const onlineRef = ref(db, `devices/${DEVICE_ID}/online`);
const lastSeenRef = ref(db, `devices/${DEVICE_ID}/lastSeen`);
const logRef = ref(db, `devices/${DEVICE_ID}/log`);

let states = [false, false, false, false];
let authReady = false;

window.sendCmd = sendCmd;
window.toggle = toggle;

function sendCmd(cmd) {
  if (!authReady) {
    addLog("Not signed in to Firebase");
    return;
  }
  set(commandRef, cmd).catch((err) => addLog("Send failed: " + err.message));
}

function toggle(n) {
  const isOn = states[n - 1];
  const cmd = isOn ? String(n * 2) : String((n * 2) - 1);
  sendCmd(cmd);
}

function updateCard(n, isOn) {
  const card = document.getElementById("card-" + n);
  const badge = document.getElementById("badge-" + n);
  const btn = card.querySelector(".toggle-btn");
  if (isOn) {
    card.classList.add("on");
    badge.innerHTML = "&#9679; ACTIVE";
    btn.textContent = "Turn OFF";
  } else {
    card.classList.remove("on");
    badge.innerHTML = "&#9679; INACTIVE";
    btn.textContent = "Turn ON";
  }
}

function applyStateString(bits) {
  if (!bits || bits.length < 4) return;
  for (let i = 0; i < 4; i++) {
    states[i] = bits[i] === "1";
    updateCard(i + 1, states[i]);
  }
}

function setStatus(connected) {
  const el = document.getElementById("conn-status");
  if (connected) {
    el.innerHTML = "&#9679; DEVICE ONLINE";
    el.classList.add("connected");
  } else {
    el.innerHTML = "&#9679; DEVICE OFFLINE";
    el.classList.remove("connected");
  }
}

function addLog(msg) {
  const el = document.getElementById("log-output");
  const now = new Date().toLocaleTimeString("en-GB", { hour12: false });
  const div = document.createElement("div");
  div.className = "log-line new";
  div.textContent = "[" + now + "]  " + msg;
  el.prepend(div);
  setTimeout(() => div.classList.remove("new"), 1200);
  while (el.children.length > 30) el.removeChild(el.lastChild);
}

function checkDeviceOnline(data) {
  const online = data && data.online === true;
  const lastSeen = data && data.lastSeen;
  if (!online || !lastSeen) {
    setStatus(false);
    return;
  }
  const ageMs = Date.now() - lastSeen;
  setStatus(ageMs < 45000);
}

onAuthStateChanged(auth, (user) => {
  if (user) {
    authReady = true;
    addLog("Signed in to Firebase (cloud)");
    document.getElementById("conn-status").innerHTML = "&#9679; CLOUD CONNECTED";
    document.getElementById("conn-status").classList.add("connected");
  } else {
    authReady = false;
    addLog("Signing in...");
    signInAnonymously(auth).catch((err) => addLog("Auth error: " + err.message));
  }
});

onValue(stateRef, (snap) => {
  const val = snap.val();
  if (typeof val === "string") applyStateString(val);
});

onValue(ref(db, `devices/${DEVICE_ID}`), (snap) => {
  checkDeviceOnline(snap.val());
});

onValue(logRef, (snap) => {
  const val = snap.val();
  if (val) addLog(val);
});

addLog("Panel loaded — connecting to cloud...");
