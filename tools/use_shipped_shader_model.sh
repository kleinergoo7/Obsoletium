#!/bin/bash
# Point the game at the CS:S 2024 shader model instead of the tree's.
#
# Why: the shipped .vcs files in the game content are 2024-numbered, but the
# tree's stdshader_dx9 is built from this tree's .fxc/.inc, which number the
# combos differently (vs20: 192 dynamic combos here vs 384 in the file). The
# static index the model produces is stride * comboNumber, and the device
# resolves it as staticIndex / fileDynamicCombos, so the two must agree.
# The 2024 model is the one built against the 2024 .vcs.
#
# The 2024 model is a prebuilt module, so the engine has to answer the
# interface it asks for: "VMaterialSystem082" (see the GetShaderAPIKind()
# slot in public/materialsystem/imaterialsystem.h).
#
# Usage: tools/use_shipped_shader_model.sh [on|off]
#        GAME_CLIENT_DIR=<dir> overrides <repo>/game_client.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${GAME_CLIENT_DIR:-$REPO/game_client}/bin/linux64"
SHIPPED="$REPO/css-steam-assets/bin/linux64/stdshader_dx9.so"
BUILT="$REPO/build-client/stdshader_dx9.so"

case "${1:-on}" in
on)
	[ -f "$SHIPPED" ] || { echo "missing $SHIPPED"; exit 1; }
	# Copy, do not symlink: the probe harness repoints every *.so symlink in
	# this directory at the build tree.
	rm -f "$BIN/stdshader_dx9.so" "$BIN/libstdshader_dx9.so"
	cp -f "$SHIPPED" "$BIN/stdshader_dx9.so"
	cp -f "$SHIPPED" "$BIN/libstdshader_dx9.so"
	echo "using the CS:S 2024 shader model"
	;;
off)
	rm -f "$BIN/stdshader_dx9.so" "$BIN/libstdshader_dx9.so"
	ln -sfn "$BUILT" "$BIN/stdshader_dx9.so"
	ln -sfn "$REPO/build-client/libstdshader_dx9.so" "$BIN/libstdshader_dx9.so"
	echo "using the tree's shader model"
	;;
*)
	echo "usage: $0 [on|off]"; exit 1;;
esac
