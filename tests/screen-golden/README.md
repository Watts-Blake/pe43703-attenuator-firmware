# Approved screen pixel fixtures

These immutable 1-bit PNGs specify the Stacked Hero screen, using the
project's MIT-licensed 5×7 font and original lock bitmap. They are design
fixtures, not photographs or physical qualification. Keep the 1×/4× pairs
and their SHA256 manifest together; do not regenerate them from firmware.

`tests/test_screen.py` compiles production C, reaches the five main states
and eight contact-sheet cases through production UI transitions, and compares
every resulting pixel with these images. It crops contact panels at
`(4 + column*132, 4 + row*76)`, each 128×64. Only the surrounding caption
scaffold is reused when assembling the firmware-rendered contact sheet.
No second font or layout renderer is used. Actual firmware outputs and raw
page-major OLED bytes are emitted to `build/screen/` at 1× and 4×.
