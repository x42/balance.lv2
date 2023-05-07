/* balance -- LV2 stereo balance control
 *
 * Copyright (C) 2013 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#ifdef HAVE_LV2_1_18_6
#include <lv2/core/lv2.h>
#include <lv2/state/state.h>
#else
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#endif

#include "uris.h"

#define MAXDELAY (2001)
#define CHANNELS (2)

#define C_LEFT (0)
#define C_RIGHT (1)

#define FADE_LEN (64)
#define METER_FALLOFF (13.3) // dB/sec
#define UPDATE_FREQ (30.0)   // Hz
#define PEAK_HOLD_TIME (2.0) // seconds

#define PEAK_INTEGRATION_MAX (0.05)   // seconds -- used for buffer size limit
#define PEAK_INTEGRATION_TIME (0.005) // seconds -- must be >=0; should be <= PEAK_INTEGRATION_MAX
#define PHASE_INTEGRATION_TIME (.5)   // seconds -- must be > 0

#define SIGNUM(a)  ( (a) < 0 ? -1 : 1)
#define SQUARE(a)  ( (a) * (a) )

#define VALTODB(V) (20.0f * log10f(V))

#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#define RAIL(v, min, max) (MIN((max), MAX((min), (v))))

typedef enum {
	BLC_TRIM   = 0,
	BLC_PHASEL,
	BLC_PHASER,
	BLC_BALANCE,
	BLC_UNIYGAIN,
	BLC_DLYL,
	BLC_DLYR,
	BLC_MONOIZE,
	BLC_INL,
	BLC_INR,
	BLC_OUTL,
	BLC_OUTR,
	BLC_UICONTROL,
	BLC_UINOTIFY
} PortIndex;

typedef struct {
  LV2_URID_Map* map;
  balanceURIs uris;

  LV2_Atom_Forge forge;
  LV2_Atom_Forge_Frame frame;
  const LV2_Atom_Sequence* control;
  LV2_Atom_Sequence* notify;

	/* control ports */
	float* trim;
	float* phase[CHANNELS];
	float* balance;
	float* unitygain;
	float* monomode;
	float* delay[CHANNELS];
	float* input[CHANNELS];
	float* output[CHANNELS];

	/* delay buffers */
	float buffer[CHANNELS][MAXDELAY];

	/* buffer offsets */
	int w_ptr[CHANNELS];
	int r_ptr[CHANNELS];

	/* current settings */
	float c_amp[CHANNELS];
	int   c_dly[CHANNELS];
	int   c_monomode;

	float samplerate;
	float p_bal[CHANNELS];
	int   p_dly[CHANNELS];

	int uicom_active;

	float meter_falloff;
	float peak_hold;

	/* peak hold */
	int     p_peakcnt;
	int     peak_integrate_pos, peak_integrate_pref, peak_integrate_max;
	float   p_peak_in[CHANNELS],    p_peak_out[CHANNELS];   // [abs max signal] peak hold
	double *p_peak_inPi[CHANNELS], *p_peak_outPi[CHANNELS]; // [sqaread signal] integration data buffer
	double  p_peak_inP[CHANNELS],   p_peak_outP[CHANNELS];  // [squared signal] current
	double  p_peak_inM[CHANNELS],   p_peak_outM[CHANNELS];  // [squared signal] max

	/* visible peak w/ falloff */
	float p_vpeak_in[CHANNELS];  // [dBFS]
	float p_vpeak_out[CHANNELS]; // [dbFS]

	int     phase_integrate_pos, phase_integrate_max;
	double *p_phase_outPi, *p_phase_outNi;
	double  p_phase_outP,   p_phase_outN;

	/* peak hold */
	float p_tme_in[CHANNELS];  // [samples]
	float p_tme_out[CHANNELS]; // [samples
	float p_max_in[CHANNELS];  // [dbFS]
	float p_max_out[CHANNELS]; // [dbFS]

	int   queue_stateswitch;
	float state[3];
} BalanceControl;

#define DLYWITHGAIN(GAIN) \
	buffer[ self->w_ptr[chn] ] = input[pos]; \
	output[pos] = buffer[ self->r_ptr[chn] ] * (GAIN);

#define INCREMENT_PTRS(CHN) \
	self->r_ptr[CHN] = (self->r_ptr[CHN] + 1) % MAXDELAY; \
	self->w_ptr[CHN] = (self->w_ptr[CHN] + 1) % MAXDELAY;

#define SMOOTHGAIN (amp + (target_amp - amp) * (float) MIN(pos, fade_len) / (float)fade_len)

static void
process_channel(BalanceControl *self,
		const float target_amp, const uint32_t chn,
		const uint32_t n_samples)
{
	uint32_t pos = 0;
	const float  delay = *(self->delay[chn]);
	const float* const input = self->input[chn];
	float* const output = self->output[chn];
	float* const buffer = self->buffer[chn];
	const float amp = self->c_amp[chn];

	// TODO bridge cycles if smoothing is longer than n_samples
	const uint32_t fade_len = (n_samples >= FADE_LEN) ? FADE_LEN : n_samples;

	if (self->c_dly[chn] != rint(delay)) {
		/* delay length changed */
		const int r_ptr = self->r_ptr[chn];
		const int w_ptr = self->w_ptr[chn];

		/* fade out */
		for (; pos < fade_len; pos++) {
			const float gain = (float)(fade_len - pos) / (float)fade_len;
			DLYWITHGAIN(gain * SMOOTHGAIN)
			INCREMENT_PTRS(chn);
		}

		/* restore pointers to beginning of fade. */
		self->r_ptr[chn] = r_ptr;
		self->w_ptr[chn] = w_ptr;
		INCREMENT_PTRS(chn);
		pos = 1;

		/* update read pointer */
		self->r_ptr[chn] += self->c_dly[chn] - rintf(delay);
		if (self->r_ptr[chn] < 0) {
			self->r_ptr[chn] -= MAXDELAY * floor(self->r_ptr[chn] / (float)MAXDELAY);
		}
		self->r_ptr[chn] = self->r_ptr[chn] % MAXDELAY;
		self->c_dly[chn] = rint(delay);

		/* fade in, x-fade */
		for (; pos < fade_len; pos++) {
			const float gain = (float)pos / (float)fade_len;
			buffer[ self->w_ptr[chn] ] = input[pos]; \
			output[pos] += buffer[ self->r_ptr[chn] ] * (gain * SMOOTHGAIN);
			INCREMENT_PTRS(chn);
		}
	}

	if (target_amp != self->c_amp[chn]) {
		for (; pos < n_samples; pos++) {
			DLYWITHGAIN(SMOOTHGAIN)
			INCREMENT_PTRS(chn);
		}
	} else {
		for (; pos < n_samples; pos++) {
			DLYWITHGAIN(amp)
			INCREMENT_PTRS(chn);
		}
	}
	self->c_amp[chn] = target_amp;
}

static void
channel_map(BalanceControl *self, int mode,
		const uint32_t start, const uint32_t end)
{
	uint32_t i;
	switch (mode) {
		case 1:
			for (i=start; i < end; ++i) {
				self->output[C_RIGHT][i] = self->output[C_LEFT][i];
			}
			break;
		case 2:
			for (i=start; i < end; ++i) {
				self->output[C_LEFT][i] = self->output[C_RIGHT][i];
			}
			break;
		case 3:
			for (i=start; i < end; ++i) {
				const float mem = self->output[C_LEFT][i];
				self->output[C_LEFT][i] = self->output[C_RIGHT][i];
				self->output[C_RIGHT][i] = mem;
			}
			break;
		case 4:
			for (i=start; i < end; ++i) {
				const float mono = (self->output[C_LEFT][i] + self->output[C_RIGHT][i]) / 2.0;
				self->output[C_LEFT][i] = self->output[C_RIGHT][i] = mono;
			}
			break;
		default:
		case 0:
			break;
	}
}

static void
channel_map_change(BalanceControl *self, int mode,
		const uint32_t pos, float *out)
{
	switch (mode) {
		case 1:
			out[C_LEFT]  = self->output[C_LEFT][pos];
			out[C_RIGHT] = self->output[C_LEFT][pos];
			break;
		case 2:
			out[C_LEFT] = self->output[C_RIGHT][pos];
			out[C_RIGHT] = self->output[C_RIGHT][pos];
			break;
		case 3:
			out[C_LEFT] = self->output[C_RIGHT][pos];
			out[C_RIGHT] = self->output[C_LEFT][pos];
			break;
		case 4:
			{
			const float mono = (self->output[C_LEFT][pos] + self->output[C_RIGHT][pos]) / 2.0;
			out[C_LEFT] = out[C_RIGHT] = mono;
			}
			break;
		default:
		case 0:
			out[C_LEFT] = self->output[C_LEFT][pos];
			out[C_RIGHT] = self->output[C_RIGHT][pos];
			break;
	}
}

static inline float gain_to_db(const float g) {
	if (g <= 0) return -INFINITY;
	return VALTODB(g);
}

static inline float db_to_gain(const float d) {
	return pow(10, d/20.0);
}

static void reset_uicom(BalanceControl* self) {
	int i;
	for (i=0; i < CHANNELS; ++i) {
		self->p_peak_in[i] = -INFINITY;
		self->p_peak_out[i] = -INFINITY;
		self->p_vpeak_in[i] = -INFINITY;
		self->p_vpeak_out[i] = -INFINITY;

		self->p_tme_in[i] = 0;
		self->p_tme_out[i] = 0;
		self->p_max_in[i] = -INFINITY;
		self->p_max_out[i] = -INFINITY;

		self->p_bal[i] = INFINITY;
		self->p_dly[i] = -1;

		self->p_phase_outP   = 0;
		self->p_phase_outN   = 0;

		memset(self->p_peak_inPi[i],  0, self->peak_integrate_max * sizeof(double));
		memset(self->p_peak_outPi[i], 0, self->peak_integrate_max * sizeof(double));

		self->p_peak_outP[i] = self->p_peak_inP[i] = 0;
		self->p_peak_outM[i] = self->p_peak_inM[i] = 0;
	}

	memset(self->p_phase_outPi, 0, self->phase_integrate_max * sizeof(double));
	memset(self->p_phase_outNi, 0, self->phase_integrate_max * sizeof(double));
	self->peak_integrate_pos = self->phase_integrate_pos = 0;

	self->p_peakcnt  = 0;
}

static void send_cfg_to_ui(BalanceControl* self) {
	forge_kvcontrolmessage(&self->forge, &self->uris, CFG_INTEGRATE, self->peak_integrate_pref / self->samplerate);
	forge_kvcontrolmessage(&self->forge, &self->uris, CFG_FALLOFF, self->meter_falloff * (float) UPDATE_FREQ);
	forge_kvcontrolmessage(&self->forge, &self->uris, CFG_HOLDTIME, self->peak_hold / (float) UPDATE_FREQ);
}

static void update_meter_cfg(BalanceControl* self, int key, float val) {
	switch (key) {
		case 0:
			if (val >=0 && val <= self->peak_integrate_max) {
				self->peak_integrate_pref = val * self->samplerate;
			}
			reset_uicom(self);
			break;
		case 1:
				self->meter_falloff = MAX(0, val / UPDATE_FREQ);
				self->meter_falloff = MIN(self->meter_falloff, 1000);
			break;
		case 2:
				self->peak_hold = MAX(0, val * UPDATE_FREQ);
				self->peak_hold = MIN(self->peak_hold, 60 * UPDATE_FREQ);
			break;
		case 3:
			for (int i=0; i < CHANNELS; ++i) {
				if ( ((int)val)&1) {
					self->p_max_in[i] = -INFINITY;
				}
				if ( ((int)val)&2) {
					self->p_max_out[i] = -INFINITY;
				}
			}
			forge_kvcontrolmessage(&self->forge, &self->uris, PEAK_IN_LEFT, self->p_max_in[C_LEFT]);
			forge_kvcontrolmessage(&self->forge, &self->uris, PEAK_IN_RIGHT, self->p_max_in[C_RIGHT]);
			forge_kvcontrolmessage(&self->forge, &self->uris, PEAK_OUT_LEFT, self->p_max_out[C_LEFT]);
			forge_kvcontrolmessage(&self->forge, &self->uris, PEAK_OUT_RIGHT, self->p_max_out[C_RIGHT]);
			break;

		default:
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
	uint32_t i,c;
	BalanceControl* self = (BalanceControl*)instance;
	const float balance = *self->balance;
	const float trim = db_to_gain(*self->trim);
	float gain_left  = 1.0;
	float gain_right = 1.0;

	const int ascnt = self->samplerate / UPDATE_FREQ;

  const uint32_t capacity = self->notify->atom.size;
  lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->notify, capacity);
  lv2_atom_forge_sequence_head(&self->forge, &self->frame, 0);

  /* reset after state restore */
	if (self->queue_stateswitch) {
		self->queue_stateswitch = 0;
		self->peak_integrate_pref = self->state[0] * self->samplerate;
		self->meter_falloff = self->state[1] / UPDATE_FREQ;
		self->peak_hold = self->state[2] * UPDATE_FREQ;

		self->peak_integrate_pref = MAX(0, self->peak_integrate_pref);
		self->peak_integrate_pref = MIN(self->peak_integrate_pref, self->peak_integrate_max);

		self->meter_falloff = MAX(0, self->meter_falloff);
		self->meter_falloff = MIN(self->meter_falloff, 1000);

		self->peak_hold = MAX(0, self->peak_hold);
		self->peak_hold = MIN(self->peak_hold, 60 * UPDATE_FREQ);
		reset_uicom(self);
		send_cfg_to_ui(self);
	}

  /* Process incoming events from GUI */
  if (self->control) {
    LV2_Atom_Event* ev = lv2_atom_sequence_begin(&(self->control)->body);
    while(!lv2_atom_sequence_is_end(&(self->control)->body, (self->control)->atom.size, ev)) {
      if (ev->body.type == self->uris.atom_Blank || ev->body.type == self->uris.atom_Object) {
				const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
				if (obj->body.otype == self->uris.blc_meters_on) {
					if (self->uicom_active == 0) {
						reset_uicom(self);
						send_cfg_to_ui(self);
						self->uicom_active = 1;
					}
				}
				if (obj->body.otype == self->uris.blc_meters_off) {
					self->uicom_active = 0;
				}
				if (obj->body.otype == self->uris.blc_meters_cfg) {
					const LV2_Atom* key = NULL;
					const LV2_Atom* value = NULL;
					lv2_atom_object_get(obj, self->uris.blc_cckey, &key, self->uris.blc_ccval, &value, 0);
					if (value && key) {
						update_meter_cfg(self, ((LV2_Atom_Int*)key)->body, ((LV2_Atom_Float*)value)->body);
					}
				}
			}
      ev = lv2_atom_sequence_next(ev);
    }
	}

	/* pre-calculate parameters */
	if (balance < 0) {
		gain_right = 1.0 + RAIL(balance, -1.0, 0.0);
	} else if (balance > 0) {
		gain_left = 1.0 - RAIL(balance, 0.0, 1.0);
	}

	switch ((int) *self->unitygain) {
		case 1:
			{
				/* maintain amplitude sum */
				const double gaindiff = (gain_left - gain_right);
				gain_left = 1.0 + gaindiff;
				gain_right = 1.0 - gaindiff;
			}
			break;
		case 2:
			{
				/* equal power*/
				if (balance < 0) {
					gain_right = MAX(.5, gain_right);
					gain_left = db_to_gain(-gain_to_db(gain_right));
				} else {
					gain_left = MAX(.5, gain_left);
					gain_right = db_to_gain(-gain_to_db(gain_left));
				}
			}
		case 0:
			/* 'tradidional' balance */
			break;
	}

	if (*(self->phase[C_LEFT])) gain_left *=-1;
	if (*(self->phase[C_RIGHT])) gain_right *=-1;

	/* keep track of input levels -- only if GUI is visiable */
	if (self->uicom_active) {
		for (c=0; c < CHANNELS; ++c) {
			for (i=0; i < n_samples; ++i) {
				/* input peak meter */
				const float ps = fabsf(self->input[c][i]);
				if (ps > self->p_peak_in[c]) self->p_peak_in[c] = ps;

				if (self->peak_integrate_pref < 1) {
					const float psm = ps * ps;
					if (psm > self->p_peak_inM[c]) self->p_peak_inM[c] = psm;
					continue;
				}

				/* integrated level, peak */
				const int pip = (self->peak_integrate_pos + i ) % self->peak_integrate_pref;
				const double p_sig = SQUARE(self->input[c][i]);
				self->p_peak_inP[c] += p_sig - self->p_peak_inPi[c][pip];
				self->p_peak_inPi[c][pip] = p_sig;
				/* peak of integrated signal */
				const float psm = self->p_peak_inP[c] / (double) self->peak_integrate_pref;
				if (psm > self->p_peak_inM[c]) self->p_peak_inM[c] = psm;
			}
		}
	}

	/* process audio -- delayline + balance & gain */
	process_channel(self, gain_left * trim,  C_LEFT, n_samples);
	process_channel(self, gain_right * trim, C_RIGHT, n_samples);

	/* swap/assign channels */
	uint32_t pos = 0;

	if (self->c_monomode != (int) *self->monomode) {
		/* smooth change */
		const uint32_t fade_len = (n_samples >= FADE_LEN) ? FADE_LEN : n_samples;
		for (; pos < fade_len; pos++) {
			const float gain = (float)pos / (float)fade_len;
			float x1[CHANNELS], x2[CHANNELS];
			channel_map_change(self, self->c_monomode, pos, x1);
			channel_map_change(self, (int) *self->monomode, pos, x2);
			self->output[C_LEFT][pos] = x1[C_LEFT] * (1.0 - gain) + x2[C_LEFT] * gain;
			self->output[C_RIGHT][pos] = x1[C_RIGHT] * (1.0 - gain) + x2[C_RIGHT] * gain;
		}
	}

	channel_map(self, (int) *self->monomode, pos, n_samples);
	self->c_monomode = (int) *self->monomode;

	/* audio processing done */

	if (!self->uicom_active) {
		return;
	}

	/* output peak meter */
	for (c=0; c < CHANNELS; ++c) {
		for (i=0; i < n_samples; ++i) {
			/* peak */
			const float ps = fabsf(self->output[c][i]);
			if (ps > self->p_peak_out[c]) self->p_peak_out[c] = ps;

				if (self->peak_integrate_pref < 1) {
				const float psm = ps * ps;
				if (psm > self->p_peak_outM[c]) self->p_peak_outM[c] = psm;
				continue;
			}

			/* integrated level, peak */
			const int pip = (self->peak_integrate_pos + i ) % self->peak_integrate_pref;
			const double p_sig = SQUARE(self->output[c][i]);
			self->p_peak_outP[c] += p_sig - self->p_peak_outPi[c][pip];
			self->p_peak_outPi[c][pip] = p_sig;
			/* peak of integrated signal */
			const float psm = self->p_peak_outP[c] / (double) self->peak_integrate_pref;
			if (psm > self->p_peak_outM[c]) self->p_peak_outM[c] = psm;
		}
	}
	if (self->peak_integrate_pref > 0) {
		self->peak_integrate_pos = (self->peak_integrate_pos + n_samples ) % self->peak_integrate_pref;
	}

	/* simple output phase correlation */
	for (i=0; i < n_samples; ++i) {
		const double p_pos = SQUARE(self->output[C_LEFT][i] + self->output[C_RIGHT][i]);
		const double p_neg = SQUARE(self->output[C_LEFT][i] - self->output[C_RIGHT][i]);

		/* integrate over 500ms */
		self->p_phase_outP += p_pos - self->p_phase_outPi[self->phase_integrate_pos];
		self->p_phase_outN += p_neg - self->p_phase_outNi[self->phase_integrate_pos];
		self->p_phase_outPi[self->phase_integrate_pos] = p_pos;
		self->p_phase_outNi[self->phase_integrate_pos] = p_neg;
		self->phase_integrate_pos = (self->phase_integrate_pos + 1) % self->phase_integrate_max;
	}

/* abs peak hold */
#define PKM(A,CHN,ID) \
{ \
	const float peak = VALTODB(self->p_peak_##A[CHN]); \
	if (peak > self->p_max_##A[CHN]) { \
		self->p_max_##A[CHN] = peak; \
		self->p_tme_##A[CHN] = 0; \
		forge_kvcontrolmessage(&self->forge, &self->uris, ID, self->p_max_##A[CHN]); \
	} else if (self->peak_hold <= 0) { \
		(self->p_tme_##A[CHN])=0; /* infinite hold */ \
	} else if (self->p_tme_##A[CHN] <= self->peak_hold) { \
		(self->p_tme_##A[CHN])++; \
	} else if (self->meter_falloff == 0) { \
		self->p_max_##A[CHN] = peak; \
		forge_kvcontrolmessage(&self->forge, &self->uris, ID, self->p_max_##A[CHN]); \
	} else { \
		self->p_max_##A[CHN] -= self->meter_falloff; \
		self->p_max_##A[CHN] = MAX(peak, self->p_max_##A[CHN]); \
		forge_kvcontrolmessage(&self->forge, &self->uris, ID, self->p_max_##A[CHN]); \
	} \
}

/* RMS meter */
#define PKF(A,CHN,ID) \
{ \
	float dbp = VALTODB(sqrt(2.0 * self->p_peak_##A##M[CHN])); \
	if (dbp > self->p_vpeak_##A[CHN]) { \
		self->p_vpeak_##A[CHN] = dbp; \
	} else if (self->meter_falloff == 0) { \
		self->p_vpeak_##A[CHN] = dbp; \
	} else { \
		self->p_vpeak_##A[CHN] -= self->meter_falloff; \
		self->p_vpeak_##A[CHN] = MAX(dbp, self->p_vpeak_##A[CHN]); \
	} \
	forge_kvcontrolmessage(&self->forge, &self->uris, ID, (self->p_vpeak_##A [CHN])); \
}

	/* report peaks to UI */
	self->p_peakcnt += n_samples;
	if (self->p_peakcnt > ascnt) {

		PKF(in,  C_LEFT,  METER_IN_LEFT)
		PKF(in,  C_RIGHT, METER_IN_RIGHT);
		PKF(out, C_LEFT,  METER_OUT_LEFT);
		PKF(out, C_RIGHT, METER_OUT_RIGHT);

		PKM(in,  C_LEFT,  PEAK_IN_LEFT);
		PKM(in,  C_RIGHT, PEAK_IN_RIGHT);
		PKM(out, C_LEFT,  PEAK_OUT_LEFT);
		PKM(out, C_RIGHT, PEAK_OUT_RIGHT);

#define RMSF(A) sqrt( ( (A) / (double)self->phase_integrate_max ) + 1.0e-12 )
		double phase = 0.0;
		const double phasdiv = self->p_phase_outP + self->p_phase_outN;
		if (phasdiv >= 1.0e-6) {
			phase = (RMSF(self->p_phase_outP) - RMSF(self->p_phase_outN)) / RMSF(phasdiv);
		} else if (self->p_phase_outP > .001 && self->p_phase_outN > .001) {
			phase = 1.0;
		}

		forge_kvcontrolmessage(&self->forge, &self->uris, PHASE_OUT, phase);

		self->p_peakcnt -= ascnt;
		for (c=0; c < CHANNELS; ++c) {
			self->p_peak_in[c] = -INFINITY;
			self->p_peak_out[c] = -INFINITY;
			self->p_peak_inM[c] = -INFINITY;
			self->p_peak_outM[c] = -INFINITY;
		}
	}

	/* report values to UI - if changed*/
	float bal = gain_to_db(fabsf(gain_left));
	if (bal != self->p_bal[C_LEFT]) {
		forge_kvcontrolmessage(&self->forge, &self->uris, GAIN_LEFT, bal);
	}
	self->p_bal[C_LEFT] = bal;

	bal = gain_to_db(fabsf(gain_right));
	if (bal != self->p_bal[C_RIGHT]) {
		forge_kvcontrolmessage(&self->forge, &self->uris, GAIN_RIGHT, bal);
	}
	self->p_bal[C_RIGHT] = bal;

	if (self->p_dly[C_LEFT] != self->c_dly[C_LEFT]) {
		forge_kvcontrolmessage(&self->forge, &self->uris, DELAY_LEFT, (float) self->c_dly[C_LEFT] / self->samplerate);
	}
	self->p_dly[C_LEFT] = self->c_dly[C_LEFT];

	if (self->p_dly[C_RIGHT] != self->c_dly[C_RIGHT]) {
		forge_kvcontrolmessage(&self->forge, &self->uris, DELAY_RIGHT, (float) self->c_dly[C_RIGHT] / self->samplerate);
	}
	self->p_dly[C_RIGHT] = self->c_dly[C_RIGHT];

}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
	int i;
	BalanceControl* self = (BalanceControl*) calloc(1, sizeof(BalanceControl));
	if (!self) return NULL;

  for (int i=0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID__map)) {
      self->map = (LV2_URID_Map*)features[i]->data;
    }
  }

  if (!self->map) {
    fprintf(stderr, "BLClv2 error: Host does not support urid:map\n");
		free(self);
		return NULL;
	}

  map_balance_uris(self->map, &self->uris);
  lv2_atom_forge_init(&self->forge, self->map);

	self->peak_integrate_max = PEAK_INTEGRATION_MAX * rate;
	self->peak_integrate_pref = PEAK_INTEGRATION_TIME * rate;
	self->phase_integrate_max = PHASE_INTEGRATION_TIME * rate;
	self->meter_falloff = METER_FALLOFF / UPDATE_FREQ;
	self->peak_hold = PEAK_HOLD_TIME * UPDATE_FREQ;

	assert(self->peak_integrate_max >= 0);
	assert(self->phase_integrate_max > 0);
	assert(self->peak_integrate_max <= self->phase_integrate_max);

	for (i=0; i < CHANNELS; ++i) {
		self->c_amp[i] = 1.0;
		self->c_dly[i] = 0;
		self->r_ptr[i] = self->w_ptr[i] = 0;
		memset(self->buffer[i], 0, sizeof(float) * MAXDELAY);
		self->p_peak_inPi[i]  = (double*) malloc(self->peak_integrate_max * sizeof(double));
		self->p_peak_outPi[i] = (double*) malloc(self->peak_integrate_max * sizeof(double));
	}
	self->p_phase_outPi = (double*) malloc(self->phase_integrate_max * sizeof(double));
	self->p_phase_outNi = (double*) malloc(self->phase_integrate_max * sizeof(double));

	self->uicom_active = 0;
	self->c_monomode = 0;
	self->samplerate = rate;
	self->queue_stateswitch = 0;

	reset_uicom(self);

	return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	BalanceControl* self = (BalanceControl*)instance;

	switch ((PortIndex)port) {
	case BLC_TRIM:
		self->trim = (float*) data;
		break;
	case BLC_PHASEL:
		self->phase[C_LEFT] = (float*) data;
		break;
	case BLC_PHASER:
		self->phase[C_RIGHT] = (float*) data;
		break;
	case BLC_BALANCE:
		self->balance = (float*) data;
		break;
	case BLC_UNIYGAIN:
		self->unitygain = (float*) data;
		break;
	case BLC_MONOIZE:
		self->monomode = (float*) data;
		break;
	case BLC_DLYL:
		self->delay[C_LEFT] = (float*) data;
		break;
	case BLC_DLYR:
		self->delay[C_RIGHT] = (float*) data;
		break;
	case BLC_INL:
		self->input[C_LEFT] = (float*) data;
		break;
	case BLC_INR:
		self->input[C_RIGHT] = (float*) data;
		break;
	case BLC_OUTL:
		self->output[C_LEFT] = (float*) data;
		break;
	case BLC_OUTR:
		self->output[C_RIGHT] = (float*) data;
		break;
	case BLC_UINOTIFY:
		self->notify = (LV2_Atom_Sequence*)data;
		break;
	case BLC_UICONTROL:
		self->control = (const LV2_Atom_Sequence*)data;
		break;
	}
}

static LV2_State_Status
save(LV2_Handle                instance,
     LV2_State_Store_Function  store,
     LV2_State_Handle          handle,
     uint32_t                  flags,
     const LV2_Feature* const* features)
{
	BalanceControl* self = (BalanceControl*)instance;

	char cfg[1024];
	int  off = 0;

	off += sprintf(cfg + off, "peak_integrate=%f\n", self->peak_integrate_pref / self->samplerate);
	off += sprintf(cfg + off, "meter_falloff=%f\n", self->meter_falloff * (float) UPDATE_FREQ);
	off += sprintf(cfg + off, "peak_hold=%f\n", self->peak_hold / (float) UPDATE_FREQ);

	store(handle, self->uris.blc_state,
			cfg, strlen(cfg) + 1,
			self->uris.atom_String,
			LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
	return LV2_STATE_SUCCESS;
}

static LV2_State_Status
restore(LV2_Handle                  instance,
        LV2_State_Retrieve_Function retrieve,
        LV2_State_Handle            handle,
        uint32_t                    flags,
        const LV2_Feature* const*   features)
{
	BalanceControl* self = (BalanceControl*)instance;
  size_t   size;
	uint32_t type;
	uint32_t valflags;
	const void* value = retrieve(handle, self->uris.blc_state, &size, &type, &valflags);

	if (!value) {
		return LV2_STATE_ERR_UNKNOWN;
	}

	const char* cfg = (const char*)value;
	const char *te, *ts = cfg;

	while (ts && *ts && (te=strchr(ts, '\n'))) {
		char *val;
		char kv[1024];
		memcpy(kv, ts, te-ts);
		kv[te-ts]=0;
		if((val=strchr(kv,'='))) {
			*val=0;
			if (!strcmp(kv, "peak_integrate")) {
				self->state[0] = atof(val+1);
			} else if (!strcmp(kv, "meter_falloff")) {
				self->state[1] = atof(val+1);
			} else if (!strcmp(kv, "peak_hold")) {
				self->state[2] = atof(val+1);
			}
		}
		ts=te+1;
	}
	self->queue_stateswitch = 1;
  return LV2_STATE_SUCCESS;
}

static void
cleanup(LV2_Handle instance)
{
	BalanceControl* self = (BalanceControl*)instance;
	for (int i=0; i < CHANNELS; ++i) {
		free(self->p_peak_inPi[i]);
		free(self->p_peak_outPi[i]);
	}
	free(self->p_phase_outPi);
	free(self->p_phase_outNi);
	free(instance);
}

const void*
extension_data(const char* uri)
{
  static const LV2_State_Interface  state  = { save, restore };
  if (!strcmp(uri, LV2_STATE__interface)) {
    return &state;
  }
	return NULL;
}

static const LV2_Descriptor descriptor = {
	BLC_URI,
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#    define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#    define LV2_SYMBOL_EXPORT  __attribute__ ((visibility ("default")))
#endif
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor;
	default:
		return NULL;
	}
}
