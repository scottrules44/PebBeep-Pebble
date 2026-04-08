/**
 * PebBeep — PebbleKit JS Companion
 * Handles config page, relay communication, and message relay to watch.
 */

// Polyfill crypto.getRandomValues BEFORE loading TweetNaCl
// PebbleKit JS has no crypto global — TweetNaCl needs it for PRNG
(function() {
  var fakeRandom = function(arr) {
    for (var i = 0; i < arr.length; i++) arr[i] = Math.floor(Math.random() * 256);
    return arr;
  };
  var c = { getRandomValues: fakeRandom };
  if (typeof self !== 'undefined') self.crypto = c;
  if (typeof window !== 'undefined') window.crypto = c;
  if (typeof global !== 'undefined') global.crypto = c;
  // Also try to set on this scope
  try { crypto = c; } catch(e) {}
})();

var nacl = null;
try {
  nacl = require('tweetnacl');
  console.log('PebBeep: TweetNaCl loaded (npm)');
} catch(e1) {
  try {
    nacl = require('./lib/tweetnacl');
    console.log('PebBeep: TweetNaCl loaded (local)');
  } catch(e2) {
    console.log('PebBeep: TweetNaCl not available: ' + e1 + ' / ' + e2);
  }
}

// Force-set PRNG if TweetNaCl loaded but PRNG wasn't detected
if (nacl && nacl.setPRNG) {
  nacl.setPRNG(function(x, n) {
    for (var i = 0; i < n; i++) x[i] = Math.floor(Math.random() * 256);
  });
  console.log('PebBeep: PRNG force-set via setPRNG');
} else if (nacl) {
  // nacl-fast doesn't expose setPRNG — override randomBytes directly
  var origRandomBytes = nacl.randomBytes;
  nacl.randomBytes = function(n) {
    try {
      return origRandomBytes(n);
    } catch(e) {
      // Fallback: generate random bytes manually
      var arr = new Uint8Array(n);
      for (var i = 0; i < n; i++) arr[i] = Math.floor(Math.random() * 256);
      return arr;
    }
  };
  console.log('PebBeep: PRNG fallback via randomBytes override');
}

// Load per-deployment config (see config.example.js).
// config.js is gitignored — copy config.example.js to config.js and fill in
// your own Firebase project values before building.
try {
  require('./config.js');
} catch (e) {
  console.error('PebBeep: config.js not found. Copy config.example.js to ' +
    'config.js and fill in your Firebase project values.');
  // Fall back to example so the build doesn't fail outright
  require('./config.example.js');
}
var CONFIG_URL = PebBeepConfig.CONFIG_URL;
var RELAY_URL = PebBeepConfig.RELAY_URL;

// AppMessage keys
var KEY_COMMAND      = 0;
var KEY_CHAT_ID      = 1;
var KEY_CHAT_NAME    = 2;
var KEY_CHAT_PREVIEW = 3;
var KEY_MSG_TEXT     = 4;
var KEY_MSG_SENDER   = 5;
var KEY_MSG_TIME     = 6;
var KEY_REPLY_TEXT   = 7;
var KEY_STATUS       = 8;
var KEY_INDEX        = 9;
var KEY_TOTAL        = 10;
var KEY_CUSTOM_REPLY_1       = 11;
var KEY_CUSTOM_REPLY_2       = 12;
var KEY_CUSTOM_REPLY_3       = 13;
var KEY_CUSTOM_REPLY_4       = 14;
var KEY_VIBRATE_ON_MESSAGE   = 15;
var KEY_SHOW_NOTIFICATION    = 16;
var KEY_POLL_INTERVAL        = 17;
var KEY_SERVICE              = 18;

var pollTimer = null;
var POLL_INTERVAL = 30000;
var lastPollTimestamp = 0;

// Map display name → real Beeper chatId (for replies)
var chatIdMap = {};

// --- Inline Crypto (no nested require) ---
var mySecretKey = null;
var myPublicKey = null;
var peerPublicKey = null;

var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

function b64ToBytes(b64) {
  b64 = b64.replace(/[^A-Za-z0-9+/]/g, '');
  var len = (b64.length * 3) >> 2;
  var bytes = new Uint8Array(len);
  var p = 0;
  for (var i = 0; i < b64.length; i += 4) {
    var a = B64.indexOf(b64[i]), b = B64.indexOf(b64[i+1]);
    var c = B64.indexOf(b64[i+2]||'A'), d = B64.indexOf(b64[i+3]||'A');
    var bits = (a<<18) | (b<<12) | (c<<6) | d;
    if (p < len) bytes[p++] = (bits>>16) & 0xff;
    if (p < len) bytes[p++] = (bits>>8) & 0xff;
    if (p < len) bytes[p++] = bits & 0xff;
  }
  return bytes;
}

function bytesToB64(bytes) {
  var r = '', i = 0;
  while (i < bytes.length) {
    var a = bytes[i++] || 0, b = bytes[i++] || 0, c = bytes[i++] || 0;
    var bits = (a<<16) | (b<<8) | c;
    r += B64[(bits>>18)&63] + B64[(bits>>12)&63] + B64[(bits>>6)&63] + B64[bits&63];
  }
  var pad = bytes.length % 3;
  if (pad === 1) r = r.slice(0,-2) + '==';
  else if (pad === 2) r = r.slice(0,-1) + '=';
  return r;
}

function strToBytes(str) {
  var b = [];
  for (var i = 0; i < str.length; i++) {
    var c = str.charCodeAt(i);
    if (c < 128) b.push(c);
    else if (c < 2048) { b.push(0xc0|(c>>6)); b.push(0x80|(c&0x3f)); }
    else { b.push(0xe0|(c>>12)); b.push(0x80|((c>>6)&0x3f)); b.push(0x80|(c&0x3f)); }
  }
  return new Uint8Array(b);
}

function bytesToStr(bytes) {
  var s = '', i = 0;
  while (i < bytes.length) {
    var c = bytes[i];
    if (c < 128) { s += String.fromCharCode(c); i++; }
    else if (c < 224) { s += String.fromCharCode(((c&0x1f)<<6)|(bytes[i+1]&0x3f)); i+=2; }
    else { s += String.fromCharCode(((c&0x0f)<<12)|((bytes[i+1]&0x3f)<<6)|(bytes[i+2]&0x3f)); i+=3; }
  }
  return s;
}

var crypto = {
  init: function() {
    if (!nacl) { console.log('PebBeep: NaCl not available'); return; }
    var sk = localStorage.getItem('pebbeep_pebble_secret_key');
    var pk = localStorage.getItem('pebbeep_pebble_public_key');
    if (sk && pk) {
      mySecretKey = b64ToBytes(sk);
      myPublicKey = b64ToBytes(pk);
      console.log('PebBeep: Loaded Pebble keypair');
    } else {
      // Generate keypair ONCE and save permanently
      try {
        var kp = nacl.box.keyPair();
        mySecretKey = kp.secretKey;
        myPublicKey = kp.publicKey;
        localStorage.setItem('pebbeep_pebble_secret_key', bytesToB64(mySecretKey));
        localStorage.setItem('pebbeep_pebble_public_key', bytesToB64(myPublicKey));
        console.log('PebBeep: Generated and saved new Pebble keypair');
      } catch(e) {
        console.log('PebBeep: Failed to generate keypair: ' + e);
      }
    }
    var peer = localStorage.getItem('pebbeep_peer_public_key');
    if (peer) {
      peerPublicKey = b64ToBytes(peer);
      console.log('PebBeep: Loaded desktop public key');
    } else {
      console.log('PebBeep: No desktop public key');
    }
    console.log('PebBeep: Crypto ready, paired=' + (peerPublicKey !== null && mySecretKey !== null));
  },
  decrypt: function(nonceB64, ciphertextB64) {
    if (!nacl || !mySecretKey || !peerPublicKey) return null;
    try {
      var nonce = b64ToBytes(nonceB64);
      var ct = b64ToBytes(ciphertextB64);
      var opened = nacl.box.open(ct, nonce, peerPublicKey, mySecretKey);
      if (!opened) return null;
      return bytesToStr(opened);
    } catch(e) { return null; }
  },
  encrypt: function(plaintext) {
    console.log('PebBeep: Encrypt check: nacl=' + !!nacl + ' sk=' + !!mySecretKey + ' pk=' + !!peerPublicKey);
    if (!nacl || !mySecretKey || !peerPublicKey) {
      console.log('PebBeep: Encrypt missing: nacl=' + !!nacl + ' sk=' + (mySecretKey ? mySecretKey.length : 'null') + ' pk=' + (peerPublicKey ? peerPublicKey.length : 'null'));
      return null;
    }
    try {
      var msgBytes = strToBytes(plaintext);
      var nonce = nacl.randomBytes(nacl.box.nonceLength);
      var box = nacl.box(msgBytes, nonce, peerPublicKey, mySecretKey);
      if (!box) {
        console.log('PebBeep: nacl.box returned null');
        return null;
      }
      console.log('PebBeep: Encrypt OK, ciphertext ' + box.length + ' bytes');
      return { nonce: bytesToB64(nonce), ciphertext: bytesToB64(box) };
    } catch(e) {
      console.log('PebBeep: Encrypt error: ' + e);
      return null;
    }
  }
};

// --- AppMessage Queue ---
var sendQueue = [];
var sending = false;

function queueMessage(msg) {
  sendQueue.push(msg);
  processSendQueue();
}

function processSendQueue() {
  if (sending || sendQueue.length === 0) return;
  sending = true;
  var msg = sendQueue.shift();
  Pebble.sendAppMessage(msg, function() {
    sending = false;
    processSendQueue();
  }, function() {
    sending = false;
    sendQueue.unshift(msg);
    setTimeout(processSendQueue, 500);
  });
}

// --- Relay HTTP ---
function relayRequest(method, path, body, callback, _retried) {
  var token = localStorage.getItem('pebbeep_auth_token');
  if (!token) {
    console.log('PebBeep: No auth token — cannot reach relay');
    callback(null);
    return;
  }

  var xhr = new XMLHttpRequest();
  xhr.open(method, RELAY_URL + path);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.setRequestHeader('Authorization', 'Bearer ' + token);
  xhr.timeout = 15000;
  xhr.onload = function() {
    if (xhr.status >= 200 && xhr.status < 300) {
      try { callback(JSON.parse(xhr.responseText)); } catch(e) { callback(null); }
    } else if (xhr.status === 401 && !_retried) {
      // Token expired — try refreshing
      console.log('PebBeep: 401 — refreshing token...');
      refreshAuthToken(function(success) {
        if (success) {
          // Retry the request with new token
          relayRequest(method, path, body, callback, true);
        } else {
          console.log('PebBeep: Token refresh failed — relay request failed');
          callback(null);
        }
      });
    } else {
      console.log('PebBeep: Relay ' + method + ' ' + path + ' => ' + xhr.status);
      callback(null);
    }
  };
  xhr.onerror = function() { callback(null); };
  xhr.ontimeout = function() { callback(null); };
  xhr.send(body ? JSON.stringify(body) : null);
}

// --- Token refresh ---
var isRefreshing = false;

function refreshAuthToken(callback) {
  var refreshToken = localStorage.getItem('pebbeep_refresh_token');
  if (!refreshToken || isRefreshing) {
    console.log('PebBeep: Cannot refresh — no refresh token');
    callback(false);
    return;
  }

  isRefreshing = true;
  console.log('PebBeep: Refreshing auth token...');

  // Firebase token refresh endpoint (key loaded from config.js)
  var url = 'https://securetoken.googleapis.com/v1/token?key=' + PebBeepConfig.FIREBASE_API_KEY;
  var xhr = new XMLHttpRequest();
  xhr.open('POST', url);
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  xhr.timeout = 15000;
  xhr.onload = function() {
    isRefreshing = false;
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        if (data.id_token) {
          localStorage.setItem('pebbeep_auth_token', data.id_token);
          if (data.refresh_token) {
            localStorage.setItem('pebbeep_refresh_token', data.refresh_token);
          }
          console.log('PebBeep: Token refreshed! New token: ' + data.id_token.substring(0, 20) + '...');
          callback(true);
          return;
        }
      } catch(e) {}
    }
    console.log('PebBeep: Token refresh failed: ' + xhr.status);
    callback(false);
  };
  xhr.onerror = function() { isRefreshing = false; console.log('PebBeep: Token refresh network error'); callback(false); };
  xhr.ontimeout = function() { isRefreshing = false; console.log('PebBeep: Token refresh timeout'); callback(false); };
  xhr.send('grant_type=refresh_token&refresh_token=' + encodeURIComponent(refreshToken));
}

// --- Decrypt a relay message ---
function serviceLabel(service) {
  switch (service) {
    case 'imessage':   return 'iMsg';
    case 'slack':      return 'Slack';
    case 'signal':     return 'Signal';
    case 'whatsapp':   return 'WA';
    case 'telegram':   return 'TG';
    case 'instagram':  return 'IG';
    case 'messenger':  return 'FB';
    case 'discord':    return 'Discord';
    case 'linkedin':   return 'LinkedIn';
    case 'twitter':    return 'X';
    case 'googlechat': return 'GChat';
    case 'gmessages':  return 'GMsg';
    case 'matrix':     return 'Matrix';
    default:           return '';
  }
}

function decryptRelayMessage(msg) {
  try {
    var plaintext = crypto.decrypt(msg.nonce, msg.ciphertext);
    if (!plaintext) {
      console.log('PebBeep: Decrypt failed for msg ' + (msg.id || '?').substring(0, 8));
      return null;
    }
    var payload = JSON.parse(plaintext);
    // Save chatName → real chatId mapping for replies
    if (payload.chatName && payload.chatId) {
      chatIdMap[payload.chatName] = payload.chatId;
    }
    return {
      chatId: payload.chatId || msg.chatId,
      chatName: payload.chatName || 'Unknown',
      sender: payload.sender || 'Unknown',
      text: payload.text || '',
      service: payload.service || '',
      timestamp: payload.timestamp || msg.timestamp
    };
  } catch (e) {
    console.log('PebBeep: Decrypt error: ' + e);
    return null;
  }
}

// --- Chat fetching ---
function fetchChats() {
  console.log('PebBeep: Fetching chats from relay');
  relayRequest('GET', '/messages?since=0&limit=50&direction=incoming', null, function(data) {
    if (!data || !data.messages) {
      console.log('PebBeep: No messages from relay');
      var msg = {};
      msg[KEY_COMMAND] = 'status';
      msg[KEY_STATUS] = 'no_chats';
      queueMessage(msg);
      return;
    }

    // Decrypt and group by chat
    var chatMap = {};
    var decryptCount = 0;
    var failCount = 0;
    for (var i = 0; i < data.messages.length; i++) {
      var m = data.messages[i];
      if (m.chatId === '__handshake__') continue;

      var decrypted = decryptRelayMessage(m);
      if (decrypted) {
        decryptCount++;
        var key = decrypted.chatName || m.chatId;
        if (!chatMap[key] || decrypted.timestamp > chatMap[key].timestamp) {
          chatMap[key] = {
            chatId: m.chatId,
            chatName: decrypted.chatName,
            service: decrypted.service || '',
            preview: decrypted.sender + ': ' + decrypted.text,
            timestamp: decrypted.timestamp
          };
        }
      } else {
        failCount++;
      }
    }
    console.log('PebBeep: Decrypted ' + decryptCount + '/' + data.messages.length + ' messages (' + failCount + ' failed)');

    var chats = [];
    for (var key in chatMap) {
      chats.push(chatMap[key]);
    }
    chats.sort(function(a, b) { return b.timestamp - a.timestamp; });

    var total = Math.min(chats.length, 10);
    console.log('PebBeep: Found ' + total + ' chats');

    if (total === 0) {
      var msg = {};
      msg[KEY_COMMAND] = 'status';
      msg[KEY_STATUS] = 'no_chats';
      queueMessage(msg);
      return;
    }

    for (var j = 0; j < total; j++) {
      var chat = chats[j];
      var msg = {};
      msg[KEY_COMMAND] = 'chat_item';
      msg[KEY_INDEX] = j;
      msg[KEY_TOTAL] = total;
      msg[KEY_CHAT_ID] = chat.chatId;
      var displayName = chat.chatName || 'Chat';
      var label = serviceLabel(chat.service);
      if (label) displayName = '[' + label + '] ' + displayName;
      msg[KEY_CHAT_NAME] = displayName.substring(0, 20);
      msg[KEY_CHAT_PREVIEW] = (chat.preview || '').substring(0, 30);
      queueMessage(msg);
    }
  });
}

// --- Fetch messages for a specific chat ---
function fetchMessagesForChat(chatId) {
  console.log('PebBeep: Fetching messages for chat: ' + chatId);
  relayRequest('GET', '/messages?since=0&limit=50&direction=incoming', null, function(data) {
    if (!data || !data.messages) {
      console.log('PebBeep: No messages from relay');
      return;
    }

    // Decrypt ALL messages and filter by chatName or hashed chatId
    var chatMessages = [];
    for (var i = 0; i < data.messages.length; i++) {
      var m = data.messages[i];
      if (m.chatId === '__handshake__') continue;

      var decrypted = decryptRelayMessage(m);
      if (!decrypted) continue;

      // Match by hashed chatId OR by decrypted chatName
      var matches = (m.chatId === chatId) ||
                    (decrypted.chatName === chatId) ||
                    (decrypted.chatId === chatId);
      if (!matches) continue;

      chatMessages.push({
        sender: decrypted.sender,
        text: decrypted.text,
        timestamp: decrypted.timestamp,
        chatName: decrypted.chatName
      });
    }

    chatMessages.sort(function(a, b) { return a.timestamp - b.timestamp; });

    var start = Math.max(0, chatMessages.length - 5);
    console.log('PebBeep: Sending ' + (chatMessages.length - start) + '/' + chatMessages.length + ' messages to watch for ' + chatId);

    for (var j = start; j < chatMessages.length; j++) {
      var cm = chatMessages[j];
      var time = new Date(cm.timestamp);
      var timeStr = time.getHours() + ':' + ('0' + time.getMinutes()).slice(-2);

      var msg = {};
      msg[KEY_COMMAND] = 'new_message';
      msg[KEY_INDEX] = j - start;
      msg[KEY_MSG_SENDER] = (cm.sender || 'Unknown').substring(0, 18);
      msg[KEY_MSG_TEXT] = (cm.text || '').substring(0, 200);
      msg[KEY_MSG_TIME] = timeStr;
      msg[KEY_CHAT_NAME] = (cm.chatName || chatId).substring(0, 16);
      queueMessage(msg);
    }

    if (chatMessages.length === 0) {
      console.log('PebBeep: No decryptable messages for chat ' + chatId);
    }
  });
}

// --- Reply sending ---
function sendReply(chatId, text) {
  // Look up real Beeper chatId from display name
  var realChatId = chatIdMap[chatId] || chatId;
  console.log('PebBeep: Sending reply "' + text + '" to ' + chatId + ' (real: ' + realChatId.substring(0, 30) + ')');

  // Encrypt the reply with the REAL chatId
  var payload = JSON.stringify({
    chatId: realChatId,
    chatName: chatId,
    sender: 'Me',
    text: text,
    timestamp: Date.now()
  });

  var encrypted = crypto.encrypt(payload);
  if (!encrypted) {
    console.log('PebBeep: Reply encryption failed — not paired?');
    var msg = {};
    msg[KEY_COMMAND] = 'status';
    msg[KEY_STATUS] = 'error';
    queueMessage(msg);
    return;
  }

  relayRequest('POST', '/messages', {
    messages: [{
      nonce: encrypted.nonce,
      ciphertext: encrypted.ciphertext,
      direction: 'outgoing',
      chatId: chatId,
      timestamp: Date.now(),
      expiresAt: Date.now() + (7 * 24 * 60 * 60 * 1000)
    }]
  }, function(data) {
    var msg = {};
    msg[KEY_COMMAND] = 'status';
    msg[KEY_STATUS] = data ? 'sent' : 'error';
    queueMessage(msg);
  });
}

// --- Poll for new messages ---
// Track seen message IDs to avoid re-notifying on relaunch
var seenMessageIds = {};

function pollForNew() {
  var token = localStorage.getItem('pebbeep_auth_token');
  if (!token) return;

  relayRequest('GET', '/messages?since=' + lastPollTimestamp + '&limit=10&direction=incoming', null, function(data) {
    if (!data || !data.messages || data.messages.length === 0) return;

    console.log('PebBeep: Got ' + data.messages.length + ' new messages from relay');
    for (var i = 0; i < data.messages.length; i++) {
      var m = data.messages[i];
      if (m.chatId === '__handshake__' || m.chatId === '__timeline_token__') continue;
      if (m.timestamp > lastPollTimestamp) {
        lastPollTimestamp = m.timestamp;
        // Persist so we don't re-process on relaunch
        localStorage.setItem('pebbeep_last_poll_ts', String(lastPollTimestamp));
      }

      // Skip already-seen messages
      if (seenMessageIds[m.id]) continue;
      seenMessageIds[m.id] = true;

      var decrypted = decryptRelayMessage(m);
      var sender = decrypted ? decrypted.sender : 'Unknown';
      var text = decrypted ? decrypted.text : 'Encrypted';
      var chatName = decrypted ? decrypted.chatName : m.chatId.substring(0, 16);
      var service = (decrypted && decrypted.service) ? decrypted.service : '';

      var time = new Date(m.timestamp);
      var timeStr = time.getHours() + ':' + ('0' + time.getMinutes()).slice(-2);

      console.log('PebBeep: Message: ' + sender + ' [' + service + '] → ' + text.substring(0, 30));

      var msg = {};
      msg[KEY_COMMAND] = 'new_message';
      msg[KEY_INDEX] = -1;
      msg[KEY_MSG_SENDER] = sender.substring(0, 18);
      msg[KEY_MSG_TEXT] = text.substring(0, 200);
      msg[KEY_MSG_TIME] = timeStr;
      msg[KEY_CHAT_NAME] = chatName.substring(0, 20);
      if (service) msg[KEY_SERVICE] = service;
      queueMessage(msg);
    }
  });
}

// --- Configuration ---
Pebble.addEventListener('showConfiguration', function() {
  console.log('PebBeep: Opening config page');
  var url = CONFIG_URL;
  var params = [];

  var peerKey = localStorage.getItem('pebbeep_peer_public_key') || '';
  var email = localStorage.getItem('pebbeep_user_email') || '';
  var reply1 = localStorage.getItem('pebbeep_custom_reply_1') || '';
  var reply2 = localStorage.getItem('pebbeep_custom_reply_2') || '';
  var reply3 = localStorage.getItem('pebbeep_custom_reply_3') || '';
  var reply4 = localStorage.getItem('pebbeep_custom_reply_4') || '';
  var vibrate = localStorage.getItem('pebbeep_vibrate') || '1';
  var showPopup = localStorage.getItem('pebbeep_show_popup') || '1';
  var pollInt = localStorage.getItem('pebbeep_poll_interval') || '30';
  var signedIn = localStorage.getItem('pebbeep_auth_token') ? '1' : '0';

  console.log('PebBeep: Current state: signedIn=' + signedIn + ' email=' + email + ' peerKey=' + (peerKey ? 'yes' : 'no'));

  if (peerKey) params.push('publicKey=' + encodeURIComponent(peerKey));
  if (email) params.push('email=' + encodeURIComponent(email));
  if (reply1) params.push('reply1=' + encodeURIComponent(reply1));
  if (reply2) params.push('reply2=' + encodeURIComponent(reply2));
  if (reply3) params.push('reply3=' + encodeURIComponent(reply3));
  if (reply4) params.push('reply4=' + encodeURIComponent(reply4));
  params.push('vibrate=' + vibrate);
  params.push('showPopup=' + showPopup);
  params.push('pollInterval=' + pollInt);
  params.push('signedIn=' + signedIn);

  // Pass existing Pebble keypair so config page doesn't generate new ones
  var pebblePk = localStorage.getItem('pebbeep_pebble_public_key') || '';
  var pebbleSk = localStorage.getItem('pebbeep_pebble_secret_key') || '';
  if (pebblePk) params.push('pebblePk=' + encodeURIComponent(pebblePk));
  if (pebbleSk) params.push('pebbleSk=' + encodeURIComponent(pebbleSk));

  if (params.length) url += '?' + params.join('&');
  console.log('PebBeep: Config URL: ' + url.substring(0, 100) + '...');
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) {
    console.log('PebBeep: Config closed with no response');
    return;
  }

  console.log('PebBeep: Config response received, length=' + (e.response || '').length);

  try {
    var config = JSON.parse(decodeURIComponent(e.response));

    // Debug: log all keys received
    var keys = Object.keys(config);
    console.log('PebBeep: Config keys: ' + keys.join(', '));
    console.log('PebBeep: authToken: ' + (config.authToken ? config.authToken.substring(0, 30) + '...' : 'NONE'));
    console.log('PebBeep: publicKey: ' + (config.publicKey ? 'yes' : 'no'));
    console.log('PebBeep: userEmail: ' + (config.userEmail || 'none'));

    // Save auth + pairing data
    if (config.authToken) {
      localStorage.setItem('pebbeep_auth_token', config.authToken);
      console.log('PebBeep: Auth token SAVED (' + config.authToken.length + ' chars)');
    } else {
      console.log('PebBeep: WARNING: No authToken in config response!');
    }
    if (config.refreshToken) {
      localStorage.setItem('pebbeep_refresh_token', config.refreshToken);
      console.log('PebBeep: Refresh token SAVED');
    }
    if (config.publicKey) localStorage.setItem('pebbeep_peer_public_key', config.publicKey);
    if (config.relayUrl) localStorage.setItem('pebbeep_relay_url', config.relayUrl);
    if (config.userId) localStorage.setItem('pebbeep_user_id', config.userId);
    if (config.userEmail) localStorage.setItem('pebbeep_user_email', config.userEmail);
    // Only save keypair if we don't already have one (never overwrite!)
    if (config.pebblePublicKey && !localStorage.getItem('pebbeep_pebble_secret_key')) {
      localStorage.setItem('pebbeep_pebble_public_key', config.pebblePublicKey);
      if (config.pebbleSecretKey) localStorage.setItem('pebbeep_pebble_secret_key', config.pebbleSecretKey);
      console.log('PebBeep: Saved new keypair from config');
    } else {
      console.log('PebBeep: Keeping existing keypair (not overwriting)');
    }

    // Save settings
    if (config.customReply1 !== undefined) localStorage.setItem('pebbeep_custom_reply_1', config.customReply1);
    if (config.customReply2 !== undefined) localStorage.setItem('pebbeep_custom_reply_2', config.customReply2);
    if (config.customReply3 !== undefined) localStorage.setItem('pebbeep_custom_reply_3', config.customReply3);
    if (config.customReply4 !== undefined) localStorage.setItem('pebbeep_custom_reply_4', config.customReply4);
    if (config.vibrate !== undefined) localStorage.setItem('pebbeep_vibrate', config.vibrate ? '1' : '0');
    if (config.showPopup !== undefined) localStorage.setItem('pebbeep_show_popup', config.showPopup ? '1' : '0');
    if (config.pollInterval !== undefined) {
      localStorage.setItem('pebbeep_poll_interval', String(config.pollInterval));
      POLL_INTERVAL = config.pollInterval * 1000;
      if (pollTimer) { clearInterval(pollTimer); pollTimer = setInterval(pollForNew, POLL_INTERVAL); }
    }

    // Verify token was saved
    var savedToken = localStorage.getItem('pebbeep_auth_token');
    console.log('PebBeep: Token verification: ' + (savedToken ? 'saved (' + savedToken.length + ' chars)' : 'NOT SAVED'));

    // Send settings to watch
    var dict = {};
    dict[KEY_CUSTOM_REPLY_1] = config.customReply1 || '';
    dict[KEY_CUSTOM_REPLY_2] = config.customReply2 || '';
    dict[KEY_CUSTOM_REPLY_3] = config.customReply3 || '';
    dict[KEY_CUSTOM_REPLY_4] = config.customReply4 || '';
    dict[KEY_VIBRATE_ON_MESSAGE] = config.vibrate ? 1 : 0;
    dict[KEY_SHOW_NOTIFICATION] = config.showPopup ? 1 : 0;
    dict[KEY_POLL_INTERVAL] = parseInt(config.pollInterval, 10) || 30;

    Pebble.sendAppMessage(dict, function() {
      console.log('PebBeep: Settings sent to watch');
    }, function(err) {
      console.log('PebBeep: Failed to send settings');
    });

    // Re-initialize crypto with new keys
    crypto.init();

    // Start polling if we now have auth
    if (config.authToken && !pollTimer) {
      console.log('PebBeep: Starting relay polling with new token');
      pollTimer = setInterval(pollForNew, POLL_INTERVAL);
      setTimeout(pollForNew, 2000);
    }

    // Immediately fetch chats if we have auth
    if (config.authToken) {
      console.log('PebBeep: Fetching chats with new token...');
      setTimeout(fetchChats, 3000);
    }

  } catch (err) {
    console.log('PebBeep: Config parse error: ' + err);
    console.log('PebBeep: Raw response: ' + (e.response || '').substring(0, 200));
  }
});

// --- Timeline Token ---
function sendTimelineToken() {
  try {
    Pebble.getTimelineToken(function(token) {
      console.log('PebBeep: Timeline token: ' + token);
      localStorage.setItem('pebbeep_timeline_token', token);

      // Push timeline token to relay so desktop can send pins
      var authToken = localStorage.getItem('pebbeep_auth_token');
      if (authToken && token) {
        var encrypted = crypto.encrypt(JSON.stringify({ timelineToken: token }));
        if (encrypted) {
          relayRequest('POST', '/messages', {
            messages: [{
              nonce: encrypted.nonce,
              ciphertext: encrypted.ciphertext,
              direction: 'outgoing',
              chatId: '__timeline_token__',
              timestamp: Date.now(),
              expiresAt: Date.now() + (30 * 24 * 60 * 60 * 1000)
            }]
          }, function() {
            console.log('PebBeep: Timeline token pushed to relay');
          });
        }
      }
    }, function(err) {
      console.log('PebBeep: Timeline token error: ' + err);
    });
  } catch(e) {
    console.log('PebBeep: Timeline not available: ' + e);
  }
}

// --- App Ready ---
Pebble.addEventListener('ready', function() {
  console.log('PebBeep: JS companion ready');

  // Initialize crypto
  crypto.init();

  var token = localStorage.getItem('pebbeep_auth_token');
  console.log('PebBeep: Startup auth token: ' + (token ? 'yes (' + token.length + ' chars)' : 'NONE'));
  console.log('PebBeep: Startup email: ' + (localStorage.getItem('pebbeep_user_email') || 'none'));
  console.log('PebBeep: Startup peerKey: ' + (localStorage.getItem('pebbeep_peer_public_key') ? 'yes' : 'no'));

  var savedInterval = localStorage.getItem('pebbeep_poll_interval');
  if (savedInterval) POLL_INTERVAL = parseInt(savedInterval, 10) * 1000;

  // Restore last poll timestamp so we don't re-show old messages
  var savedTs = localStorage.getItem('pebbeep_last_poll_ts');
  if (savedTs) {
    lastPollTimestamp = parseInt(savedTs, 10) || 0;
    console.log('PebBeep: Restored lastPollTimestamp: ' + lastPollTimestamp);
  }

  if (token) {
    console.log('PebBeep: Starting polling (interval: ' + POLL_INTERVAL + 'ms)');
    pollTimer = setInterval(pollForNew, POLL_INTERVAL);
    setTimeout(pollForNew, 2000);
  } else {
    console.log('PebBeep: No token — polling disabled until config');
  }

  // Get timeline token for push notifications
  sendTimelineToken();
});

// --- Watch Messages ---
Pebble.addEventListener('appmessage', function(e) {
  var command = e.payload[KEY_COMMAND];
  console.log('PebBeep: Watch command: ' + command);

  if (command === 'fetch_chats') {
    fetchChats();
  } else if (command === 'fetch_messages') {
    var chatId = e.payload[KEY_CHAT_ID];
    console.log('PebBeep: Fetch messages for ' + chatId);
    fetchMessagesForChat(chatId);
  } else if (command === 'send_reply') {
    var chatId = e.payload[KEY_CHAT_ID];
    var text = e.payload[KEY_REPLY_TEXT];
    sendReply(chatId, text);
  }
});
