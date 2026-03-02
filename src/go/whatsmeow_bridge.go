// Package main implements the Go side of the libpurple ↔ whatsmeow bridge.
//
// Architecture:
//   C side (plugin.c)  ←→  CGO exports (this file)  ←→  whatsmeow
//
// The C side handles all libpurple API calls (registering the protocol plugin,
// creating buddy lists, handling UI callbacks). This Go side handles all
// WhatsApp protocol logic via whatsmeow. They communicate through:
//   - C→Go: exported Go functions called from C (e.g. gowhatsapp_go_login)
//   - Go→C: C callback functions declared in bridge.h, called from Go via CGO
//
// Security:
//   - All Signal protocol encryption handled by whatsmeow (never exposed to C)
//   - Session DB stored with restricted perms inside purple config dir
//   - QR codes rendered locally, never leave the machine
//   - No third-party servers — direct WebSocket to WhatsApp
package main

/*
#include "bridge.h"
#include <stdlib.h>
*/
import "C"

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"unsafe"

	_ "github.com/mattn/go-sqlite3"
	"go.mau.fi/whatsmeow"
	"go.mau.fi/whatsmeow/proto/waE2E"
	"go.mau.fi/whatsmeow/store/sqlstore"
	"go.mau.fi/whatsmeow/types"
	"go.mau.fi/whatsmeow/types/events"
	waLog "go.mau.fi/whatsmeow/util/log"
	"google.golang.org/protobuf/proto"
)

// accountState holds per-account whatsmeow state.
type accountState struct {
	client    *whatsmeow.Client
	container *sqlstore.Container
	ctx       context.Context
	cancel    context.CancelFunc
}

var (
	mu       sync.Mutex
	accounts = make(map[uintptr]*accountState) // keyed by PurpleAccount pointer
)

// ──────────────────────────────────────────────────────────────────
// Exported functions — called from C
// ──────────────────────────────────────────────────────────────────

//export gowhatsapp_go_login
func gowhatsapp_go_login(account C.gowhatsapp_account_t, phoneC *C.char) C.int {
	phone := C.GoString(phoneC)
	key := uintptr(account)

	mu.Lock()
	defer mu.Unlock()

	if _, exists := accounts[key]; exists {
		return -1 // already logged in
	}

	// Determine DB path inside purple config directory
	home, _ := os.UserHomeDir()
	purpleDir := filepath.Join(home, ".purple", "whatsmeow")
	os.MkdirAll(purpleDir, 0700)
	dbPath := filepath.Join(purpleDir, fmt.Sprintf("%s.db", phone))

	logger := waLog.Stdout("WM", "WARN", true)
	ctx := context.Background()

	container, err := sqlstore.New(ctx, "sqlite3",
		fmt.Sprintf("file:%s?_foreign_keys=on", dbPath), logger)
	if err != nil {
		reportError(account, fmt.Sprintf("DB error: %v", err))
		return -1
	}
	os.Chmod(dbPath, 0600)

	deviceStore, err := container.GetFirstDevice()
	if err != nil {
		reportError(account, fmt.Sprintf("Device store error: %v", err))
		return -1
	}

	client := whatsmeow.NewClient(deviceStore, waLog.Stdout("Client", "WARN", true))

	actx, cancel := context.WithCancel(context.Background())
	state := &accountState{
		client:    client,
		container: container,
		ctx:       actx,
		cancel:    cancel,
	}
	accounts[key] = state

	// Register event handler
	client.AddEventHandler(func(evt interface{}) {
		handleEvent(account, state, evt)
	})

	// Connect
	if client.Store.ID == nil {
		// New login — need QR code
		qrChan, err := client.GetQRChannel(ctx)
		if err != nil {
			reportError(account, fmt.Sprintf("QR channel error: %v", err))
			return -1
		}
		if err := client.Connect(); err != nil {
			reportError(account, fmt.Sprintf("Connect error: %v", err))
			return -1
		}

		go func() {
			for evt := range qrChan {
				switch evt.Event {
				case "code":
					cCode := C.CString(evt.Code)
					C.bridge_show_qr_code(account, cCode)
					C.free(unsafe.Pointer(cCode))
				case "success":
					C.bridge_connected(account)
				case "timeout":
					reportError(account, "QR code timed out — reconnect to retry")
				}
			}
		}()
	} else {
		// Existing session
		if err := client.Connect(); err != nil {
			reportError(account, fmt.Sprintf("Reconnect error: %v", err))
			return -1
		}
	}

	return 0
}

//export gowhatsapp_go_logout
func gowhatsapp_go_logout(account C.gowhatsapp_account_t) {
	key := uintptr(account)

	mu.Lock()
	state, ok := accounts[key]
	if ok {
		delete(accounts, key)
	}
	mu.Unlock()

	if ok && state.client != nil {
		state.cancel()
		state.client.Disconnect()
	}
}

//export gowhatsapp_go_send_message
func gowhatsapp_go_send_message(account C.gowhatsapp_account_t, jidC *C.char, textC *C.char) C.int {
	jidStr := C.GoString(jidC)
	text := C.GoString(textC)
	key := uintptr(account)

	mu.Lock()
	state, ok := accounts[key]
	mu.Unlock()

	if !ok || state.client == nil {
		return -1
	}

	targetJID, err := types.ParseJID(jidStr)
	if err != nil {
		reportError(account, fmt.Sprintf("Invalid JID %q: %v", jidStr, err))
		return -1
	}

	msg := &waE2E.Message{
		Conversation: proto.String(text),
	}

	_, err = state.client.SendMessage(context.Background(), targetJID, msg)
	if err != nil {
		reportError(account, fmt.Sprintf("Send failed: %v", err))
		return -1
	}

	return 0
}

//export gowhatsapp_go_send_typing
func gowhatsapp_go_send_typing(account C.gowhatsapp_account_t, jidC *C.char, typing C.int) {
	jidStr := C.GoString(jidC)
	key := uintptr(account)

	mu.Lock()
	state, ok := accounts[key]
	mu.Unlock()

	if !ok || state.client == nil {
		return
	}

	targetJID, err := types.ParseJID(jidStr)
	if err != nil {
		return
	}

	media := types.ChatPresenceMediaText
	if typing != 0 {
		state.client.SendChatPresence(targetJID, types.ChatPresenceComposing, media)
	} else {
		state.client.SendChatPresence(targetJID, types.ChatPresencePaused, media)
	}
}

//export gowhatsapp_go_mark_read
func gowhatsapp_go_mark_read(account C.gowhatsapp_account_t, jidC *C.char, msgIDC *C.char, senderC *C.char) {
	jidStr := C.GoString(jidC)
	msgID := C.GoString(msgIDC)
	senderStr := C.GoString(senderC)
	key := uintptr(account)

	mu.Lock()
	state, ok := accounts[key]
	mu.Unlock()

	if !ok || state.client == nil || msgID == "" {
		return
	}

	chatJID, _ := types.ParseJID(jidStr)
	senderJID, _ := types.ParseJID(senderStr)

	state.client.MarkRead([]types.MessageID{msgID}, chatJID, senderJID, chatJID)
}

// ──────────────────────────────────────────────────────────────────
// Event handling — dispatches whatsmeow events to C callbacks
// ──────────────────────────────────────────────────────────────────

func handleEvent(account C.gowhatsapp_account_t, state *accountState, evt interface{}) {
	switch v := evt.(type) {
	case *events.Message:
		handleMessage(account, state, v)

	case *events.Connected:
		C.bridge_connected(account)

	case *events.Disconnected:
		C.bridge_disconnected(account)

	case *events.LoggedOut:
		cReason := C.CString(fmt.Sprintf("Logged out: %s", v.Reason))
		C.bridge_error(account, cReason)
		C.free(unsafe.Pointer(cReason))

	case *events.Presence:
		cJID := C.CString(v.From.String())
		available := C.int(0)
		if v.Unavailable == false {
			available = 1
		}
		C.bridge_presence_update(account, cJID, available)
		C.free(unsafe.Pointer(cJID))

	case *events.ChatPresence:
		cJID := C.CString(v.MessageSource.Sender.String())
		composing := C.int(0)
		if v.State == types.ChatPresenceComposing {
			composing = 1
		}
		C.bridge_typing_notification(account, cJID, composing)
		C.free(unsafe.Pointer(cJID))

	case *events.Receipt:
		// Could handle read receipts here
	}
}

func handleMessage(account C.gowhatsapp_account_t, state *accountState, v *events.Message) {
	// Extract text content
	var text string
	if conv := v.Message.GetConversation(); conv != "" {
		text = conv
	} else if ext := v.Message.GetExtendedTextMessage(); ext != nil {
		text = ext.GetText()
	} else if img := v.Message.GetImageMessage(); img != nil {
		text = fmt.Sprintf("[Image] %s", img.GetCaption())
	} else if vid := v.Message.GetVideoMessage(); vid != nil {
		text = fmt.Sprintf("[Video] %s", vid.GetCaption())
	} else if doc := v.Message.GetDocumentMessage(); doc != nil {
		text = fmt.Sprintf("[Document] %s", doc.GetTitle())
	} else if v.Message.GetStickerMessage() != nil {
		text = "[Sticker]"
	} else if v.Message.GetAudioMessage() != nil {
		text = "[Voice Message]"
	} else if reaction := v.Message.GetReactionMessage(); reaction != nil {
		text = fmt.Sprintf("[Reaction: %s]", reaction.GetText())
	} else {
		text = "[Unsupported message type]"
	}

	if text == "" {
		return
	}

	cSenderJID := C.CString(v.Info.Sender.String())
	cChatJID := C.CString(v.Info.Chat.String())
	cText := C.CString(text)
	cMsgID := C.CString(v.Info.ID)
	cPushName := C.CString(v.Info.PushName)
	cTimestamp := C.long(v.Info.Timestamp.Unix())
	cFromMe := C.int(0)
	if v.Info.IsFromMe {
		cFromMe = 1
	}
	cIsGroup := C.int(0)
	if v.Info.IsGroup {
		cIsGroup = 1
	}

	C.bridge_receive_message(account, cSenderJID, cChatJID, cText, cMsgID,
		cPushName, cTimestamp, cFromMe, cIsGroup)

	C.free(unsafe.Pointer(cSenderJID))
	C.free(unsafe.Pointer(cChatJID))
	C.free(unsafe.Pointer(cText))
	C.free(unsafe.Pointer(cMsgID))
	C.free(unsafe.Pointer(cPushName))
}

// reportError sends an error string to the C side.
func reportError(account C.gowhatsapp_account_t, msg string) {
	cMsg := C.CString(msg)
	C.bridge_error(account, cMsg)
	C.free(unsafe.Pointer(cMsg))
}

// main is required for CGO but not actually called — libpurple loads us as a shared lib.
func main() {}
