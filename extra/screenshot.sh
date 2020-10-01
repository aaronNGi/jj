#!/bin/sh
#
# Creates the example screenshot for README.md.
# Dependencies: st, xdotool, imagemagick.

jjp=../jjp
outfile=../jjirc.png
spacing=3
cols=80
rows=24

realcols=$((cols + 4 * spacing))
realrows=$((rows + 2 * spacing))

[ -z "$JJ_SCROT" ] && {
	JJ_SCROT=1 exec st -g "${realcols}x${realrows}+0+0" -e "$0" "$@"
}

# Clear screen and hide cursor.
printf '\033[H\033[J\033[?25l'

{
cat <<-EOF
	==> irc/irc.test.org/#test.log <==
	1601460000 <-> Channel opened
	1601460001 <-> jjuser [~user@host] has joined #test
	1601460121 <jjuser>* this is us
	1601460121 <jjuser>* successive messages from the same user are grouped
	1601460180 <bob> Hello from another user
	1601460180 <bob> different nicks have unique colors
	1601503200 <alice1> the date is shown when it changed
	1601503200 <alice1>! jjuser: this is how it looks when your nick is mentioned
	1601503200 <alice1> (highlight)
	1601503200 <-> alice1 [~alice@host] has changed nick to alice

	==> irc/irc.test.org/#anotherchan.log <==
	1601506533 <jjuser>* say cheese for the screenshot
	1601506593 <bob>a says cheese
EOF
} \
| awk -f "$jjp" \
| tail -n"-$rows" \
| awk -vspacing="$spacing" '
	function repeat(char, amount,   str) {
		str = sprintf("%" amount "s", "")
		gsub(/ /, char, str)
		return str
	}
	BEGIN {
		printf "%s", repeat("\n", spacing)
		pre = repeat(" ", spacing * 2)
	} {
		gsub(/\a/, "")
		printf "%s%s\n", pre, $0
	} END {
		# Pane border.
		printf "%s\033[38;5;8m%s\033[0m\n",
			pre, repeat("─", 80)

		# Input.
		printf "%s\033[37m%s\033[0m> %s\033[42m \033[0m\n",
			pre, "#test", "Lorem ipsum"

		# Window list.
		printf "%s 1  \033[30;47m 2 \033[0m \033[30;41m 3 \033[0m\n",
			repeat(" ", spacing * 2 + 80 / 2 - 6)
	}
'
printf 'Press enter to take a screenshot of this window or press Ctrl-C to cancel'
read || exit

# Move cursor up one line and clear to end of screen.
printf '\033[1A\033[K'

import -window "$(xdotool getwindowfocus -f)" "$outfile"

printf 'Done\nPress enter to close this window'
read
