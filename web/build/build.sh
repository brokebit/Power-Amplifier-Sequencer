#!/usr/bin/env bash
set -euo pipefail

# --- Configuration ---
TAILWIND_VERSION="v3.4.17"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WEB_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_DIR="$(dirname "$WEB_DIR")"
STATIC_DIR="$WEB_DIR/static"
DATA_DIR="$PROJECT_DIR/data"
BIN_DIR="$SCRIPT_DIR/.bin"
TAILWIND="$BIN_DIR/tailwindcss"

# --- Tailwind CLI acquisition ---
acquire_tailwind() {
    if [ -x "$TAILWIND" ]; then
        return
    fi

    echo "Downloading Tailwind CSS standalone CLI ${TAILWIND_VERSION}..."
    mkdir -p "$BIN_DIR"

    local os arch binary
    os="$(uname -s)"
    arch="$(uname -m)"

    case "$os" in
        Darwin) os="macos" ;;
        Linux)  os="linux" ;;
        *)      echo "Error: Unsupported OS: $os" >&2; exit 1 ;;
    esac

    case "$arch" in
        arm64|aarch64) arch="arm64" ;;
        x86_64)        arch="x64" ;;
        *)             echo "Error: Unsupported architecture: $arch" >&2; exit 1 ;;
    esac

    binary="tailwindcss-${os}-${arch}"
    local url="https://github.com/tailwindlabs/tailwindcss/releases/download/${TAILWIND_VERSION}/${binary}"

    curl -fsSL -o "$TAILWIND" "$url"
    chmod +x "$TAILWIND"
    echo "Tailwind CLI installed: $TAILWIND"
}

# --- Clean data directory ---
clean_data() {
    echo "Cleaning data/ directory..."
    rm -rf "${DATA_DIR:?}"/*
}

# --- Build CSS with Tailwind ---
build_css() {
    echo "Building CSS with Tailwind..."
    mkdir -p "$DATA_DIR/css"
    # Run from config directory so content paths resolve correctly
    (
        cd "$SCRIPT_DIR"
        "$TAILWIND" \
            -c tailwind.config.js \
            -i input.css \
            -o "$DATA_DIR/css/app.css" \
            --minify
    )
}

# --- Copy static assets ---
copy_assets() {
    echo "Copying static assets..."

    # index.html
    cp "$STATIC_DIR/index.html" "$DATA_DIR/"

    # JavaScript
    if compgen -G "$STATIC_DIR/js/*.js" >/dev/null 2>&1; then
        mkdir -p "$DATA_DIR/js"
        cp "$STATIC_DIR/js/"*.js "$DATA_DIR/js/"
    fi

    # Vendored libraries
    if compgen -G "$STATIC_DIR/lib/*.js" >/dev/null 2>&1; then
        mkdir -p "$DATA_DIR/lib"
        cp "$STATIC_DIR/lib/"*.js "$DATA_DIR/lib/"
    fi

    # Language files
    if compgen -G "$STATIC_DIR/lang/*.json" >/dev/null 2>&1; then
        mkdir -p "$DATA_DIR/lang"
        cp "$STATIC_DIR/lang/"*.json "$DATA_DIR/lang/"
    fi

    # Theme CSS
    if compgen -G "$STATIC_DIR/themes/*.css" >/dev/null 2>&1; then
        mkdir -p "$DATA_DIR/themes"
        cp "$STATIC_DIR/themes/"*.css "$DATA_DIR/themes/"
    fi
}

# --- Gzip all files ---
gzip_all() {
    echo "Compressing files with gzip..."
    find "$DATA_DIR" -type f ! -name '*.gz' -exec gzip -9 {} \;
}

# --- Size report ---
size_report() {
    echo ""
    echo "=== Build output (data/) ==="
    local total=0
    while IFS= read -r file; do
        local size
        size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null)
        total=$((total + size))
        printf "  %6d B  %s\n" "$size" "${file#"$DATA_DIR"/}"
    done < <(find "$DATA_DIR" -type f | sort)
    echo "  ------"
    printf "  %6d B  total (%d KB)\n" "$total" "$((total / 1024))"
    echo ""
}

# --- Main ---
acquire_tailwind
clean_data
build_css
copy_assets
gzip_all
size_report

echo "Build complete."
