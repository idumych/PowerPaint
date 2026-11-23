#!/bin/sh
# smoke.sh — drives PowerPaint on Haiku through its --do commands and checks
# what it reports: drawing, undo/redo, selections, transforms, saving.
#
#   sh tests/smoke.sh [path/to/PowerPaint]
#
# Quit PowerPaint first: it is single-launch, so a running copy would get
# the commands and this script would never see its output.
APP=${1:-./PowerPaint}
LOG=/tmp/powerpaint-smoke.log
OUT=/tmp/powerpaint-smoke.png
DIR=/tmp/powerpaint-smoke-dir.png

if ps | grep -v grep | grep -q PowerPaint; then
	echo "smoke: quit PowerPaint first" >&2
	exit 1
fi
rm -rf "$OUT" "$DIR"
mkdir "$DIR"

# Picture coordinates; the canvas is cut to 200 x 100, white.
"$APP" --do "fg 0 0 0; bg 255 255 255; set strokeSize 1; set transparent 0;
	send datt width=200 height=100; dump;
	tool pencil; drag 10 10 50 10; pixel 30 10;
	tool 3; fg 255 0 0; click 100 50; pixel 100 50;
	send undo; pixel 100 50; send redo; pixel 100 50;
	tool select; drag 25 5 35 15; drag 30 10 130 10 ctrl; key esc;
	pixel 30 10; pixel 130 10;
	send dflr flip=0; pixel 169 10; pixel 69 10;
	send dflr angle=90.0; dump; send undo;
	tool line; drag 0 90 50 90; send undo; tool text; drag 60 60 120 80;
	type a; send redo; dump;
	save $OUT; open $OUT; dump;
	save $DIR; discard; send datt width=10000 height=10000" > "$LOG" 2>&1 &

# Waits until the log shows $1.
wait_for() {
	i=0
	until grep -q "$1" "$LOG" 2>/dev/null || [ $i -ge 30 ]; do
		sleep 1
		i=$((i + 1))
	done
}
# Too large a picture ends the script with an alert; the running copy
# then takes a second script.
wait_for "script: send datt width=10000"
sleep 1
"$APP" --do "pixel 300 300; done"
wait_for "script: done"
hey PowerPaint quit > /dev/null 2>&1

fail=0
expect() {
	if grep -qF "$1" "$LOG"; then
		echo "ok    $1"
	else
		echo "FAIL  $1"
		fail=1
	fi
}
expect "dump: size 200x100"
expect "pixel: 30,10 = 0 0 0 255"			# pencil
expect "pixel: 100,50 = 255 0 0 255"		# fill, and again after redo
expect "pixel: 100,50 = 255 255 255 255"	# undo
expect "pixel: 130,10 = 0 0 0 255"			# Ctrl-drag left a copy
expect "pixel: 169,10 = 0 0 0 255"			# flip
expect "pixel: 69,10 = 0 0 0 255"
expect "dump: size 100x200"					# rotate
expect "tool Text selection no text no"		# redo with a text box: no crash
expect "modified no"						# saved and reopened
expect "script: error: save $DIR"			# can't save over a folder...
[ -d "$DIR" ] && echo "ok    folder untouched" || { echo "FAIL  folder"; fail=1; }
ls -a /tmp | grep -q powerpaint-tmp \
	&& { echo "FAIL  temporary file left"; fail=1; } || echo "ok    no temporary file"
expect "pixel: 300,300 = 0 0 0 0"			# 10000 x 10000 refused
expect "script: done"						# ...and still running

rm -rf "$OUT" "$DIR"
[ $fail = 0 ] && echo "smoke: all passed" || echo "smoke: FAILED, see $LOG"
exit $fail
