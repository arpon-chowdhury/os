#!/bin/sh
# Explicit, project-local download only. No package installation or cleanup.
set -eu
cd "$(dirname "$0")/.."
version=12.9.3
sha256=9f42fe9ea2e84d71056529969b1d5c24ba9a2287ffcc01bcce58b0ef8aa98b1d
archive="build/downloads/limine-${version}-binary.tar.gz"
if [ -e vendor/limine ]; then
    echo 'vendor/limine already exists; refusing to overwrite it.' >&2
    exit 1
fi
mkdir -p build/downloads vendor
curl --fail --location --proto '=https' --tlsv1.2 \
    "https://github.com/Limine-Bootloader/Limine/releases/download/v${version}/limine-binary.tar.gz" \
    -o "$archive"
printf '%s  %s\n' "$sha256" "$archive" | shasum -a 256 -c -
mkdir vendor/limine
tar -xzf "$archive" --strip-components=1 -C vendor/limine
for file in limine.c limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin BOOTX64.EFI; do
    test -f "vendor/limine/$file" || { echo "Missing Limine file: $file" >&2; exit 1; }
done
printf 'Obtained Limine %s; nothing installed system-wide.\n' "$version"
