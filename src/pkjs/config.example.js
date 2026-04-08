// PebBeep configuration — copy this file to config.js and fill in your
// own values before building. config.js is gitignored and MUST NOT be
// committed to source control.
//
// To set up your own backend:
//   1. Create a Firebase project (https://console.firebase.google.com)
//   2. Deploy the PebBeep relay Cloud Function to your project
//   3. Host the config page (config.html) on Firebase Hosting or similar
//   4. Copy this file to config.js and fill in the values below
//
// The Firebase Web API key is not a secret in the traditional sense
// (it's embedded in every Firebase web client), but it identifies YOUR
// project and should not be shared if you want to keep this repo a
// clean template others can fork.

var PebBeepConfig = {
  // URL of the hosted config/sign-in page
  CONFIG_URL: 'https://YOUR-PROJECT.web.app/config.html',

  // URL of your deployed relay Cloud Function
  RELAY_URL: 'https://us-central1-YOUR-PROJECT.cloudfunctions.net/api',

  // Firebase Web API key (from Firebase console → Project settings → Web app)
  FIREBASE_API_KEY: 'YOUR_FIREBASE_WEB_API_KEY',
};
