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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define BLC_URI "http://gareus.org/oss/lv2/balance"

#define MAXDELAY (2001)
#define CHANNELS (2)
#define FADE_LEN (16)
#define GAIN_SMOOTH_LEN (64)

typedef enum {
	BLC_BALANCE   = 0,
	BLC_UNIYGAIN,
	BLC_DLYL,
	BLC_DLYR,
	BLC_ATTL,
	BLC_ATTR,
	BLC_MTRL,
	BLC_MTRR,
	BLC_INL,
	BLC_INR,
	BLC_OUTL,
	BLC_OUTR
} PortIndex;

typedef struct {
	float* balance;
	float* unitygain;
	float* atten[CHANNELS];
	float* meter[CHANNELS];
	float* delay[CHANNELS];
	float* input[CHANNELS];
	float* output[CHANNELS];

	float buffer[CHANNELS][MAXDELAY];

	/* buffer offsets */
	int w_ptr[CHANNELS];
	int r_ptr[CHANNELS];

	/* current settings */
	float c_amp[CHANNELS];
	int   c_dly[CHANNELS];
} BalanceControl;

#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#define RAIL(v, min, max) (MIN((max), MAX((min), (v))))

#define DLYWITHGAIN(GAIN) \
	buffer[ self->w_ptr[chn] ] = input[pos]; \
	output[pos] = buffer[ self->r_ptr[chn] ] * (GAIN);

#define INCREMENT_PTRS(CHN) \
	self->r_ptr[CHN] = (self->r_ptr[CHN] + 1) % MAXDELAY; \
	self->w_ptr[CHN] = (self->w_ptr[CHN] + 1) % MAXDELAY;

// TODO optimize, use delta..
#define SMOOTHGAIN (amp + (target_amp - amp) * (float) MAX(pos, smooth_len) / (float)smooth_len)

static void
process_channel(BalanceControl *self, const float target_amp, const uint32_t chn, const uint32_t n_samples)
{
	uint32_t pos = 0;
	const float  delay = *(self->delay[chn]);
	const float* const input = self->input[chn];
	float* const output = self->output[chn];
	float* const buffer = self->buffer[chn];

	// TODO bridge cycles if smoothing is longer than n_samples
	const int smooth_len = (n_samples >= GAIN_SMOOTH_LEN) ? GAIN_SMOOTH_LEN : n_samples;
	const float amp = self->c_amp[chn];

	if (self->c_dly[chn] != rint(delay)) {
		// TODO x-fade instead of fade out/in
		const int fade_len = (n_samples >= FADE_LEN) ? FADE_LEN : n_samples / 2;

		// fade out
		for (; pos < fade_len; pos++) {
			const float gain = (float)(fade_len - pos) / (float)fade_len;
			DLYWITHGAIN(gain * SMOOTHGAIN)
			INCREMENT_PTRS(chn);
		}

		// update read pointer
		self->r_ptr[chn] += self->c_dly[chn] - rintf(delay);
		if (self->r_ptr[chn] < 0) {
			self->r_ptr[chn] -= MAXDELAY * floor(self->r_ptr[chn] / (float)MAXDELAY);
		}
		self->r_ptr[chn] = self->r_ptr[chn] % MAXDELAY;
		self->c_dly[chn] = rint(delay);

		// fade in
		for (; pos < 2 * fade_len; pos++) {
			const float gain = (float)(pos - fade_len) / (float)fade_len;
			DLYWITHGAIN(gain * amp)
			INCREMENT_PTRS(chn);
		}
	}

	for (; pos < n_samples; pos++) {
		DLYWITHGAIN(amp)
		INCREMENT_PTRS(chn);
	}
	self->c_amp[chn] = target_amp;
}

static float gain_to_db(const float g) {
	if (g <= 0) return -INFINITY;
	return 20.0 * log10(g);
}
static float db_to_gain(const float d) {
	return pow(10, d/20.0);
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
	BalanceControl* self = (BalanceControl*)instance;
	const float balance = *self->balance;
	float gain_left  = 1.0;
	float gain_right = 1.0;

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

	/* report attenuation to UI */
	*(self->atten[0]) = gain_to_db(gain_left);
	*(self->atten[1]) = gain_to_db(gain_right);

	process_channel(self, gain_left,  0, n_samples);
	process_channel(self, gain_right, 1, n_samples);

	int c, i;
	for (c=0; c < CHANNELS; ++c) {
		float sig_max = 0;
		for (i=0; i < n_samples; ++i) {
			sig_max = MAX(sig_max, self->output[c][i]);
		}
		*(self->meter[c]) = gain_to_db(sig_max);
	}
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
	int i;
	BalanceControl* self = (BalanceControl*)calloc(1, sizeof(BalanceControl));
	if (!self) return NULL;

	for (i=0; i < CHANNELS; ++i) {
		self->c_amp[i] = 1.0;
		self->c_dly[i] = 0;
		self->r_ptr[i] = self->w_ptr[i] = 0;
		memset(self->buffer[i], 0, sizeof(float) * MAXDELAY);
	}

	return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	BalanceControl* self = (BalanceControl*)instance;

	switch ((PortIndex)port) {
	case BLC_BALANCE:
		self->balance = data;
		break;
	case BLC_UNIYGAIN:
		self->unitygain = data;
		break;
	case BLC_ATTL:
		self->atten[0] = data;
		break;
	case BLC_ATTR:
		self->atten[1] = data;
		break;
	case BLC_MTRL:
		self->meter[0] = data;
		break;
	case BLC_MTRR:
		self->meter[1] = data;
		break;
	case BLC_DLYL:
		self->delay[0] = data;
		break;
	case BLC_DLYR:
		self->delay[1] = data;
		break;
	case BLC_INL:
		self->input[0] = data;
		break;
	case BLC_INR:
		self->input[1] = data;
		break;
	case BLC_OUTL:
		self->output[0] = data;
		break;
	case BLC_OUTR:
		self->output[1] = data;
		break;
	}
}

static void
cleanup(LV2_Handle instance)
{
	free(instance);
}

const void*
extension_data(const char* uri)
{
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
