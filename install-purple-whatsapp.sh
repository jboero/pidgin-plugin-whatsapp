#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────
# install-purple-whatsapp.sh
# One-shot builder & installer for the purple-gowhatsapp Pidgin plugin.
# Supports: Ubuntu/Debian, Fedora, Arch Linux
#
# Usage: bash install-purple-whatsapp.sh [--user|--system]
#   --user   Install to ~/.purple/plugins (no sudo, default)
#   --system Install to system libpurple plugin dir (needs sudo)
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

INSTALL_MODE="${1:---user}"
BUILD_DIR="$(mktemp -d)"
#trap 'rm -rf "$BUILD_DIR"' EXIT

# ── Detect distro ──────────────────────────────────────────────────
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif command -v lsb_release &>/dev/null; then
        lsb_release -si | tr '[:upper:]' '[:lower:]'
    else
        echo "unknown"
    fi
}

DISTRO=$(detect_distro)
info "Detected distribution: $DISTRO"

# ── Install dependencies ──────────────────────────────────────────
install_deps() {
    info "Installing build dependencies..."
    case "$DISTRO" in
        ubuntu|debian|pop|linuxmint|elementary)
            sudo apt-get update
            sudo apt-get install -y \
                git golang cmake make pkg-config gcc \
                pidgin libpurple-dev \
                libgdk-pixbuf2.0-dev \
                libopusfile-dev \
                webp-pixbuf-loader
            ;;
        fedora)
            sudo dnf install -y \
                git golang cmake make pkg-config gcc \
                pidgin libpurple-devel \
                gdk-pixbuf2-devel \
                opusfile-devel \
                webp-pixbuf-loader
            ;;
        arch|manjaro|endeavouros)
            sudo pacman -S --needed --noconfirm \
                git go cmake make pkg-config gcc \
                pidgin libpurple \
                gdk-pixbuf2 \
                opusfile \
                webp-pixbuf-loader
            ;;
        *)
            warn "Unknown distro '$DISTRO'. Attempting to continue — you may need to install deps manually."
            warn "Required: git, go (1.21+), cmake, make, pkg-config, gcc, pidgin, libpurple-dev, libgdk-pixbuf2.0-dev, libopusfile-dev"
            ;;
    esac
}

# ── Check Go version ──────────────────────────────────────────────
check_go() {
    if ! command -v go &>/dev/null; then
        error "Go is not installed. Please install Go 1.21+ first."
    fi
    GO_VER=$(go version | grep -oP '\d+\.\d+' | head -1)
    GO_MAJOR=$(echo "$GO_VER" | cut -d. -f1)
    GO_MINOR=$(echo "$GO_VER" | cut -d. -f2)
    if [ "$GO_MAJOR" -lt 1 ] || ([ "$GO_MAJOR" -eq 1 ] && [ "$GO_MINOR" -lt 21 ]); then
        error "Go $GO_VER is too old. Need Go 1.21+."
    fi
    info "Go version: $GO_VER ✓"
}

# ── Clone and build ───────────────────────────────────────────────
build_plugin() {
    info "Cloning purple-gowhatsapp (whatsmeow branch)..."
    cd "$BUILD_DIR"
    git clone --recurse-submodules --depth=1 \
        https://github.com/hoehermann/purple-gowhatsapp.git \
        purple-whatsmeow

    cd purple-whatsmeow

    # Use fresh go.mod for bleeding-edge compatibility
    rm -f go.mod go.sum

    info "Configuring with CMake..."
    mkdir -p build && cd build

    if [ "$INSTALL_MODE" = "--user" ]; then
        cmake -G "Unix Makefiles" \
            -DPURPLE_DATA_DIR:PATH="$HOME/.local/share" \
            -DPURPLE_PLUGIN_DIR:PATH="$HOME/.purple/plugins" \
            ..
    else
        cmake -G "Unix Makefiles" ..
    fi

    info "Building (this may take a few minutes on first run — Go downloads modules)..."
    cmake --build . -j"$(nproc)"
    info "Build complete ✓"
}

# ── Install ───────────────────────────────────────────────────────
install_plugin() {
    cd "$BUILD_DIR/purple-whatsmeow/build"

    if [ "$INSTALL_MODE" = "--user" ]; then
        info "Installing to ~/.purple/plugins/ ..."
        cmake --install . --strip
        # Also copy protocol icons
        mkdir -p "$HOME/.local/share/pixmaps/pidgin/protocols/"
        cp -r "$BUILD_DIR/purple-whatsmeow/assets/pixmaps/pidgin/protocols/"* \
            "$HOME/.local/share/pixmaps/pidgin/protocols/" 2>/dev/null || true
    else
        info "Installing system-wide (requires sudo)..."
        sudo cmake --install . --strip
    fi
}

# ── Post-install ──────────────────────────────────────────────────
post_install() {
    echo ""
    echo -e "${GREEN}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN} WhatsApp plugin for Pidgin installed successfully!${NC}"
    echo -e "${GREEN}════════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo "  Next steps:"
    echo "  1. Launch Pidgin"
    echo "  2. Accounts → Manage Accounts → Add"
    echo "  3. Protocol: select 'whatsmeow'"
    echo "  4. Username: your phone number with country code"
    echo "     e.g. 6512345678@s.whatsapp.net (Singapore)"
    echo "  5. Click 'Add', then scan the QR code with your phone"
    echo "     (WhatsApp → Settings → Linked Devices → Link a Device)"
    echo ""
    echo "  Security notes:"
    echo "  • E2E encryption is maintained (Signal protocol via whatsmeow)"
    echo "  • Session data stored in ~/.purple/ (standard Pidgin location)"
    echo "  • No third-party servers — direct connection to WhatsApp"
    echo ""

    if [ "$INSTALL_MODE" = "--user" ]; then
        echo "  Plugin location: ~/.purple/plugins/libwhatsmeow.so"
    else
        SYSDIR=$(pkg-config --variable=plugindir purple 2>/dev/null || echo "/usr/lib/purple-2")
        echo "  Plugin location: $SYSDIR/libwhatsmeow.so"
    fi
    echo ""
}

# ── Main ──────────────────────────────────────────────────────────
main() {
    echo ""
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║  WhatsApp Plugin for Pidgin — Automated Installer       ║"
    echo "║  Based on purple-gowhatsapp by hoehermann               ║"
    echo "║  Powered by whatsmeow (Signal protocol E2E encryption)  ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""

    install_deps
    check_go
    build_plugin
    install_plugin
    post_install
}

main "$@"
