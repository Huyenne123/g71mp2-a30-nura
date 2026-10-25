#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright 2022 Sxmo Contributors

# We still use dmenu in dwm|worgs cause pointer/touch events
# are not implemented yet in the X11 library of bemenu

# Note: Only pass parameters to this script that are unambiguous across all
# supported implementations! (dmenu, wofi, dmenu), which are only:

# --show-over-lockscreen
# -p PROMPT
# -i            (case insensitive)

# -- options need to proceed - options as these are handled by sxmo_dmenu.sh
# directly.

# include common definitions
# shellcheck source=scripts/core/sxmo_common.sh
. sxmo_common.sh

#prevent infinite recursion:
unalias bemenu
unalias dmenu

if [ -z "$SXMO_MENU" ]; then
	case "$SXMO_WM" in
		sway|river)
			SXMO_MENU=bemenu
			;;
		dwm|i3|*)
			SXMO_MENU=dmenu
			;;
	esac
fi

# Need to pass any options first before menu args
case "$1" in
	"--show-over-lockscreen")
		SHOW_OVER_LOCKSCREEN_FLAG=1
		shift
		;;
	"isopen")
		exec pgrep "$SXMO_MENU" >/dev/null
		;;
	"close")
		if pgrep "$SXMO_MENU" >/dev/null; then
			exec pkill "$SXMO_MENU" >/dev/null
		else
			exit
		fi
		;;
esac

if [ -n "$WAYLAND_DISPLAY" ]; then
	case "$SXMO_WM" in
		sway)
			if sxmo_state.sh get | grep -q unlock; then
				swaymsg mode menu -q # disable default button inputs
				cleanmode() {
					swaymsg mode default -q
				}
				trap 'cleanmode' TERM INT
			fi
			;;
		river)
			if sxmo_state.sh get | grep -q unlock; then
				riverctl enter-mode menu # disable default button inputs
				cleanmode() {
					riverctl enter-mode normal
				}
				trap 'cleanmode' TERM INT
			fi
			;;
	esac
fi

wofi_wrapper() {
	#let wofi handle the number of lines dynamically
	# (wofi is a bit confused after rotating to horizontal mode though)
	if [ "$SXMO_WOFI_SMALLSCREEN" = "0" ]; then
		wofi -k /dev/null "$@"
	else
		# shellcheck disable=SC2046
		#  (not quoted because we want to split args here)
		wofi -k /dev/null $(sxmo_rotate.sh isrotated > /dev/null && echo -W "${SXMO_WOFI_LANDSCAPE_WIDTH:-640}" -H "${SXMO_WOFI_LANDSCAPE_HEIGHT:-200}" -l top) "$@"
	fi
}

if [ -n "$WAYLAND_DISPLAY" ] || [ -n "$DISPLAY" ]; then
	case "$SXMO_MENU" in
		bemenu)
			bemenu -l "$(sxmo_rotate.sh isrotated > /dev/null && \
				printf %s "${SXMO_BEMENU_LANDSCAPE_LINES:-8}" || \
				printf %s "${SXMO_BEMENU_PORTRAIT_LINES:-16}")" "$@"
			returned=$?

			[ -n "$WAYLAND_DISPLAY" ] && cleanmode
			exit "$returned"
			;;
		wofi)
			picked="$(wofi_wrapper "$@")"
			returned=$?

			cleanmode

			if [ -z "$picked" ]; then
				exit 1
			else
				printf "%s\n" "$picked"
				exit "$returned"
			fi
			;;
		dmenu)
			# i3 and bspwm need the return focus as this commit 1d21a2d6 in sxmo-dmenu
			# causes the event to be consumed by dmenu and not used to return focus
			# dwm doesn't get affected by it.
			# How to reproduce: use sxmo_dmenu.sh whilst looking at a terminal window.
			# After it will show unfocused, firefox was also affected.
			# Standard dmenu isn't affected by this, unsure how to fix.
			FOCUSED_WINDOW=$(xdotool getwindowfocus)
			ID=$(xprop -id "$FOCUSED_WINDOW" WM_CLASS)
			case "$ID" in
				*"dmenu"*|*"not found"*) FOCUSED_WINDOW= ;;
			esac
			if [ -n "$SHOW_OVER_LOCKSCREEN_FLAG" ]; then
				# List of screenlockers that are running to check for embedding
				if pgrep smlock >/dev/null; then
					set -- "$@" -w smlock
				fi
			fi
			# SXMO_DMENU_OPTS may contain multiple arguments, so we want it to be split.
			# shellcheck disable=SC2086
			dmenu $SXMO_DMENU_OPTS -l "$(sxmo_rotate.sh isrotated > /dev/null && \
				printf %s "${SXMO_DMENU_LANDSCAPE_LINES:-5}" || \
				printf %s "${SXMO_DMENU_PORTRAIT_LINES:-12}")" "$@"
			EXIT_STATUS="$?"
			case "$FOCUSED_WINDOW" in
				"") exit "$EXIT_STATUS" ;;
				*)
					xdotool windowfocus "$FOCUSED_WINDOW"
					exit "$EXIT_STATUS"

			esac
			;;
	esac
else
	#fallback to tty menu (e.g. over ssh)
	export BEMENU_BACKEND=curses
	exec bemenu -w "$@"
fi
