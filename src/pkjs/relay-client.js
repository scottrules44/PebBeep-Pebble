/**
 * PebBeep Relay Client
 * Communicates with the relay API via XMLHttpRequest.
 */

function getRelayUrl() {
  return localStorage.getItem('pebbeep_relay_url') || '';
}

function getAuthToken() {
  return localStorage.getItem('pebbeep_auth_token') || '';
}

/**
 * Make an authenticated HTTP request to the relay.
 */
function request(method, path, body, callback) {
  var relayUrl = getRelayUrl();
  if (!relayUrl) {
    console.log('PebBeep: No relay URL configured');
    callback(null);
    return;
  }

  var url = relayUrl + path;
  var xhr = new XMLHttpRequest();
  xhr.open(method, url);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.setRequestHeader('Authorization', 'Bearer ' + getAuthToken());
  xhr.timeout = 15000; // 15 second timeout

  xhr.onload = function() {
    if (xhr.status >= 200 && xhr.status < 300) {
      try {
        var data = JSON.parse(xhr.responseText);
        callback(data);
      } catch (e) {
        console.log('PebBeep: JSON parse error: ' + e);
        callback(null);
      }
    } else {
      console.log('PebBeep: HTTP ' + xhr.status + ' from ' + path);
      callback(null);
    }
  };

  xhr.onerror = function() {
    console.log('PebBeep: Request error for ' + path);
    callback(null);
  };

  xhr.ontimeout = function() {
    console.log('PebBeep: Request timeout for ' + path);
    callback(null);
  };

  if (body) {
    xhr.send(JSON.stringify(body));
  } else {
    xhr.send();
  }
}

/**
 * Poll for messages from the relay.
 */
function pollMessages(since, limit, direction, callback) {
  var path = '/messages?since=' + since + '&limit=' + (limit || 50);
  if (direction) {
    path += '&direction=' + direction;
  }

  request('GET', path, null, function(data) {
    if (data && data.messages) {
      callback(data.messages);
    } else {
      callback([]);
    }
  });
}

/**
 * Push an encrypted message to the relay.
 */
function pushMessage(msg, callback) {
  request('POST', '/messages', { messages: [msg] }, function(data) {
    callback(data !== null);
  });
}

/**
 * Delete / confirm receipt of a message.
 */
function deleteMessage(id, callback) {
  request('DELETE', '/messages/' + encodeURIComponent(id), null, function(data) {
    callback(data !== null);
  });
}

module.exports = {
  pollMessages: pollMessages,
  pushMessage: pushMessage,
  deleteMessage: deleteMessage
};
