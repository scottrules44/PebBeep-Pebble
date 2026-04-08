/**
 * PebBeep Message Handler
 * Encrypts outgoing and decrypts incoming messages using TweetNaCl.
 */

var crypto = require('./crypto');

/**
 * Decrypt an encrypted message from the relay.
 * Returns the parsed payload or null on failure.
 */
function decryptMessage(encMsg) {
  try {
    var plaintext = crypto.decrypt(encMsg.nonce, encMsg.ciphertext);
    if (!plaintext) return null;

    var payload = JSON.parse(plaintext);
    return {
      chatId: payload.chatId,
      chatName: payload.chatName || 'Unknown',
      sender: payload.sender || 'Unknown',
      text: payload.text || '',
      timestamp: payload.timestamp || encMsg.timestamp
    };
  } catch (e) {
    console.log('PebBeep: Decrypt error: ' + e);
    return null;
  }
}

/**
 * Encrypt a plaintext JSON string for the relay.
 * Returns { nonce, ciphertext } as base64 strings, or null on failure.
 */
function encryptMessage(plaintext) {
  try {
    return crypto.encrypt(plaintext);
  } catch (e) {
    console.log('PebBeep: Encrypt error: ' + e);
    return null;
  }
}

module.exports = {
  decryptMessage: decryptMessage,
  encryptMessage: encryptMessage
};
