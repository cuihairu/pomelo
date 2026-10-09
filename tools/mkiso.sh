#!/bin/sh
# Bundle the kernel ELF into a BIOS-bootable ISO (grub, multiboot).
# Needs grub-mkrescue (grub-pc-bin + xorriso + mtools on debian).
#
# usage: mkiso.sh <kernel-elf> <output.iso>

set -e
kernel="$1"; iso="$2"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir -p "$dir/boot/grub"
cp "$kernel" "$dir/boot/pomelo"
cat > "$dir/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0
menuentry "pomelo" {
    multiboot /boot/pomelo
    boot
}
EOF
grub-mkrescue -o "$iso" "$dir" 2>/dev/null
echo "wrote $iso"
