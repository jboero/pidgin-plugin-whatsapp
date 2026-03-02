/*
 * bridge.h — Shared header between Go (whatsmeow) and C (libpurple) sides.
 *
 * This defines:
 *   1. An opaque account handle type (pointer to PurpleAccount)
 *   2. Go→C callback declarations (implemented in plugin.c)
 *   3. C→Go function declarations (implemented in whatsmeow_bridge.go)
 *
 * The bridge is intentionally thin — each side owns its own complexity:
 *   Go side: WhatsApp protocol, encryption, WebSocket
 *   C side: libpurple API, buddy lists, conversation windows, UI
 */
#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to a PurpleAccount — Go doesn't need to know the struct layout */
typedef uintptr_t gowhatsapp_account_t;

/* ────────────────────────────────────────────────────────────────
 * Go → C callbacks (implemented in plugin.c, called from Go)
 * ──────────────────────────────────────────────────────────────── */

/* Show QR code to user for pairing. `qr_data` is the raw QR string. */
void bridge_show_qr_code(gowhatsapp_account_t account, const char *qr_data);

/* Notify that connection is established (QR scanned or session resumed). */
void bridge_connected(gowhatsapp_account_t account);

/* Notify that connection was lost. */
void bridge_disconnected(gowhatsapp_account_t account);

/* Report an error message to the user. */
void bridge_error(gowhatsapp_account_t account, const char *message);

/* Deliver a received message to the purple conversation window. */
void bridge_receive_message(
    gowhatsapp_account_t account,
    const char *sender_jid,
    const char *chat_jid,
    const char *text,
    const char *message_id,
    const char *push_name,
    long timestamp,
    int from_me,
    int is_group
);

/* Update buddy presence (online/offline). */
void bridge_presence_update(
    gowhatsapp_account_t account,
    const char *jid,
    int available  /* 1 = online, 0 = offline */
);

/* Notify typing status for a contact. */
void bridge_typing_notification(
    gowhatsapp_account_t account,
    const char *jid,
    int composing  /* 1 = typing, 0 = stopped */
);

/* ────────────────────────────────────────────────────────────────
 * C → Go functions (implemented in whatsmeow_bridge.go via CGO export)
 * ──────────────────────────────────────────────────────────────── */

/* Initiate WhatsApp login. Phone format: "6512345678" (no @s.whatsapp.net). */
int gowhatsapp_go_login(gowhatsapp_account_t account, const char *phone);

/* Disconnect and clean up. */
void gowhatsapp_go_logout(gowhatsapp_account_t account);

/* Send a text message to the given JID. Returns 0 on success. */
int gowhatsapp_go_send_message(
    gowhatsapp_account_t account,
    const char *jid,
    const char *text
);

/* Send typing notification. typing=1 for composing, 0 for stopped. */
void gowhatsapp_go_send_typing(
    gowhatsapp_account_t account,
    const char *jid,
    int typing
);

/* Mark a message as read. */
void gowhatsapp_go_mark_read(
    gowhatsapp_account_t account,
    const char *jid,
    const char *message_id,
    const char *sender_jid
);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_H */
