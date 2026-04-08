/**
 * PebBeep Crypto Module
 * Wraps TweetNaCl for NaCl box encryption/decryption.
 *
 * In production, tweetnacl.min.js (7KB) would be bundled here.
 * For now, we implement the interface that app.js and message-handler.js expect.
 *
 * Key storage:
 *   - Own keypair: localStorage 'pebbeep_secret_key' + 'pebbeep_public_key'
 *   - Peer (desktop) public key: localStorage 'pebbeep_peer_public_key'
 */

// TweetNaCl.js — 32KB fast variant, runs on phone via PebbleKit JS
var nacl = require('./lib/tweetnacl');

var mySecretKey = null;
var myPublicKey = null;
var peerPublicKey = null;

/**
 * Base64 encode/decode helpers
 */
function base64ToBytes(base64) {
  var raw = atob(base64);
  var bytes = new Uint8Array(raw.length);
  for (var i = 0; i < raw.length; i++) {
    bytes[i] = raw.charCodeAt(i);
  }
  return bytes;
}

function bytesToBase64(bytes) {
  var raw = '';
  for (var i = 0; i < bytes.length; i++) {
    raw += String.fromCharCode(bytes[i]);
  }
  return btoa(raw);
}

function stringToBytes(str) {
  var bytes = [];
  for (var i = 0; i < str.length; i++) {
    var c = str.charCodeAt(i);
    if (c < 128) {
      bytes.push(c);
    } else if (c < 2048) {
      bytes.push(0xc0 | (c >> 6));
      bytes.push(0x80 | (c & 0x3f));
    } else {
      bytes.push(0xe0 | (c >> 12));
      bytes.push(0x80 | ((c >> 6) & 0x3f));
      bytes.push(0x80 | (c & 0x3f));
    }
  }
  return new Uint8Array(bytes);
}

function bytesToString(bytes) {
  var str = '';
  var i = 0;
  while (i < bytes.length) {
    var c = bytes[i];
    if (c < 128) {
      str += String.fromCharCode(c);
      i++;
    } else if (c < 224) {
      str += String.fromCharCode(((c & 0x1f) << 6) | (bytes[i+1] & 0x3f));
      i += 2;
    } else {
      str += String.fromCharCode(((c & 0x0f) << 12) | ((bytes[i+1] & 0x3f) << 6) | (bytes[i+2] & 0x3f));
      i += 3;
    }
  }
  return str;
}

/**
 * Initialize crypto keys.
 * Generate keypair on first run, load peer key from config.
 */
function init() {
  if (!nacl) {
    console.log('PebBeep: Crypto not available');
    return;
  }

  // Load our keypair (saved by config page)
  var storedSk = localStorage.getItem('pebbeep_pebble_secret_key') || localStorage.getItem('pebbeep_secret_key');
  var storedPk = localStorage.getItem('pebbeep_pebble_public_key') || localStorage.getItem('pebbeep_public_key');

  if (storedSk && storedPk) {
    mySecretKey = base64ToBytes(storedSk);
    myPublicKey = base64ToBytes(storedPk);
    console.log('PebBeep: Loaded Pebble keypair (pk=' + storedPk.substring(0, 16) + '...)');
  } else {
    var kp = nacl.box.keyPair();
    mySecretKey = kp.secretKey;
    myPublicKey = kp.publicKey;
    localStorage.setItem('pebbeep_pebble_secret_key', bytesToBase64(mySecretKey));
    localStorage.setItem('pebbeep_pebble_public_key', bytesToBase64(myPublicKey));
    console.log('PebBeep: Generated new keypair');
  }

  // Load peer (desktop) public key
  var storedPeerPk = localStorage.getItem('pebbeep_peer_public_key');
  if (storedPeerPk) {
    peerPublicKey = base64ToBytes(storedPeerPk);
    console.log('PebBeep: Loaded desktop public key (pk=' + storedPeerPk.substring(0, 16) + '...)');
  } else {
    console.log('PebBeep: No desktop public key — cannot decrypt');
  }

  console.log('PebBeep: Crypto initialized, paired=' + (peerPublicKey !== null));
}

/**
 * Encrypt a plaintext string.
 * Returns { nonce: base64, ciphertext: base64 } or null.
 */
function encrypt(plaintext) {
  if (!nacl || !mySecretKey || !peerPublicKey) return null;

  var messageBytes = stringToBytes(plaintext);
  var nonce = nacl.randomBytes(nacl.box.nonceLength);
  var box = nacl.box(messageBytes, nonce, peerPublicKey, mySecretKey);

  if (!box) return null;

  return {
    nonce: bytesToBase64(nonce),
    ciphertext: bytesToBase64(box)
  };
}

/**
 * Decrypt a message.
 * Takes base64 nonce and ciphertext, returns plaintext string or null.
 */
function decrypt(nonceB64, ciphertextB64) {
  if (!nacl || !mySecretKey || !peerPublicKey) return null;

  var nonce = base64ToBytes(nonceB64);
  var ciphertext = base64ToBytes(ciphertextB64);
  var opened = nacl.box.open(ciphertext, nonce, peerPublicKey, mySecretKey);

  if (!opened) return null;

  return bytesToString(opened);
}

/**
 * Get our public key as base64 (for pairing).
 */
function getPublicKey() {
  return myPublicKey ? bytesToBase64(myPublicKey) : null;
}

module.exports = {
  init: init,
  encrypt: encrypt,
  decrypt: decrypt,
  getPublicKey: getPublicKey
};
