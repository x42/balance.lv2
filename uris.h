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

#ifndef BLC_URIS_H
#define BLC_URIS_H

#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"

#define BLC_URI "http://gareus.org/oss/lv2/balance"

#define BLC__cckey    BLC_URI "#controlkey"
#define BLC__ccval    BLC_URI "#controlval"
#define BLC__control  BLC_URI "#control"
#define BLC__meteron  BLC_URI "#meteron"
#define BLC__meteroff BLC_URI "#meteroff"
#define BLC__meterint BLC_URI "#meterint"

typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Path;
	LV2_URID atom_String;
	LV2_URID atom_Int;
	LV2_URID atom_URID;
	LV2_URID atom_eventTransfer;
	LV2_URID atom_Sequence;

	LV2_URID blc_control;
	LV2_URID blc_cckey;
	LV2_URID blc_ccval;

	LV2_URID blc_meters_on;
	LV2_URID blc_meters_off;
	LV2_URID blc_meters_int;

} balanceURIs;


// numeric keys
enum {
	KEY_INVALID = 0,
	GAIN_LEFT,
	GAIN_RIGHT,
	DELAY_LEFT,
	DELAY_RIGHT,
	METER_IN_LEFT,
	METER_IN_RIGHT,
	METER_OUT_LEFT,
	METER_OUT_RIGHT,
	PEAK_IN_LEFT,
	PEAK_IN_RIGHT,
	PEAK_OUT_LEFT,
	PEAK_OUT_RIGHT,
	PHASE_OUT
};


static inline void
map_balance_uris(LV2_URID_Map* map, balanceURIs* uris)
{
	uris->atom_Blank         = map->map(map->handle, LV2_ATOM__Blank);
	uris->atom_Path          = map->map(map->handle, LV2_ATOM__Path);
	uris->atom_String        = map->map(map->handle, LV2_ATOM__String);
	uris->atom_Int           = map->map(map->handle, LV2_ATOM__Int);
	uris->atom_URID          = map->map(map->handle, LV2_ATOM__URID);
	uris->atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
  uris->atom_Sequence      = map->map(map->handle, LV2_ATOM__Sequence);
	uris->blc_cckey          = map->map(map->handle, BLC__cckey);
	uris->blc_ccval          = map->map(map->handle, BLC__ccval);
	uris->blc_control        = map->map(map->handle, BLC__control);

	uris->blc_meters_on       = map->map(map->handle, BLC__meteron);
	uris->blc_meters_off      = map->map(map->handle, BLC__meteroff);
	uris->blc_meters_int      = map->map(map->handle, BLC__meterint);
}


static inline LV2_Atom *
forge_kvcontrolmessage(LV2_Atom_Forge* forge,
		const balanceURIs* uris,
		const int key, const float value)
{
	//printf("UIcom: Tx '%s' -> %d \n", key, value);

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time(forge, 0);
	LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_blank(forge, &frame, 1, uris->blc_control);

	lv2_atom_forge_property_head(forge, uris->blc_cckey, 0);
	lv2_atom_forge_int(forge, key);
	lv2_atom_forge_property_head(forge, uris->blc_ccval, 0);
	lv2_atom_forge_float(forge, value);
	lv2_atom_forge_pop(forge, &frame);
	return msg;
}

static inline int
get_cc_key_value(
		const balanceURIs* uris, const LV2_Atom_Object* obj,
		int *k, float *v)
{
	const LV2_Atom* key = NULL;
	const LV2_Atom* value = NULL;
	if (!k || !v) return -1;
	*k = 0; *v = 0.0;

	if (obj->body.otype != uris->blc_control) {
		return -1;
	}
	lv2_atom_object_get(obj, uris->blc_cckey, &key, uris->blc_ccval, &value, 0);
	if (!key || !value) {
		fprintf(stderr, "BLClv2: Malformed ctrl message has no key or value.\n");
		return -1;
	}
	*k = ((LV2_Atom_Int*)key)->body;
	*v = ((LV2_Atom_Float*)value)->body;
	return 0;
}

#endif
