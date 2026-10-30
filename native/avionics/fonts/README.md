The unmodified `FBW-Display-EIS-A380.ttf` comes from the pinned FlyByWire A380
source at `fbw-a380x/src/fonts/FBW-Display-EIS-A380.ttf`, under the repository's
GPL-3.0 license. The native EWD embeds a generated alpha atlas of this font;
`tools/build_ewd_font.py` regenerates it and records the source hash.

The unmodified `A380X_FCU.ttf` (FBW "Digital" FCU value font) and
`Poppins-SemiBold.ttf` (FCU label font) come from the same pinned source at
`fbw-a380x/src/base/flybywire-aircraft-a380-842/html_ui/Fonts/fbw-a380x/`.
Poppins is distributed by its authors under the SIL Open Font License 1.1.
The native AFS CP LCDs embed a generated alpha atlas of both fonts;
`tools/build_fcu_font.py` regenerates `a380x_fcu_font.h` and records both hashes.

The unmodified `ISISFontTemporary.ttf` (FlyByWire "FBW Display-EIS A320", the `Ecam`
face of `ISISlegacy/style.scss`) comes from the same pinned source at
`fbw-a380x/src/base/flybywire-aircraft-a380-842/html_ui/Fonts/fbw-a380x/`, under the
repository's GPL-3.0 license (its name table: "Licenced under GPLv3"). The native ISIS
embeds a generated alpha atlas of it at the exact drawn texel sizes;
`tools/build_isis_font.py` regenerates `a380x_isis_font.h` and records the font hash
(SHA-256 e936f34e42474d0d084adba49378f672a743e596a9c2e6694f12fce3014e86cc).
