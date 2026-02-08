/*
 * plugin.c — libpurple protocol plugin for WhatsApp via whatsmeow.
 *
 * This is the C side of the bridge. It:
 *   1. Registers a protocol plugin with libpurple ("prpl-whatsmeow-lite")
 *   2. Implements libpurple callbacks (login, send_im, chat_send, etc.)
 *   3. Implements Go→C bridge callbacks (bridge_receive_message, etc.)
 *
 * The Go side (whatsmeow_bridge.go) handles all WhatsApp protocol logic.
 *
 * Build: This file is compiled as part of a shared library that links
 *        against both libpurple and the Go static archive.
 *
 * Security considerations:
 *   - We never handle encryption — that's entirely in whatsmeow (Go side)
 *   - QR code is displayed via purple_request_action (stays local)
 *   - Session DB lives in ~/.purple/whatsmeow/ with 0600 perms
 */

#include <string.h>
#include <time.h>

#include <purple.h>

/* The bridge header (shared with Go) */
#include "bridge.h"

/* Plugin metadata */
#define PLUGIN_ID       "prpl-whatsmeow-lite"
#define PLUGIN_NAME     "WhatsApp (whatsmeow)"
#define PLUGIN_VERSION  "0.1.0"
#define PLUGIN_AUTHOR   "whatsapp-native project"
#define PLUGIN_URL      "https://github.com/johnny/pidgin-whatsapp"
#define PLUGIN_SUMMARY  "WhatsApp via whatsmeow — lightweight, E2E encrypted"

/* ────────────────────────────────────────────────────────────────
 * Utility: extract phone number from purple account username
 * Username format: "6512345678@s.whatsapp.net"
 * ──────────────────────────────────────────────────────────────── */
static char *extract_phone(const char *username) {
    const char *at = strchr(username, '@');
    if (at == NULL) {
        return g_strdup(username);
    }
    return g_strndup(username, at - username);
}

/* ────────────────────────────────────────────────────────────────
 * Go → C bridge callback implementations
 * ──────────────────────────────────────────────────────────────── */

void bridge_show_qr_code(gowhatsapp_account_t account, const char *qr_data) {
    PurpleAccount *pa = (PurpleAccount *)account;
    PurpleConnection *gc = purple_account_get_connection(pa);
    if (gc == NULL) return;

    /* Display QR code as a request dialog.
     * In a full implementation, we'd render the QR as an image.
     * For the PoC, we show the raw code + instructions. */
    char *msg = g_strdup_printf(
        "<b>Scan this QR code with your phone:</b><br><br>"
        "WhatsApp → Settings → Linked Devices → Link a Device<br><br>"
        "<b>QR Code Data:</b><br>"
        "<tt>%s</tt><br><br>"
        "<i>Tip: Copy this string and paste it into a QR code generator, "
        "or use the terminal QR display if running from command line.</i>",
        qr_data
    );

    purple_notify_formatted(gc, "WhatsApp QR Code",
        "Scan to Link Device", NULL, msg, NULL, NULL);

    /* Also output to terminal if available (for headless/bitlbee setups) */
    purple_debug_info(PLUGIN_ID, "QR Code: %s\n", qr_data);

    g_free(msg);
}

void bridge_connected(gowhatsapp_account_t account) {
    PurpleAccount *pa = (PurpleAccount *)account;
    PurpleConnection *gc = purple_account_get_connection(pa);
    if (gc == NULL) return;

    purple_connection_set_state(gc, PURPLE_CONNECTED);
    purple_debug_info(PLUGIN_ID, "Connected to WhatsApp\n");
}

void bridge_disconnected(gowhatsapp_account_t account) {
    PurpleAccount *pa = (PurpleAccount *)account;
    PurpleConnection *gc = purple_account_get_connection(pa);
    if (gc == NULL) return;

    purple_connection_error_reason(gc,
        PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
        "Disconnected from WhatsApp");
}

void bridge_error(gowhatsapp_account_t account, const char *message) {
    PurpleAccount *pa = (PurpleAccount *)account;
    PurpleConnection *gc = purple_account_get_connection(pa);
    if (gc == NULL) return;

    purple_debug_error(PLUGIN_ID, "Error: %s\n", message);
    purple_notify_error(gc, "WhatsApp Error", message, NULL);
}

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
) {
    PurpleAccount *pa = (PurpleAccount *)account;

    if (from_me) {
        /* Echoed outgoing message — could display in conversation */
        return;
    }

    if (is_group) {
        /* Group message: find or create the chat conversation */
        PurpleConversation *conv = purple_find_conversation_with_account(
            PURPLE_CONV_TYPE_CHAT, chat_jid, pa);

        if (conv == NULL) {
            /* Auto-join the group chat */
            int chat_id = g_str_hash(chat_jid);
            conv = serv_got_joined_chat(
                purple_account_get_connection(pa), chat_id, chat_jid);
        }

        if (conv != NULL) {
            const char *display = (push_name && push_name[0]) ? push_name : sender_jid;
            serv_got_chat_in(
                purple_account_get_connection(pa),
                purple_conv_chat_get_id(PURPLE_CONV_CHAT(conv)),
                display,
                PURPLE_MESSAGE_RECV,
                text,
                (time_t)timestamp
            );
        }
    } else {
        /* 1:1 message */
        const char *display = (push_name && push_name[0]) ? push_name : sender_jid;

        /* Ensure the buddy exists in the list */
        PurpleBuddy *buddy = purple_find_buddy(pa, sender_jid);
        if (buddy == NULL) {
            buddy = purple_buddy_new(pa, sender_jid, display);
            purple_blist_add_buddy(buddy, NULL, NULL, NULL);
        } else if (push_name && push_name[0]) {
            /* Update display name if we got a push name */
            purple_blist_alias_buddy(buddy, display);
        }

        serv_got_im(
            purple_account_get_connection(pa),
            sender_jid,
            text,
            PURPLE_MESSAGE_RECV,
            (time_t)timestamp
        );
    }
}

void bridge_presence_update(
    gowhatsapp_account_t account,
    const char *jid,
    int available
) {
    PurpleAccount *pa = (PurpleAccount *)account;
    if (available) {
        purple_prpl_got_user_status(pa, jid, "online", NULL);
    } else {
        purple_prpl_got_user_status(pa, jid, "offline", NULL);
    }
}

void bridge_typing_notification(
    gowhatsapp_account_t account,
    const char *jid,
    int composing
) {
    PurpleAccount *pa = (PurpleAccount *)account;
    if (composing) {
        serv_got_typing(purple_account_get_connection(pa), jid,
            0, PURPLE_TYPING);
    } else {
        serv_got_typing_stopped(purple_account_get_connection(pa), jid);
    }
}

/* ────────────────────────────────────────────────────────────────
 * libpurple protocol plugin callbacks
 * ──────────────────────────────────────────────────────────────── */

static const char *wm_list_icon(PurpleAccount *account, PurpleBuddy *buddy) {
    return "whatsapp";
}

static GList *wm_status_types(PurpleAccount *account) {
    GList *types = NULL;
    PurpleStatusType *type;

    type = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE,
        "online", "Online", TRUE, TRUE, FALSE);
    types = g_list_append(types, type);

    type = purple_status_type_new_full(PURPLE_STATUS_AWAY,
        "away", "Away", TRUE, TRUE, FALSE);
    types = g_list_append(types, type);

    type = purple_status_type_new_full(PURPLE_STATUS_OFFLINE,
        "offline", "Offline", TRUE, TRUE, FALSE);
    types = g_list_append(types, type);

    return types;
}

static void wm_login(PurpleAccount *account) {
    PurpleConnection *gc = purple_account_get_connection(account);
    purple_connection_set_state(gc, PURPLE_CONNECTING);

    const char *username = purple_account_get_username(account);
    char *phone = extract_phone(username);

    gowhatsapp_account_t handle = (gowhatsapp_account_t)account;
    int result = gowhatsapp_go_login(handle, phone);

    g_free(phone);

    if (result != 0) {
        purple_connection_error_reason(gc,
            PURPLE_CONNECTION_ERROR_OTHER_ERROR,
            "Failed to initialize WhatsApp connection");
    }
}

static void wm_close(PurpleConnection *gc) {
    PurpleAccount *account = purple_connection_get_account(gc);
    gowhatsapp_account_t handle = (gowhatsapp_account_t)account;
    gowhatsapp_go_logout(handle);
}

static int wm_send_im(PurpleConnection *gc, const char *who,
                       const char *message, PurpleMessageFlags flags) {
    PurpleAccount *account = purple_connection_get_account(gc);
    gowhatsapp_account_t handle = (gowhatsapp_account_t)account;

    /* Strip HTML tags that Pidgin may add */
    char *plain = purple_markup_strip_html(message);

    int result = gowhatsapp_go_send_message(handle, who, plain);
    g_free(plain);

    return (result == 0) ? 1 : -1;
}

static unsigned int wm_send_typing(PurpleConnection *gc, const char *name,
                                    PurpleTypingState state) {
    PurpleAccount *account = purple_connection_get_account(gc);
    gowhatsapp_account_t handle = (gowhatsapp_account_t)account;

    gowhatsapp_go_send_typing(handle, name,
        (state == PURPLE_TYPING) ? 1 : 0);
    return 0;
}

static int wm_chat_send(PurpleConnection *gc, int id,
                         const char *message, PurpleMessageFlags flags) {
    PurpleAccount *account = purple_connection_get_account(gc);
    PurpleConversation *conv = purple_find_chat(gc, id);
    if (conv == NULL) return -1;

    const char *chat_jid = purple_conversation_get_name(conv);
    gowhatsapp_account_t handle = (gowhatsapp_account_t)account;

    char *plain = purple_markup_strip_html(message);
    int result = gowhatsapp_go_send_message(handle, chat_jid, plain);
    g_free(plain);

    return (result == 0) ? 1 : -1;
}

/* ────────────────────────────────────────────────────────────────
 * Plugin registration
 * ──────────────────────────────────────────────────────────────── */

static PurplePluginProtocolInfo prpl_info = {
    .options           = OPT_PROTO_NO_PASSWORD | OPT_PROTO_IM_IMAGE,
    .list_icon         = wm_list_icon,
    .status_types      = wm_status_types,
    .login             = wm_login,
    .close             = wm_close,
    .send_im           = wm_send_im,
    .send_typing       = wm_send_typing,
    .chat_send         = wm_chat_send,
    /* Fields we don't implement yet */
    .list_emblem       = NULL,
    .status_text       = NULL,
    .tooltip_text      = NULL,
    .blist_node_menu   = NULL,
    .chat_info         = NULL,
    .chat_info_defaults= NULL,
    .set_chat_topic    = NULL,
    .get_info          = NULL,
    .set_status        = NULL,
    .add_buddy         = NULL,
    .remove_buddy      = NULL,
    .join_chat         = NULL,
    .reject_chat       = NULL,
    .get_chat_name     = NULL,
    .roomlist_get_list = NULL,
    .struct_size       = sizeof(PurplePluginProtocolInfo),
};

static PurplePluginInfo info = {
    .magic             = PURPLE_PLUGIN_MAGIC,
    .major_version     = PURPLE_MAJOR_VERSION,
    .minor_version     = PURPLE_MINOR_VERSION,
    .type              = PURPLE_PLUGIN_PROTOCOL,
    .priority          = PURPLE_PRIORITY_DEFAULT,
    .id                = PLUGIN_ID,
    .name              = PLUGIN_NAME,
    .version           = PLUGIN_VERSION,
    .summary           = PLUGIN_SUMMARY,
    .description       = "WhatsApp messaging via whatsmeow. "
                         "E2E encrypted using the Signal protocol. "
                         "No third-party servers involved.",
    .author            = PLUGIN_AUTHOR,
    .homepage          = PLUGIN_URL,
    .extra_info        = &prpl_info,
};

static void init_plugin(PurplePlugin *plugin) {
    PurpleAccountOption *option;

    /* Option: send read receipts */
    option = purple_account_option_bool_new(
        "Send read receipts", "send-receipts", TRUE);
    prpl_info.protocol_options = g_list_append(
        prpl_info.protocol_options, option);

    /* Option: auto-download images */
    option = purple_account_option_bool_new(
        "Auto-download images", "auto-download-images", FALSE);
    prpl_info.protocol_options = g_list_append(
        prpl_info.protocol_options, option);

    purple_debug_info(PLUGIN_ID, "WhatsApp (whatsmeow) plugin initialized\n");
}

PURPLE_INIT_PLUGIN(whatsmeow_lite, init_plugin, info)
