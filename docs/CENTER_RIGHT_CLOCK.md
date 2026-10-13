# Center right clock

The center right clock uses the authored `KNOB_CHRONO_GPS` mode knob and the
`KNOB_CHRONO_RUN`, CHR, RST, and DATE controls. Its live readout is drawn in
three separate regions of the native cockpit atlas over the authored CHR,
UTC and ET windows. The readouts use white seven-segment digits, with smaller
seconds/year digits in the UTC window. The datarefs
and commands use the `a380x/xp12/clock/` prefix.

| Mode | Behavior |
| --- | --- |
| GPS (0) | Follows X-Plane's `sim/time/zulu_time_sec` and `sim/time/zulu_date_days`. |
| INT (1) | Advances an internal clock from the last GPS or manual setting. |
| SET (2) | Holds the clock for manual editing. DATE cycles hour, minute, second, day, month, and year; CHR increases the selected field and RST decreases it. |

The date appears while DATE is held. In SET, the date appears when a calendar
field is selected. The selected digit pair flashes; the printed face labels
remain visible around the three apertures. Dates appear as day, month, year,
as specified for this port. Reentering GPS replaces the internal setting with simulator UTC.
X-Plane exposes the UTC day of year and time of day but no UTC year dataref, so
GPS mode uses the host UTC year. A different simulated year can be entered in
SET, then advanced in INT mode.

The ET knob has RUN (0), STP (1), and momentary RST (2) positions. RST clears
ET, blanks its display until RUN is selected, and returns to STP on command
release. CHR starts and stops its own counter; RST clears it while running.
Both counters are initially blank. ET saturates at 99:59; CHR wraps from
99:59 to 00:00. ET and CHR are independent
of the clock's GPS/INT/SET selection. Published values are `mode`,
`et_selector`, `date_held`, `set_field`, `year`, `month`, `day`, `hour`, `minute`,
`second`, `et_seconds`, `chr_seconds`, and `chr_running`. `et_display` and
`chr_display` distinguish an unused/reset blank counter from a displayed zero.
`set_field` runs from 0 (hour) through 5 (year). `chr_held`, `rst_held`, and
`date_held` expose button travel; `mode` and `et_selector` drive the physical
knobs.

Clock power is independent of the avionics master. The native electrical
adapter accepts a live battery or DC bus at 18 V or above; actual battery
voltage models the direct hot-battery feed even when the battery contactor is
open. X-Plane does not expose a dedicated A380 hot-bus dataref, so this remains
an aggregate native-supply adapter. `powered` exposes its state. Total loss of
supply blanks/reset counters and halts INT. GPS resynchronizes when power is
restored. INT, ET, and CHR honor simulation speed and stop during pause/replay.

The native control regression covers calendar rollovers, manual date/time,
counter start/stop/reset, supply loss/redundancy, pause and simulation speed.
The standalone clock render regression checks every aperture's bounds,
white digit color, blanking, and SET flashing. The generated physical mapping
still requires an in-simulator visual check.
