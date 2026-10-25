#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Optimized lisgd gesture mapping for Samsung Galaxy A30 (1080x2340 403 PPI)

. sxmo_common.sh

# High-resolution phone thresholds (403 PPI AMOLED)
LISGD_THRESHOLD="${SXMO_LISGD_THRESHOLD:-140}"
LISGD_THRESHOLD_PRESSED="${SXMO_LISGD_THRESHOLD_PRESSED:-90}"
LISGD_INPUT_DEVICE="${SXMO_LISGD_INPUT_DEVICE:-"/dev/input/by-path/first-touchscreen"}"
LISGD_EDGE_SIZE="${LISGD_EDGE_SIZE:-1.5}"

case "$SXMO_WM" in
	dwm|i3)
		case "$(xrandr | grep primary | cut -d' ' -f 5)" in
			right) orientation=1;;
			left) orientation=3;;
			inverted) orientation=2;;
			*) orientation=0;;
		esac
	;;
esac

#-g format:
#   fingers,swipe,edge,distance,command
# MULTI-FINGER FIRST so they are not hijacked by 1-finger triggers
lisgd "$@" -d "$LISGD_INPUT_DEVICE" ${orientation:+-o $orientation} \
	-s "$LISGD_EDGE_SIZE" -t "$LISGD_THRESHOLD" -T "$LISGD_THRESHOLD_PRESSED" \
	-g "3,RL,*,*,setsid -f sxmo_hook_inputhandler.sh threeleft" \
	-g "3,LR,*,*,setsid -f sxmo_hook_inputhandler.sh threeright" \
	-g "3,DU,*,*,setsid -f sxmo_hook_inputhandler.sh threeup" \
	-g "3,UD,*,*,setsid -f sxmo_hook_inputhandler.sh threedown" \
	-g "2,UD,T,*,setsid -f sxmo_hook_inputhandler.sh twodowntopedge" \
	-g "2,UD,B,*,setsid -f sxmo_hook_inputhandler.sh twodownbottomedge" \
	-g "2,RL,*,*,setsid -f sxmo_hook_inputhandler.sh twoleft" \
	-g "2,LR,*,*,setsid -f sxmo_hook_inputhandler.sh tworight" \
	-g "2,DU,*,*,setsid -f sxmo_hook_inputhandler.sh twoup" \
	-g "2,UD,*,*,setsid -f sxmo_hook_inputhandler.sh twodown" \
	-g "1,DRUL,BR,*,setsid -f sxmo_hook_inputhandler.sh bottomrightcorner" \
	-g "1,DLUR,BL,*,setsid -f sxmo_hook_inputhandler.sh bottomleftcorner" \
	-g "1,ULDR,TL,*,setsid -f sxmo_hook_inputhandler.sh topleftcorner" \
	-g "1,URDL,TR,*,setsid -f sxmo_hook_inputhandler.sh toprightcorner" \
	-g "1,LR,B,L,setsid -f sxmo_hook_inputhandler.sh rightbottomedge" \
	-g "1,RL,B,L,setsid -f sxmo_hook_inputhandler.sh leftbottomedge" \
	-g "1,LR,L,*,setsid -f sxmo_hook_inputhandler.sh rightleftedge" \
	-g "1,RL,R,*,setsid -f sxmo_hook_inputhandler.sh leftrightedge" \
	-g "1,DU,L,*,P,setsid -f sxmo_hook_inputhandler.sh upleftedge" \
	-g "1,UD,L,*,P,setsid -f sxmo_hook_inputhandler.sh downleftedge" \
	-g "1,LR,T,*,P,setsid -f sxmo_hook_inputhandler.sh righttopedge" \
	-g "1,RL,T,*,P,setsid -f sxmo_hook_inputhandler.sh lefttopedge" \
	-g "1,DU,B,*,setsid -f sxmo_hook_inputhandler.sh upbottomedge" \
	-g "1,UD,B,*,setsid -f sxmo_hook_inputhandler.sh downbottomedge" \
	-g "1,UD,T,*,setsid -f sxmo_hook_inputhandler.sh downtopedge" \
	-g "1,DU,T,*,setsid -f sxmo_hook_inputhandler.sh uptopedge" \
	-g "1,DU,R,*,P,setsid -f sxmo_hook_inputhandler.sh uprightedge" \
	-g "1,UD,R,*,P,setsid -f sxmo_hook_inputhandler.sh downrightedge" \
	-g "1,LR,R,S,setsid -f sxmo_hook_inputhandler.sh rightrightedge_short" \
	-g "1,RL,L,S,setsid -f sxmo_hook_inputhandler.sh leftrightedge_short" \
	-g "1,RL,*,L,setsid -f sxmo_hook_inputhandler.sh longoneleft" \
	-g "1,LR,*,L,setsid -f sxmo_hook_inputhandler.sh longoneright" \
	-g "1,DU,*,L,setsid -f sxmo_hook_inputhandler.sh longoneup" \
	-g "1,UD,*,L,setsid -f sxmo_hook_inputhandler.sh longonedown" \
	-g "1,RL,*,M,setsid -f sxmo_hook_inputhandler.sh mediumoneleft" \
	-g "1,LR,*,M,setsid -f sxmo_hook_inputhandler.sh mediumoneright" \
	-g "1,DU,*,M,setsid -f sxmo_hook_inputhandler.sh mediumoneup" \
	-g "1,UD,*,M,setsid -f sxmo_hook_inputhandler.sh mediumonedown" \
	-g "1,RL,*,*,setsid -f sxmo_hook_inputhandler.sh oneleft" \
	-g "1,LR,*,*,setsid -f sxmo_hook_inputhandler.sh oneright" \
	-g "1,DU,*,*,setsid -f sxmo_hook_inputhandler.sh oneup" \
	-g "1,UD,*,*,setsid -f sxmo_hook_inputhandler.sh onedown" \
	-g "1,DRUL,*,*,setsid -f sxmo_hook_inputhandler.sh upleft" \
	-g "1,URDL,*,*,setsid -f sxmo_hook_inputhandler.sh downleft" \
	-g "1,DLUR,*,*,setsid -f sxmo_hook_inputhandler.sh upright" \
	-g "1,ULDR,*,*,setsid -f sxmo_hook_inputhandler.sh downright"
