/*
 * Haptic evaluation firmware -- bench-only, standalone from the product
 * remote build. Purpose: feel the DRV2605L's real ROM library effects on
 * each candidate actuator, correctly configured and calibrated for that
 * actuator, so the motor choice and the waveform choices for
 * remote/src/haptic.c's effect_single[] table can be made by ear instead of
 * guessed. No BLE, no provisioning, no OTP command -- OTP is deliberately
 * not exposed here; drv2605_burn_otp() is irreversible per physical chip
 * and this firmware's whole point is repeated reconfiguration across
 * mounts and motors, not committing one. Everything below lives in
 * registers only and dies at the next power cycle, which is what makes
 * swapping actuators on one DRV2605L breakout safe.
 *
 * Shell commands (over the DK's on-board UART/J-Link VCOM, already the
 * board's default console):
 *   haptic act [name]     -- select the actuator on the output, or list
 *   haptic cal [mv [mv]]  -- calibrate for the selected actuator
 *   haptic play <id>      -- play one ROM library effect (1-123)
 *   haptic seq <id...>    -- play up to 7 effects as one sequence
 *   haptic list           -- print all 123 effect names
 *   haptic lib <0-7>      -- override the ROM library
 *   haptic loop <o|c>     -- override open/closed-loop drive
 *   haptic measure        -- VDD under load, and the LRA's real resonance
 *   haptic diag / dump / reset
 */
#include "drv2605.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Registers this firmware reads directly for diagnostics. drv2605.c owns the
 * full map; these are duplicated here rather than exported because a bench
 * register dump is not a driver API -- if the dump and the driver ever
 * disagree about an address, that is a bench bug, not a product one. */
#define REG_VBAT       0x21
#define REG_LRA_PERIOD 0x22

/*
 * ROM library effect names, datasheets/drv2605l.pdf S12.1.2. Indexed by
 * effect ID (1-123, matching the register value drv2605_play_sequence()
 * writes) so lookup is a direct array access; index 0 is unused (there is
 * no effect 0 -- 0 is the WAV_FRM_SEQ terminator, drv2605.c's own comment).
 *
 * One list, all libraries: S12.1.2 names the effects once and every library
 * -- the five ERM ones, the LRA one, and F -- is that same set of effects
 * re-tuned for a different actuator class. So "effect 4, Sharp Click" means
 * the same intent on the LRA as on the ERM, which is what makes an
 * effect-by-effect comparison across actuators meaningful at all.
 */
static const char *const EFFECT_NAME[124] = {
	[1] = "Strong Click - 100%",
	[2] = "Strong Click - 60%",
	[3] = "Strong Click - 30%",
	[4] = "Sharp Click - 100%",
	[5] = "Sharp Click - 60%",
	[6] = "Sharp Click - 30%",
	[7] = "Soft Bump - 100%",
	[8] = "Soft Bump - 60%",
	[9] = "Soft Bump - 30%",
	[10] = "Double Click - 100%",
	[11] = "Double Click - 60%",
	[12] = "Triple Click - 100%",
	[13] = "Soft Fuzz - 60%",
	[14] = "Strong Buzz - 100%",
	[15] = "750 ms Alert 100%",
	[16] = "1000 ms Alert 100%",
	[17] = "Strong Click 1 - 100%",
	[18] = "Strong Click 2 - 80%",
	[19] = "Strong Click 3 - 60%",
	[20] = "Strong Click 4 - 30%",
	[21] = "Medium Click 1 - 100%",
	[22] = "Medium Click 2 - 80%",
	[23] = "Medium Click 3 - 60%",
	[24] = "Sharp Tick 1 - 100%",
	[25] = "Sharp Tick 2 - 80%",
	[26] = "Sharp Tick 3 - 60%",
	[27] = "Short Double Click Strong 1 - 100%",
	[28] = "Short Double Click Strong 2 - 80%",
	[29] = "Short Double Click Strong 3 - 60%",
	[30] = "Short Double Click Strong 4 - 30%",
	[31] = "Short Double Click Medium 1 - 100%",
	[32] = "Short Double Click Medium 2 - 80%",
	[33] = "Short Double Click Medium 3 - 60%",
	[34] = "Short Double Sharp Tick 1 - 100%",
	[35] = "Short Double Sharp Tick 2 - 80%",
	[36] = "Short Double Sharp Tick 3 - 60%",
	[37] = "Long Double Sharp Click Strong 1 - 100%",
	[38] = "Long Double Sharp Click Strong 2 - 80%",
	[39] = "Long Double Sharp Click Strong 3 - 60%",
	[40] = "Long Double Sharp Click Strong 4 - 30%",
	[41] = "Long Double Sharp Click Medium 1 - 100%",
	[42] = "Long Double Sharp Click Medium 2 - 80%",
	[43] = "Long Double Sharp Click Medium 3 - 60%",
	[44] = "Long Double Sharp Tick 1 - 100%",
	[45] = "Long Double Sharp Tick 2 - 80%",
	[46] = "Long Double Sharp Tick 3 - 60%",
	[47] = "Buzz 1 - 100%",
	[48] = "Buzz 2 - 80%",
	[49] = "Buzz 3 - 60%",
	[50] = "Buzz 4 - 40%",
	[51] = "Buzz 5 - 20%",
	[52] = "Pulsing Strong 1 - 100%",
	[53] = "Pulsing Strong 2 - 60%",
	[54] = "Pulsing Medium 1 - 100%",
	[55] = "Pulsing Medium 2 - 60%",
	[56] = "Pulsing Sharp 1 - 100%",
	[57] = "Pulsing Sharp 2 - 60%",
	[58] = "Transition Click 1 - 100%",
	[59] = "Transition Click 2 - 80%",
	[60] = "Transition Click 3 - 60%",
	[61] = "Transition Click 4 - 40%",
	[62] = "Transition Click 5 - 20%",
	[63] = "Transition Click 6 - 10%",
	[64] = "Transition Hum 1 - 100%",
	[65] = "Transition Hum 2 - 80%",
	[66] = "Transition Hum 3 - 60%",
	[67] = "Transition Hum 4 - 40%",
	[68] = "Transition Hum 5 - 20%",
	[69] = "Transition Hum 6 - 10%",
	[70] = "Transition Ramp Down Long Smooth 1 - 100 to 0%",
	[71] = "Transition Ramp Down Long Smooth 2 - 100 to 0%",
	[72] = "Transition Ramp Down Medium Smooth 1 - 100 to 0%",
	[73] = "Transition Ramp Down Medium Smooth 2 - 100 to 0%",
	[74] = "Transition Ramp Down Short Smooth 1 - 100 to 0%",
	[75] = "Transition Ramp Down Short Smooth 2 - 100 to 0%",
	[76] = "Transition Ramp Down Long Sharp 1 - 100 to 0%",
	[77] = "Transition Ramp Down Long Sharp 2 - 100 to 0%",
	[78] = "Transition Ramp Down Medium Sharp 1 - 100 to 0%",
	[79] = "Transition Ramp Down Medium Sharp 2 - 100 to 0%",
	[80] = "Transition Ramp Down Short Sharp 1 - 100 to 0%",
	[81] = "Transition Ramp Down Short Sharp 2 - 100 to 0%",
	[82] = "Transition Ramp Up Long Smooth 1 - 0 to 100%",
	[83] = "Transition Ramp Up Long Smooth 2 - 0 to 100%",
	[84] = "Transition Ramp Up Medium Smooth 1 - 0 to 100%",
	[85] = "Transition Ramp Up Medium Smooth 2 - 0 to 100%",
	[86] = "Transition Ramp Up Short Smooth 1 - 0 to 100%",
	[87] = "Transition Ramp Up Short Smooth 2 - 0 to 100%",
	[88] = "Transition Ramp Up Long Sharp 1 - 0 to 100%",
	[89] = "Transition Ramp Up Long Sharp 2 - 0 to 100%",
	[90] = "Transition Ramp Up Medium Sharp 1 - 0 to 100%",
	[91] = "Transition Ramp Up Medium Sharp 2 - 0 to 100%",
	[92] = "Transition Ramp Up Short Sharp 1 - 0 to 100%",
	[93] = "Transition Ramp Up Short Sharp 2 - 0 to 100%",
	[94] = "Transition Ramp Down Long Smooth 1 - 50 to 0%",
	[95] = "Transition Ramp Down Long Smooth 2 - 50 to 0%",
	[96] = "Transition Ramp Down Medium Smooth 1 - 50 to 0%",
	[97] = "Transition Ramp Down Medium Smooth 2 - 50 to 0%",
	[98] = "Transition Ramp Down Short Smooth 1 - 50 to 0%",
	[99] = "Transition Ramp Down Short Smooth 2 - 50 to 0%",
	[100] = "Transition Ramp Down Long Sharp 1 - 50 to 0%",
	[101] = "Transition Ramp Down Long Sharp 2 - 50 to 0%",
	[102] = "Transition Ramp Down Medium Sharp 1 - 50 to 0%",
	[103] = "Transition Ramp Down Medium Sharp 2 - 50 to 0%",
	[104] = "Transition Ramp Down Short Sharp 1 - 50 to 0%",
	[105] = "Transition Ramp Down Short Sharp 2 - 50 to 0%",
	[106] = "Transition Ramp Up Long Smooth 1 - 0 to 50%",
	[107] = "Transition Ramp Up Long Smooth 2 - 0 to 50%",
	[108] = "Transition Ramp Up Medium Smooth 1 - 0 to 50%",
	[109] = "Transition Ramp Up Medium Smooth 2 - 0 to 50%",
	[110] = "Transition Ramp Up Short Smooth 1 - 0 to 50%",
	[111] = "Transition Ramp Up Short Smooth 2 - 0 to 50%",
	[112] = "Transition Ramp Up Long Sharp 1 - 0 to 50%",
	[113] = "Transition Ramp Up Long Sharp 2 - 0 to 50%",
	[114] = "Transition Ramp Up Medium Sharp 1 - 0 to 50%",
	[115] = "Transition Ramp Up Medium Sharp 2 - 0 to 50%",
	[116] = "Transition Ramp Up Short Sharp 1 - 0 to 50%",
	[117] = "Transition Ramp Up Short Sharp 2 - 0 to 50%",
	[118] = "Long buzz for programmatic stopping - 100%",
	[119] = "Smooth Hum 1 (No kick or brake pulse) - 50%",
	[120] = "Smooth Hum 2 (No kick or brake pulse) - 40%",
	[121] = "Smooth Hum 3 (No kick or brake pulse) - 30%",
	[122] = "Smooth Hum 4 (No kick or brake pulse) - 20%",
	[123] = "Smooth Hum 5 (No kick or brake pulse) - 10%",
};

static const char *effect_name(uint8_t id)
{
	if (id >= ARRAY_SIZE(EFFECT_NAME) || EFFECT_NAME[id] == NULL) {
		return "(unknown)";
	}
	return EFFECT_NAME[id];
}

/*
 * ---------------------------------------------------------------------------
 * The three actuators on the bench.
 * ---------------------------------------------------------------------------
 *
 * A profile is written in the units the *motor's* datasheet uses -- volts and
 * a resonance frequency -- not in register codes, because that is the form in
 * which the numbers can be checked against the PDF sitting next to the bench.
 * profile_actuator() below does the conversion, and it is the only place the
 * DRV2605L's equations appear, so `haptic cal 1600` converts a bench override
 * through exactly the same arithmetic as the datasheet default does.
 *
 * The ERM and the two LRAs are not the same device with a different number in
 * it. An LRA needs the LRA ROM library (6), LRA mode in FEEDBACK_CONTROL,
 * closed-loop auto-resonance, a DRIVE_TIME matched to its own resonance, and
 * a *different equation* for RATED_VOLTAGE -- get any one of those wrong and
 * the actuator still buzzes, just weakly and off-resonance, which is exactly
 * the failure that reads as "this LRA is worse than the ERM".
 */
struct eval_profile {
	const char *key;      /* what you type after `haptic act` */
	const char *part;     /* the part number on the datasheet */
	const char *note;
	enum drv2605_actuator_kind kind;
	uint16_t rated_mv;    /* ERM: steady-state average. LRA: RMS. */
	uint16_t od_mv;       /* ERM: average full scale. LRA: peak. */
	uint16_t f0_hz;       /* LRA resonance; 0 for the ERM */
	uint16_t rated_scale; /* Eq. 5's sqrt term x1000 -- see profile_actuator() */
	uint8_t library;
	bool open_loop;
};

static const struct eval_profile PROFILES[] = {
	{
		.key = "erm",
		.part = "Vybronics VZ7AL2B1690002 (ERM)",
		.note = "rated 3.0V, operating 2.2-3.6V, 250 mA max",
		.kind = DRV2605_ERM,
		/* Eq. 4 and Eq. 6 -- the product's own targets, derivation in
		 * drv2605.c above RATED_VOLTAGE_TARGET. */
		.rated_mv = 3000,
		.od_mv = 3600,
		/* Library B, not the reset default A the product still ships:
		 * Table 1 buckets the ERM libraries by the motor's rated
		 * voltage, and A is the 1.3-V bucket while B-E are the 3-V
		 * ones. `haptic lib 1` puts it back to what every ERM effect
		 * felt on this project so far was actually played through, and
		 * that A/B comparison is worth making deliberately rather than
		 * inheriting. B/C/D/E differ only in the rise time they assume
		 * (40-60 / 60-80 / 100-140 / >140 ms); this motor's datasheet
		 * states no rise time at all, so which of them fits is a
		 * question only the bench can answer. */
		.library = 2,
		/* The ERM libraries were designed for open-loop drive and
		 * ERM_OPEN_LOOP resets to 1 to match. Worth flipping with
		 * `haptic loop closed` while comparing: closed loop adds
		 * automatic overdrive and braking, which is most of what makes
		 * an LRA feel crisp, and it is the only way the ERM competes
		 * on sharpness. Note it also changes which register does
		 * anything -- open loop ignores RATED_VOLTAGE entirely and
		 * takes full scale from OD_CLAMP (S8.5.2.1). */
		.open_loop = true,
	},
	{
		.key = "vg",
		.part = "Vybronics VG0832012 (LRA, 8mm coin)",
		.note = "1.8 Vrms rated, 0.1-1.85 Vrms, f0 235 Hz, 80 mArms max",
		.kind = DRV2605_LRA,
		.rated_mv = 1800,   /* 2-1, rated voltage, RMS sine */
		/* Eq. 9 clamps the *peak*, and the datasheet's ceiling is an
		 * RMS one: 1.85 Vrms sine peaks at 1.85 x sqrt(2) = 2.616 V.
		 * Overdrive is a few tens of ms, so this is a conservative
		 * clamp rather than a limit being pushed -- raise it with
		 * `haptic cal 1800 2900` if the kick feels soft, but that is
		 * then above what the motor is specified for. */
		.od_mv = 2616,
		.f0_hz = 235,       /* 4-2, 235 +/-5 Hz */
		/* sqrt(1 - (4 x 300us + 300us) x 235 Hz) x 1000 = 805 */
		.rated_scale = 805,
		.library = 6,
		.open_loop = false, /* auto-resonance: the whole point of an LRA */
	},
	{
		.key = "lyra",
		.part = "JYLRA1030Z (LRA, 10mm coin)",
		.note = "1.8 Vrms rated, 0.1-1.85 Vrms, f0 210 Hz, 100 mA max",
		.kind = DRV2605_LRA,
		.rated_mv = 1800,
		.od_mv = 2616,      /* same 1.85 Vrms ceiling as the VG above */
		.f0_hz = 210,
		/* sqrt(1 - (4 x 300us + 300us) x 210 Hz) x 1000 = 828 */
		.rated_scale = 828,
		.library = 6,
		.open_loop = false,
	},
};

/* Live bench state. All of it is RAM and registers -- nothing here survives a
 * power cycle, deliberately (see the file header on OTP). */
static const struct eval_profile *sel = &PROFILES[0];
static struct drv2605_actuator act;
static uint16_t sel_rated_mv;
static uint16_t sel_od_mv;
static bool calibrated;  /* since the last actuator/voltage change */

static const struct eval_profile *profile_find(const char *key)
{
	for (size_t i = 0; i < ARRAY_SIZE(PROFILES); i++) {
		if (strcmp(PROFILES[i].key, key) == 0) {
			return &PROFILES[i];
		}
	}
	return NULL;
}

/*
 * Motor datasheet units -> DRV2605L register codes. Every equation reference
 * is datasheets/drv2605l.pdf S8.5.2 and S8.5.1.1; the divisions round to
 * nearest rather than truncating, since one code is ~21 mV and always
 * rounding down is a systematic half-code of lost drive for no reason.
 *
 * The LRA rated-voltage case is the one that cannot be done by inspection.
 * Eq. 5 is
 *
 *   V(LRA-CL_RMS) = 20.58e-3 x RATED_VOLTAGE / sqrt(1 - (4 x t_SAMPLE + 300us) x f)
 *
 * so the register value carries a per-actuator correction that depends on the
 * resonance frequency *and* on SAMPLE_TIME (fixed at 3 = 300us by drv2605.c's
 * SAMPLE_TIME_TARGET, which is why that constant is not a knob). Rather than
 * pull in a square root for two compile-time-known frequencies, each profile
 * carries its own sqrt term x1000 with the arithmetic shown above it -- so
 * changing f0_hz without recomputing rated_scale is visibly wrong at the site
 * where it matters, instead of silently mis-driving the motor.
 */
static void profile_actuator(const struct eval_profile *p, uint16_t rated_mv,
			     uint16_t od_mv, struct drv2605_actuator *out)
{
	uint32_t rated_reg;
	uint32_t od_reg;
	uint32_t drive_time;

	if (p->kind == DRV2605_LRA) {
		/* Eq. 5 rearranged: reg = V_rms x sqrt-term / 20.58 mV */
		rated_reg = ((uint32_t)rated_mv * p->rated_scale + 10290u) / 20580u;
		/* Eq. 9: reg = V_peak / 21.22 mV */
		od_reg = ((uint32_t)od_mv * 100u + 1061u) / 2122u;
		/* S8.5.1.1: "if the LRA has a resonance frequency of 200 Hz,
		 * then the drive time should be set to 2.5 ms" -- half the
		 * period. Drive time (ms) = DRIVE_TIME x 0.1 ms + 0.5 ms. */
		drive_time = (500000u / p->f0_hz + 50u - 500u) / 100u;
	} else {
		/* Eq. 4: reg = V_avg / 21.18 mV */
		rated_reg = ((uint32_t)rated_mv * 100u + 1059u) / 2118u;
		/* Eq. 6: reg = V_avg / 21.59 mV. Used as a safe upper-bound
		 * substitute for the closed-loop Eq. 8, same reasoning as
		 * drv2605.c's OD_CLAMP_TARGET comment. */
		od_reg = ((uint32_t)od_mv * 100u + 1080u) / 2159u;
		drive_time = 0x13u;  /* ERM: back-EMF sample rate, reset default */
	}

	out->kind = p->kind;
	out->rated_voltage = (uint8_t)MIN(rated_reg, 255u);
	out->od_clamp = (uint8_t)MIN(od_reg, 255u);
	out->drive_time = (uint8_t)MIN(drive_time, 31u);  /* Control1[4:0] */
	out->library = p->library;
	out->open_loop = p->open_loop;
}

static void print_selection(const struct shell *sh)
{
	shell_print(sh, "actuator : %s  [%s]", sel->key, sel->part);
	shell_print(sh, "           %s", sel->note);
	shell_print(sh, "mode     : %s, %s loop, ROM library %u",
		    act.kind == DRV2605_LRA ? "LRA" : "ERM",
		    act.open_loop ? "open" : "closed", act.library);
	if (act.kind == DRV2605_LRA) {
		shell_print(sh, "rated    : %u mVrms -> 0x%02x   (Eq. 5, f0 %u Hz)",
			    sel_rated_mv, act.rated_voltage, sel->f0_hz);
		shell_print(sh, "od clamp : %u mVpk  -> 0x%02x   (Eq. 9)",
			    sel_od_mv, act.od_clamp);
		shell_print(sh, "drive    : 0x%02x -> %u.%u ms half-period guess",
			    act.drive_time, (act.drive_time + 5u) / 10u,
			    (act.drive_time + 5u) % 10u);
	} else {
		shell_print(sh, "rated    : %u mV avg -> 0x%02x   (Eq. 4%s)",
			    sel_rated_mv, act.rated_voltage,
			    act.open_loop ? ", IGNORED in open loop" : "");
		shell_print(sh, "od clamp : %u mV avg -> 0x%02x   (Eq. 6%s)",
			    sel_od_mv, act.od_clamp,
			    act.open_loop ? ", full scale in open loop" : "");
	}
	shell_print(sh, "cal      : %s", calibrated
		    ? "done for this configuration"
		    : "NOT RUN -- effects play weak/untuned until 'haptic cal'");
}

/* Re-derive the register image from the current profile and voltages, push it
 * to the chip, and mark the calibration stale. Anything that changes what the
 * motor is or what it is driven at goes through here, so there is exactly one
 * place where "the chip disagrees with what the shell last printed" could be
 * introduced. */
static int apply_selection(const struct shell *sh)
{
	int rc;

	profile_actuator(sel, sel_rated_mv, sel_od_mv, &act);
	calibrated = false;

	rc = drv2605_apply_actuator(&act);
	if (rc != 0) {
		shell_error(sh, "applying actuator config failed (%d)", rc);
	}
	return rc;
}

static int cmd_haptic_act(const struct shell *sh, size_t argc, char **argv)
{
	const struct eval_profile *p;
	int rc;

	if (argc == 1) {
		print_selection(sh);
		shell_print(sh, "");
		for (size_t i = 0; i < ARRAY_SIZE(PROFILES); i++) {
			shell_print(sh, "  %-5s %s", PROFILES[i].key, PROFILES[i].part);
		}
		return 0;
	}

	p = profile_find(argv[1]);
	if (p == NULL) {
		shell_error(sh, "'%s' is not an actuator name -- run 'haptic act' "
			    "with no argument for the list", argv[1]);
		return -EINVAL;
	}

	sel = p;
	sel_rated_mv = p->rated_mv;   /* a new motor drops any bench voltage
				       * override: those were tuned against the
				       * motor that is no longer connected */
	sel_od_mv = p->od_mv;

	rc = apply_selection(sh);
	if (rc != 0) {
		return rc;
	}

	print_selection(sh);
	shell_print(sh, "Now run 'haptic cal'.");
	return 0;
}

static int cmd_haptic_lib(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long lib = strtoul(argv[1], &end, 10);
	int rc;

	ARG_UNUSED(argc);

	if (*end != '\0' || lib > 7) {
		shell_error(sh, "'%s' is not a library 0-7", argv[1]);
		return -EINVAL;
	}

	act.library = (uint8_t)lib;
	/* Not a calibration input (it is not in S8.5.6 step 3's list), so this
	 * deliberately does not invalidate a calibration -- switching library
	 * to compare waveform sets is the one change here that does not cost a
	 * recalibration. */
	rc = drv2605_apply_actuator(&act);
	if (rc != 0) {
		shell_error(sh, "library write failed (%d)", rc);
		return rc;
	}

	shell_print(sh, "library %lu (%s)", lib,
		    lib == 0 ? "Empty -- nothing will play" :
		    lib == 6 ? "LRA library" : "a TS2200 ERM library");
	if ((lib == 6) != (act.kind == DRV2605_LRA)) {
		shell_warn(sh, "that library and this actuator disagree: "
			   "library 6 is the LRA one, 1-5 and 7 are ERM ones");
	}
	return 0;
}

static int cmd_haptic_loop(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "open") == 0) {
		act.open_loop = true;
	} else if (strcmp(argv[1], "closed") == 0) {
		act.open_loop = false;
	} else {
		shell_error(sh, "'%s' is not 'open' or 'closed'", argv[1]);
		return -EINVAL;
	}

	rc = drv2605_apply_actuator(&act);
	if (rc != 0) {
		shell_error(sh, "loop-mode write failed (%d)", rc);
		return rc;
	}

	shell_print(sh, "%s loop", act.open_loop ? "open" : "closed");
	if (act.open_loop) {
		shell_print(sh, "note: open loop ignores RATED_VOLTAGE and takes "
			    "full scale from OD_CLAMP (S8.5.2.1)");
	}
	return 0;
}

/*
 * VBAT (0x21) and LRA_PERIOD (0x22) both read valid only "while the device is
 * actively sending a waveform" (S8.6.27/S8.6.28), so this plays effect 118 --
 * the library's long buzz, which exists precisely to be stopped
 * programmatically -- samples both, and clears GO.
 *
 * Two questions get answered at once. VBAT says what the rail actually
 * delivers *under load*, which is the number that decides whether an OD_CLAMP
 * target is reachable at all rather than merely written. LRA_PERIOD says what
 * resonance the auto-resonance engine locked onto, which is the only direct
 * check that an LRA is being driven at its own f0 and not at whatever
 * DRIVE_TIME guessed.
 */
static int cmd_haptic_measure(const struct shell *sh, size_t argc, char **argv)
{
	static const uint8_t LONG_BUZZ = 118;
	uint8_t vbat_min = 0xFF;
	uint8_t vbat_max = 0;
	uint8_t period_min = 0xFF;
	uint8_t period_max = 0;
	int period_samples = 0;
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = drv2605_play_sequence(&LONG_BUZZ, 1);
	if (rc != 0) {
		shell_error(sh, "playback failed (%d)", rc);
		return rc;
	}

	for (int i = 0; i < 15; i++) {
		uint8_t v;

		k_sleep(K_MSEC(20));
		if (drv2605_read_reg(REG_VBAT, &v) == 0 && v != 0) {
			vbat_min = MIN(vbat_min, v);
			vbat_max = MAX(vbat_max, v);
		}
		if (drv2605_read_reg(REG_LRA_PERIOD, &v) == 0 && v != 0) {
			period_min = MIN(period_min, v);
			period_max = MAX(period_max, v);
			period_samples++;
		}
	}
	(void)drv2605_stop();

	if (vbat_max == 0) {
		shell_warn(sh, "VDD: no reading -- the waveform was not playing");
	} else {
		/* S8.6.27: VDD (V) = VBAT x 5.6 / 255 */
		shell_print(sh, "VDD under drive: %u mV min, %u mV max "
			    "(raw 0x%02x-0x%02x)",
			    vbat_min * 5600u / 255u, vbat_max * 5600u / 255u,
			    vbat_min, vbat_max);
		shell_print(sh, "  an OD_CLAMP above that min is a target the "
			    "driver cannot reach, not a louder motor");
	}

	if (act.kind != DRV2605_LRA) {
		shell_print(sh, "resonance: n/a on an ERM (LRA_PERIOD is an "
			    "auto-resonance output)");
		return 0;
	}

	if (period_samples == 0) {
		shell_warn(sh, "resonance: no reading -- auto-resonance never "
			   "locked. Check the actuator really is the selected "
			   "one, that the loop is closed, and that DRIVE_TIME "
			   "is near its half-period.");
		return -EIO;
	}

	/* S8.6.28: LRA period (us) = LRA_PERIOD x 98.46 us. The longer period
	 * is the lower frequency, hence the crossed min/max. */
	shell_print(sh, "resonance: %u-%u Hz over %d samples (period raw "
		    "0x%02x-0x%02x = %u-%u us)",
		    1000000u / (period_max * 9846u / 100u),
		    1000000u / (period_min * 9846u / 100u),
		    period_samples, period_min, period_max,
		    period_min * 9846u / 100u, period_max * 9846u / 100u);
	shell_print(sh, "  datasheet f0 for this actuator: %u Hz -- a large "
		    "disagreement means the lock is wrong, not the motor",
		    sel->f0_hz);
	return 0;
}

/* argv[0] is the command word itself in every handler below -- Zephyr shell
 * convention, not an off-by-one. */

static int cmd_haptic_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = drv2605_dev_reset();

	if (rc != 0) {
		shell_error(sh, "reset i2c error (%d)", rc);
		return rc;
	}

	/* A DEV_RESET really does undo the actuator configuration -- including
	 * ERM/LRA mode and the library -- so the selected profile is pushed
	 * back down immediately. Without this the shell would still claim an
	 * LRA is selected while the chip had reverted to ERM mode on library
	 * A, and nothing would report the disagreement.
	 *
	 * "Power-on defaults" means the *profile's* numbers, not whatever mV
	 * a previous 'haptic cal <mv>' override last left in sel_rated_mv/
	 * sel_od_mv -- a reset that quietly kept a bench override would not
	 * be a reset, and this file printed exactly that lie until it was
	 * caught: the message below claimed "the 'erm' configuration
	 * re-applied" while the register writes underneath it were still an
	 * old 'haptic cal 2000' override. Dropping back to the profile here
	 * is what makes the printed voltages and the chip's actual registers
	 * agree again. */
	sel_rated_mv = sel->rated_mv;
	sel_od_mv = sel->od_mv;

	rc = apply_selection(sh);
	if (rc != 0) {
		return rc;
	}

	shell_print(sh, "DEV_RESET complete -- chip back to power-on defaults, "
		    "then the '%s' configuration re-applied:", sel->key);
	print_selection(sh);
	return 0;
}

/*
 * Optional args: `haptic cal [rated_mv [od_mv]]`. No args = the selected
 * actuator's own datasheet numbers. With args, the same conversion the
 * profile uses (profile_actuator(), which is where the equations live) is
 * applied to a bench override -- the knob for finding what this supply can
 * actually deliver, since a rail that cannot reach the commanded level leaves
 * the cal engine chasing it forever (GO never clears).
 *
 * The units follow the actuator, because the datasheet's equations do: on an
 * LRA these are RMS volts for rated and *peak* volts for the clamp; on an ERM
 * both are steady-state averages. Passing 3000 to an LRA is not a big number,
 * it is a wrong one -- the range check below is deliberately per-kind.
 */
static int cmd_haptic_cal(const struct shell *sh, size_t argc, char **argv)
{
	struct drv2605_cal_result cal;
	const uint16_t ceiling = sel->kind == DRV2605_LRA ? 2900u : 3600u;
	int rc;

	if (argc > 1) {
		char *end;
		unsigned long rated_mv = strtoul(argv[1], &end, 10);
		unsigned long od_mv;

		if (*end != '\0' || rated_mv < 300 || rated_mv > ceiling) {
			shell_error(sh, "'%s' is not a rated voltage in mV "
				    "(300-%u for this actuator)", argv[1], ceiling);
			return -EINVAL;
		}
		/* Default: hold the profile's own rated:overdrive ratio, so
		 * overriding only the rated voltage scales the clamp with it
		 * rather than silently leaving a mismatched pair. */
		od_mv = rated_mv * sel->od_mv / sel->rated_mv;

		if (argc > 2) {
			od_mv = strtoul(argv[2], &end, 10);
			if (*end != '\0' || od_mv < rated_mv || od_mv > 3600) {
				shell_error(sh, "'%s' is not an overdrive clamp "
					    "in mV (>= rated, <= 3600)", argv[2]);
				return -EINVAL;
			}
		}

		sel_rated_mv = (uint16_t)rated_mv;
		sel_od_mv = (uint16_t)MIN(od_mv, 3600ul);

		/* The ceilings above are what the *driver* will accept; the
		 * motor's own limit is lower and is the one that costs money.
		 * Both LRAs top out at 1.85 Vrms and the ERM at 3.6 V, so an
		 * override past the profile is an experiment being run
		 * knowingly, not a setting. */
		if (sel_rated_mv > sel->rated_mv || sel_od_mv > sel->od_mv) {
			shell_warn(sh, "above the %s datasheet numbers "
				   "(%u/%u mV) -- brief effects only, and watch "
				   "the motor's temperature",
				   sel->key, sel->rated_mv, sel->od_mv);
		}
	} else {
		sel_rated_mv = sel->rated_mv;
		sel_od_mv = sel->od_mv;
	}

	profile_actuator(sel, sel_rated_mv, sel_od_mv, &act);
	calibrated = false;
	print_selection(sh);

	rc = drv2605_calibrate_actuator(&act, &cal);
	if (rc != 0) {
		shell_error(sh, "calibration i2c error (%d)", rc);
		if (rc == -ETIMEDOUT) {
			shell_print(sh, "GO reads that succeeded while waiting: %d "
				    "(0 means the I2C bus itself was failing -- "
				    "check wiring/power, not calibration tuning; "
				    "nonzero means the bus is fine and the chip "
				    "never cleared GO)",
				    cal.go_reads_ok);
		}
		return rc;
	}

	shell_print(sh, "calibration %s (STATUS=0x%02x)",
		    cal.passed ? "PASSED" : "FAILED -- check motor wiring/OD_CLAMP",
		    cal.status);
	shell_print(sh, "A_CAL_COMP=0x%02x A_CAL_BEMF=0x%02x FEEDBACK_CONTROL=0x%02x",
		    cal.a_cal_comp, cal.a_cal_bemf, cal.feedback_control);
	calibrated = cal.passed;
	return cal.passed ? 0 : -EIO;
}

static int cmd_haptic_diag(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	bool ok;
	uint8_t status;
	int go_reads_ok = 0;
	int rc = drv2605_diagnose(&ok, &status, &go_reads_ok);

	if (rc != 0) {
		shell_error(sh, "diagnostic i2c error (%d)", rc);
		if (rc == -ETIMEDOUT) {
			shell_print(sh, "GO reads that succeeded while waiting: %d "
				    "(0 means the I2C bus itself was failing -- "
				    "check wiring/power, not calibration tuning; "
				    "nonzero means the bus is fine and the chip "
				    "never cleared GO)",
				    go_reads_ok);
		}
		return rc;
	}

	shell_print(sh, "actuator %s (STATUS=0x%02x)",
		    ok ? "OK -- present, not shorted, back-EMF in range"
		       : "FAULT -- not present, shorted, timing out, or "
			 "out-of-range back-EMF",
		    status);
	return ok ? 0 : -EIO;
}

/*
 * Post-mortem visibility: dump the registers that matter after a hang or a
 * suspicious pass, so chip state is read instead of inferred. Reading STATUS
 * here does clear its latching DIAG_RESULT/OVER_TEMP/OC_DETECT bits (S8.6.1
 * "clears upon read") -- dump after noting a failure, not before expecting
 * to re-read it elsewhere.
 */
static int cmd_haptic_dump(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	static const struct {
		uint8_t reg;
		const char *name;
	} regs[] = {
		{ 0x00, "STATUS       " },
		{ 0x01, "MODE         " },
		{ 0x03, "LIBRARY      " },  /* which ROM library GO plays from */
		{ 0x0C, "GO           " },
		{ 0x16, "RATED_VOLTAGE" },
		{ 0x17, "OD_CLAMP     " },
		{ 0x18, "A_CAL_COMP   " },
		{ 0x19, "A_CAL_BEMF   " },
		{ 0x1A, "FEEDBACK_CTRL" },  /* bit 7 is ERM(0)/LRA(1) */
		{ 0x1B, "CONTROL1     " },  /* bits 4-0 DRIVE_TIME */
		{ 0x1C, "CONTROL2     " },
		{ 0x1D, "CONTROL3     " },  /* bit 5 ERM_OPEN_LOOP, bit 0 LRA_OPEN_LOOP */
		{ 0x1E, "CONTROL4     " },
		{ 0x1F, "CONTROL5     " },
	};

	for (size_t i = 0; i < ARRAY_SIZE(regs); i++) {
		uint8_t val;
		int rc = drv2605_read_reg(regs[i].reg, &val);

		if (rc != 0) {
			shell_error(sh, "0x%02x %s read failed (%d)",
				    regs[i].reg, regs[i].name, rc);
			return rc;
		}
		shell_print(sh, "0x%02x %s = 0x%02x", regs[i].reg, regs[i].name, val);
	}

	/* The four bytes above that decide what the motor actually feels like
	 * are spread over three registers and two bit positions, which is how
	 * an LRA ends up being driven as an ERM without anything looking
	 * wrong. Decoded here, read back from the chip -- not from what this
	 * firmware believes it wrote. */
	{
		uint8_t fc, c1, c3, lib;

		if (drv2605_read_reg(0x1A, &fc) == 0 &&
		    drv2605_read_reg(0x1B, &c1) == 0 &&
		    drv2605_read_reg(0x1D, &c3) == 0 &&
		    drv2605_read_reg(0x03, &lib) == 0) {
			bool lra = (fc & BIT(7)) != 0;

			shell_print(sh, "decoded: %s, library %u, %s loop, "
				    "DRIVE_TIME %u",
				    lra ? "LRA" : "ERM", lib & 0x07u,
				    (lra ? (c3 & BIT(0)) : (c3 & BIT(5))) ? "open" : "closed",
				    c1 & 0x1Fu);
		}
	}
	return 0;
}

/* Shared by `play` (count 1) and `seq` (count up to 7) -- WAV_FRM_SEQ_MAX-1
 * in drv2605.c, kept as a literal 7 here rather than pulled from drv2605.c's
 * private #define, same boundary the datasheet's sequencer register block
 * fixes (0x04-0x0B, one trailing terminator byte). */
static int play_ids(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t effects[7];
	size_t count = argc - 1;

	for (size_t i = 0; i < count; i++) {
		char *end;
		unsigned long id = strtoul(argv[i + 1], &end, 10);

		if (*end != '\0' || id < 1 || id > 123) {
			shell_error(sh, "'%s' is not an effect id 1-123", argv[i + 1]);
			return -EINVAL;
		}
		effects[i] = (uint8_t)id;
	}

	int rc = drv2605_play_sequence(effects, count);

	if (rc != 0) {
		shell_error(sh, "play failed (%d)", rc);
		return rc;
	}

	/* Judging one actuator against another on an uncalibrated chip
	 * compares two arbitrary drive levels, not two motors -- the whole
	 * evaluation is void and nothing else says so. */
	if (!calibrated) {
		shell_warn(sh, "UNCALIBRATED for '%s' -- run 'haptic cal' "
			   "before comparing this against anything", sel->key);
	}

	for (size_t i = 0; i < count; i++) {
		shell_print(sh, "%3u  %s", effects[i], effect_name(effects[i]));
	}
	return 0;
}

static int cmd_haptic_play(const struct shell *sh, size_t argc, char **argv)
{
	return play_ids(sh, argc, argv);
}

static int cmd_haptic_seq(const struct shell *sh, size_t argc, char **argv)
{
	return play_ids(sh, argc, argv);
}

static int cmd_haptic_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (size_t id = 1; id < ARRAY_SIZE(EFFECT_NAME); id++) {
		if (EFFECT_NAME[id] != NULL) {
			shell_print(sh, "%3u  %s", (unsigned)id, EFFECT_NAME[id]);
		}
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(haptic_cmds,
	SHELL_CMD_ARG(act, NULL,
		"[erm|vg|lyra] -- select which actuator is on the output and "
		"configure the DRV2605L for it (ERM/LRA mode, ROM library, "
		"loop, drive time, rated voltage, overdrive clamp). No "
		"argument: show the current selection and the list. Registers "
		"only, nothing burned.",
		cmd_haptic_act, 1, 1),
	SHELL_CMD_ARG(cal, NULL,
		"[rated_mv [od_mv]] -- run auto-calibration for the selected "
		"actuator. No args: its datasheet numbers. With args: bench "
		"overrides, in the units that actuator's equations use (LRA: "
		"RMS rated, PEAK clamp; ERM: averages). No OTP -- register "
		"values only, lost on power cycle.",
		cmd_haptic_cal, 1, 2),
	SHELL_CMD_ARG(lib, NULL,
		"<0-7> -- override the ROM library. 6 is the LRA library, 1-5 "
		"and 7 are the TS2200 ERM libraries (1=A ... 5=E, 7=F), "
		"bucketed by rated voltage and rise time. Does not invalidate "
		"a calibration.",
		cmd_haptic_lib, 2, 0),
	SHELL_CMD_ARG(loop, NULL,
		"<open|closed> -- override the drive loop. The ERM libraries "
		"were designed open-loop; closed loop adds automatic overdrive "
		"and braking. Worth A/B-ing on the ERM.",
		cmd_haptic_loop, 2, 0),
	SHELL_CMD_ARG(measure, NULL,
		"Play a sustained buzz and read VDD under load plus, on an "
		"LRA, the resonance the auto-resonance engine locked onto. "
		"Both registers only read valid mid-playback.",
		cmd_haptic_measure, 1, 0),
	SHELL_CMD_ARG(reset, NULL,
		"Force a DEV_RESET (register write, not a supply event) -- use "
		"this instead of toggling the bench supply if a bulk cap on "
		"VIN might be holding it up through a 'power cycle'. Re-applies "
		"the selected actuator afterwards.",
		cmd_haptic_reset, 1, 0),
	SHELL_CMD_ARG(dump, NULL,
		"Dump STATUS/MODE/library/cal/control registers and decode the "
		"four fields that decide the feel -- read chip state after a "
		"hang instead of inferring it. Note: reading STATUS clears its "
		"latching fault bits.",
		cmd_haptic_dump, 1, 0),
	SHELL_CMD_ARG(diag, NULL,
		"Run the standalone actuator diagnostic (MODE=6) -- narrower "
		"and faster than 'cal', checks the actuator is present/not "
		"shorted/giving sane back-EMF without the full convergence "
		"search.",
		cmd_haptic_diag, 1, 0),
	SHELL_CMD_ARG(play, NULL,
		"<effect_id 1-123> -- play one ROM library effect.",
		cmd_haptic_play, 2, 0),
	SHELL_CMD_ARG(seq, NULL,
		"<id> [id...] -- play up to 7 effects back-to-back as one "
		"WAV_FRM_SEQ sequence.",
		cmd_haptic_seq, 2, 6),
	SHELL_CMD_ARG(list, NULL, "List all 123 library effect names.",
		cmd_haptic_list, 1, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(haptic, &haptic_cmds,
	"DRV2605L haptic bench evaluation across the candidate actuators "
	"(bare, no product mapping).",
	NULL);

int main(void)
{
	int rc = drv2605_init();

	if (rc != 0) {
		printk("[haptic-eval] drv2605_init failed (%d) -- check VIN, GND, "
		       "SDA/SCL wiring and the 0x5A address\n", rc);
		return 0;
	}

	/* drv2605_init() applies the *product's* actuator (the ERM). Push the
	 * bench selection over the top of it so the chip and this firmware's
	 * idea of what is connected agree from the first command -- they are
	 * the same motor at boot, but only because PROFILES[0] is the ERM, and
	 * that is not a thing to leave load-bearing. */
	profile_actuator(sel, sel->rated_mv, sel->od_mv, &act);
	sel_rated_mv = sel->rated_mv;
	sel_od_mv = sel->od_mv;
	rc = drv2605_apply_actuator(&act);
	if (rc != 0) {
		printk("[haptic-eval] applying the default actuator failed (%d)\n", rc);
		return 0;
	}

	printk("[haptic-eval] DRV2605L ready, configured for '%s' (%s).\n"
	       "  1. 'haptic act <erm|vg|lyra>' -- whichever motor is wired up\n"
	       "  2. 'haptic cal'               -- required; effects are weak "
	       "and untuned without it\n"
	       "  3. 'haptic play <id>'         -- 'haptic list' for the 123 names\n",
	       sel->key, sel->part);
	return 0;
}
