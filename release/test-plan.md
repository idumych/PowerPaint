# Test plan: PowerPaint

PowerPaint is a document app: it opens, edits and saves pictures. It also runs
commands given with `--do` and prints each one and its result on stdout
(README § Testing), so the feature checks drive it that way and read its own
output, plus one check through the menus.

`{p}` is the platform: `haiku`, `vos` or `linux`. The command is `PowerPaint`
on Haiku (`/boot/system/apps/PowerPaint`) and V\OS (`/system/apps/PowerPaint`),
and the AppImage under Xvfb on Linux. PowerPaint is single-launch: quit every
running copy first, or the commands go to it and its output to another terminal.

## Fixtures

None. The picture is drawn by the app itself and saved to `/tmp`.

## Fresh state

Delete the settings file before launch, and any picture left from a previous run:

- Haiku: `~/config/settings/PowerPaint settings`
- V\OS: `~/config/settings/PowerPaint settings`
- Linux: `~/.config/cosmoe/PowerPaint settings`
- every platform: `/tmp/powerpaint-{p}.png`

## Feature checks

### draw-save-reopen

Steps:

1. Draw and save, then quit:

       PowerPaint --do "send datt width=120 height=80; fg 255 0 0; set strokeSize 1;
         tool pencil; drag 10 20 70 20; pixel 40 20; save /tmp/powerpaint-{p}.png; discard" \
         > dist/V/evidence/{p}-feature.log 2>&1 &

   Wait for `script: discard` in the log, then quit the app: `hey PowerPaint quit`
   (Haiku; `send _QRQ` in the script does **not** quit it), or close its window
   (V\OS, Linux: `xdotool windowclose`). It has nothing unsaved after `discard`.
2. Wait until PowerPaint has quit, then open the file in a new launch. Check that
   it's gone with `roster | grep -i powerpaint` on Haiku, not `ps | grep`
   from an SSH command: that command line itself contains "PowerPaint" and matches.

       PowerPaint --do "open /tmp/powerpaint-{p}.png; dump; pixel 40 20; pixel 40 50; discard" \
         > dist/V/evidence/{p}-reopen.log 2>&1 &

   Wait for `pixel: 40,50` in the log, then quit it as in step 1.

3. Check the file on disk is a PNG: `file /tmp/powerpaint-{p}.png` (Haiku:
   `catattr BEOS:TYPE` shows `image/png`; V\OS has no `file`, use `head -c 8 | od -c`).

PASS when:

- `{p}-feature.log` has `pixel: 40,20 = 255 0 0 255` (the pencil drew red), and
- `{p}-reopen.log` has `script: open /tmp/powerpaint-{p}.png`, `dump: size 120x80`,
  `pixel: 40,20 = 255 0 0 255` (the red line survived the file) and
  `pixel: 40,50 = 255 255 255 255` (the rest stayed white), and no `script: error`.

### about

Steps: launch PowerPaint normally (no `--do`). Open the **Help** menu and choose
**About PowerPaint…**. Take a screenshot, cropped to the about window, as
`dist/V/shots/{p}-about.png`, and its OCR text with
`ocr-find.sh dist/V/shots/{p}-about.png --text > dist/V/evidence/{p}-about.txt`.
Close it (Haiku/Linux: the **OK** button; V\OS: the window's close button or Escape).

What appears differs by platform, on purpose:

- **V\OS:** V\OS's own about window (`BAboutWindow`): the app icon, "PowerPaint",
  the version "0.1.0", the description, "© 2022 Isaiah Odhner (JS Paint)" and
  "2026 the PowerPaint contributors", and the MIT line.
- **Haiku, Linux:** an alert titled "About PowerPaint" with the description, the
  JS Paint copyright and the MIT line, and an **OK** button.

PASS when: `{p}-about.txt` has `Odhner`, and on V\OS also `0.1.0` (the version
`BAboutWindow` reads from the app's resources: it proves the V\OS window, not
the alert, was shown). Look at the screenshot too.
