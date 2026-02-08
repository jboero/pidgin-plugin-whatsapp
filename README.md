# Pidgin WhatsApp Plugin (whatsmeow-lite)

A **from-scratch** libpurple protocol plugin that adds WhatsApp support to Pidgin, powered by [whatsmeow](https://github.com/tulir/whatsmeow).

This project provides two things:

1. **`install-purple-whatsapp.sh`** — A turnkey installer for the mature [purple-gowhatsapp](https://github.com/hoehermann/purple-gowhatsapp) plugin (recommended for daily use)
2. **The `./` directory** — A clean, minimal, from-scratch implementation showing exactly how the C↔Go bridge architecture works (educational + hackable)

## Quick Start (Option A: Use the existing plugin)

The easiest path to WhatsApp in Pidgin:

```bash
bash install-purple-whatsapp.sh --user
```

This clones, builds, and installs purple-gowhatsapp automatically. Supports Ubuntu/Debian, Fedora, and Arch Linux.

## Build From Scratch (Option B: This plugin)

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install pidgin libpurple-dev libgdk-pixbuf2.0-dev \
    libopusfile-dev golang gcc cmake pkg-config

# Fedora
sudo dnf install pidgin libpurple-devel gdk-pixbuf2-devel \
    opusfile-devel golang gcc cmake pkg-config

# Arch
sudo pacman -S pidgin libpurple gdk-pixbuf2 opusfile go gcc cmake pkg-config
```

### Build with Make (simpler)

```bash
make
make install    # installs to ~/.purple/plugins/
```

### Build with CMake

```bash
mkdir build && cd build
cmake -DPURPLE_PLUGIN_DIR:PATH=~/.purple/plugins ..
cmake --build .
cmake --install . --strip
```

### First Run

1. Launch Pidgin
2. Accounts → Manage Accounts → Add
3. Protocol: **WhatsApp (whatsmeow)**
4. Username: `<countrycode><phone>@s.whatsapp.net`
   - Example (Singapore): `6512345678@s.whatsapp.net`
   - Example (US): `14155551234@s.whatsapp.net`
5. Password: *(leave blank)*
6. Click Add → A QR code dialog appears
7. On your phone: WhatsApp → Settings → Linked Devices → Link a Device → Scan

## Architecture

The plugin uses a **C↔Go bridge** pattern — the same approach used by purple-gowhatsapp:

```
┌─────────────────────────────────────────────────┐
│  Pidgin / libpurple                              │
│  (buddy list, conversations, notifications)      │
└──────────────┬──────────────────────────────────┘
               │ C function calls
┌──────────────┴──────────────────────────────────┐
│  plugin.c  (C)                                   │
│  • Registers protocol plugin with libpurple      │
│  • Implements prpl callbacks (login, send_im)    │
│  • Implements Go→C callbacks (bridge_*)          │
└──────────────┬──────────────────────────────────┘
               │ CGO exported functions
┌──────────────┴──────────────────────────────────┐
│  whatsmeow_bridge.go  (Go, compiled to .a)       │
│  • whatsmeow client lifecycle                    │
│  • QR pairing / session management               │
│  • Message send/receive                          │
│  • Presence & typing notifications               │
└──────────────┬──────────────────────────────────┘
               │ Signal Protocol (E2E encrypted WebSocket)
┌──────────────┴──────────────────────────────────┐
│  WhatsApp Servers                                │
└─────────────────────────────────────────────────┘
```

### How the bridge works

The key insight is that Go can compile to a **C static archive** (`.a` file) using `go build -buildmode=c-archive`. This produces:

- `libwhatsmeow-bridge.a` — static library with all Go code + runtime
- `libwhatsmeow-bridge.h` — auto-generated C header for exported Go functions

The C plugin links against this archive, so the final `.so` is a single shared library containing both the C libpurple glue and the entire Go runtime + whatsmeow.

**bridge.h** defines the contract:

| Direction | Function | Purpose |
|-----------|----------|---------|
| C → Go | `gowhatsapp_go_login()` | Start WhatsApp connection |
| C → Go | `gowhatsapp_go_send_message()` | Send a text message |
| C → Go | `gowhatsapp_go_send_typing()` | Send typing indicator |
| C → Go | `gowhatsapp_go_mark_read()` | Mark message as read |
| C → Go | `gowhatsapp_go_logout()` | Disconnect |
| Go → C | `bridge_show_qr_code()` | Display QR for pairing |
| Go → C | `bridge_connected()` | Signal successful connection |
| Go → C | `bridge_receive_message()` | Deliver incoming message |
| Go → C | `bridge_presence_update()` | Update buddy online/offline |
| Go → C | `bridge_typing_notification()` | Show typing indicator |
| Go → C | `bridge_error()` | Report error to user |

## Security Design

| Aspect | Implementation |
|--------|---------------|
| **E2E Encryption** | Signal protocol handled entirely by whatsmeow — the C side never sees encryption keys or plaintext crypto material |
| **Session Storage** | SQLite DB at `~/.purple/whatsmeow/<phone>.db` with `0600` permissions |
| **No Proxies** | Direct WebSocket to WhatsApp servers, same as official WhatsApp Web |
| **QR Code** | Displayed locally via purple_notify_formatted — never transmitted |
| **Memory Safety** | Go side manages its own memory; C↔Go boundary uses explicit malloc/free with clear ownership |
| **No Passwords** | Authentication is via linked device (QR scan), not stored passwords |

## Comparison with purple-gowhatsapp

| Feature | This plugin (whatsmeow-lite) | purple-gowhatsapp |
|---------|------------------------------|-------------------|
| Purpose | Educational PoC / hackable base | Production-ready |
| Text messages | ✅ | ✅ |
| Group chats | ✅ (basic) | ✅ (full) |
| Image/video/audio | ❌ (shows placeholder) | ✅ |
| File sending | ❌ | ✅ |
| Contact sync | ❌ | ✅ |
| Profile pictures | ❌ | ✅ |
| Reactions | ❌ | ✅ |
| Read receipts | ✅ (sending) | ✅ |
| Typing indicators | ✅ | ✅ |
| QR code display | Text (raw) | Image |
| Code complexity | ~600 lines | ~3000+ lines |

For daily use, install purple-gowhatsapp via the script. This plugin is for understanding the architecture and as a clean starting point if you want to fork/customize.

## File Structure

```
pidgin-whatsapp/
├── CMakeLists.txt              # CMake build system
├── Makefile                    # Simpler make-based build
├── README.md
├── install-purple-whatsapp.sh  # Turnkey installer for purple-gowhatsapp
├── plugin.c            # libpurple protocol plugin (C side)
└── whatsmeow_bridge.go # whatsmeow wrapper (Go side)
```
