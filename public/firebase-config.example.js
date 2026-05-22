// Copy this file to firebase-config.js and fill in values from Firebase Console:
// Project Settings → General → Your apps → Web app config

export const firebaseConfig = {
  apiKey: "YOUR_API_KEY",
  authDomain: "esp32-and-relay-cloud-project.firebaseapp.com",
  databaseURL: "https://esp32-and-relay-cloud-project-default-rtdb.firebaseio.com",
  projectId: "esp32-and-relay-cloud-project",
  storageBucket: "esp32-and-relay-cloud-project.firebasestorage.app",
  messagingSenderId: "YOUR_SENDER_ID",
  appId: "YOUR_APP_ID"
};

// Must match DEVICE_ID in include/secrets.h on the ESP32
export const DEVICE_ID = "esp32relay";
