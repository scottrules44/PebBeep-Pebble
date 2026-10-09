# PebBeep

Beeper on your wrist. PebBeep is a Pebble watchapp that shows your recent
chats, lets you read the last few messages of a conversation, and reply with
canned responses or voice dictation. Messages travel end-to-end encrypted
(TweetNaCl `box`) between the PebBeep desktop bridge and the watch's phone
companion through a small relay.

```
Beeper ──▶ PebBeep desktop ──▶ relay (Cloud Function) ──▶ PebbleKit JS ──▶ watch
            (encrypts)          (stores ciphertext)        (decrypts)     (this repo)
```

This repository contains the watch app (C) and its PebbleKit JS companion.
The desktop bridge, relay function and config page are deployed separately;
see `src/pkjs/config.example.js` for what the companion needs to know.

## Supported watches

| Platform | Watch                          | Display            | Touch | Voice |
|----------|--------------------------------|--------------------|-------|-------|
| aplite   | Pebble Classic / Steel         | 144x168 B/W        |       |       |
| basalt   | Pebble Time / Time Steel       | 144x168 colour     |       | ✓     |
| chalk    | Pebble Time Round              | 180x180 colour     |       | ✓     |
| diorite  | Pebble 2                       | 144x168 B/W        |       | ✓     |
| flint    | Pebble 2 Duo                   | 144x168 B/W        |       | ✓     |
| emery    | Pebble Time 2                  | 200x228 colour     | ✓     | ✓     |
| gabbro   | Pebble Round 2                 | 260x260 colour     | ✓     | ✓     |

## Controls

Buttons work everywhere:

| Screen        | UP / DOWN        | SELECT                 | BACK                     |
|---------------|------------------|------------------------|--------------------------|
| Welcome       | DOWN: demo mode  | Connect (live mode)    | Exit                     |
| Chat list     | Move selection   | Open chat (hold: reply)| Back / exit              |
| Conversation  | Scroll           | Reply                  | Back to chats            |
| Reply menu    | Move selection   | Send / start dictation | Cancel                   |
| Notification  | UP: previous     | Reply                  | Next queued, or dismiss  |

On the Pebble Time 2 and Pebble Round 2 the same screens are touch-driven:

- **Chat list and reply menu** scroll with a finger; a tap selects a row and
  a second tap (or a quick double tap) opens it. The system touch bridge
  drives these menus, and swipe right goes back.
- **Conversation** scrolls by dragging, flicks keep scrolling, a **Reply**
  button sits along the bottom edge, swipe right goes back and swipe left
  opens the reply menu.
- **Notifications** show **Reply** and **Next/Dismiss** buttons; swipe
  left/right pages through queued notifications.
- **Welcome** shows **Connect** and **Demo** buttons.

If touch is switched off in the watch's Settings → Display → Touch, the
button layouts are used instead.

## Building

PebBeep uses the Core Devices Pebble SDK (4.9 or newer; touch needs 4.33+).

```bash
brew install node libpng        # macOS prerequisites
uv tool install pebble-tool     # https://developer.repebble.com/sdk
pebble sdk install latest
```

Then, in this directory:

```bash
cp src/pkjs/config.example.js src/pkjs/config.js   # fill in your relay/Firebase values
pebble build
pebble install --emulator emery      # or basalt, chalk, gabbro, ...
pebble install --phone <phone-ip>    # a real watch via the phone app's developer connection
```

`config.js` is gitignored because it points at your own relay deployment.
The build falls back to `config.example.js` so an unconfigured tree still
compiles; the app then runs in demo mode only.

### Trying touch in the emulator

The emery and gabbro emulators have a working touchscreen. Start the
emulator with VNC and inject gestures with the helper script:

```bash
pebble install --emulator emery --vnc
python3 tools/emu-touch.py tap 148 208             # tap "Demo" on the welcome screen
python3 tools/emu-touch.py tap 100 50              # open the first chat
python3 tools/emu-touch.py pan 100 160 100 60      # drag the conversation
python3 tools/emu-touch.py tap 100 208             # tap "Reply"
pebble screenshot --emulator emery --vnc
```

Pass `--vnc` to every `pebble` command aimed at that emulator; pebble-tool
restarts the emulator whenever the flag changes. To simulate an incoming
message without a relay:

```bash
pebble send-app-message --emulator emery --vnc \
  --string 0=new_message 5=Alice 4="Are we still on for dinner?" 6=12:45 2=Alice 18=imessage \
  --int 9=-1
```

## Project layout

```
src/main.c           AppMessage plumbing, settings, routing between screens
src/welcome.c        First-run screen (connect / demo)
src/chat_list.c      MenuLayer of recent chats with service icons
src/message_view.c   Scrollable conversation with an optimistic local echo
src/reply_menu.c     Canned replies + voice dictation
src/notification.c   Incoming-message popup with a small queue
src/touch_ui.c       Shared touch helpers: availability, footer layout, pill buttons
src/pkjs/index.js    Phone companion: config page, relay polling, crypto
src/pkjs/lib/        Vendored TweetNaCl
resources/images/    20x20 service icons
tools/emu-touch.py   Touch injection for the emulator (see above)
```

Message keys are declared in `package.json` and must stay in the same order
as the `KEY_*` defines in `src/main.c` and `src/pkjs/index.js`.
