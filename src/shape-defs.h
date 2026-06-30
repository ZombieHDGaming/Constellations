/*
Constellations
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

enum constellations_shape {
	CSHAPE_CIRCLE = 0,
	CSHAPE_CROSS = 1,
	CSHAPE_SQUARE = 2,
	CSHAPE_POLYGON = 3,
	CSHAPE_SQUIRCLE = 4,
	CSHAPE_HLINE = 5,
	CSHAPE_VLINE = 6,
};

enum constellations_item_kind {
	CITEM_SHAPE = 0,
	CITEM_IMAGE = 1,
	CITEM_SOURCE = 2,
};

enum constellations_vignette_shape {
	CVIGN_CIRCLE = 0,
	CVIGN_LINEAR = 1,
	CVIGN_SQUARE = 2,
	CVIGN_POLYGON = 3,
};

enum constellations_transition_mode {
	CTRANS_MODE_COVER = 0,
	CTRANS_MODE_DISSOLVE = 1,
	CTRANS_MODE_SHAPE_ZOOM = 2,
};

enum constellations_transition_type {
	CTRANS_TYPE_DIRECTIONAL = 0,
	CTRANS_TYPE_INSIDE_OUT = 1,
	CTRANS_TYPE_OUTSIDE_IN = 2,
	CTRANS_TYPE_MIRRORED = 3,
};

enum constellations_dissolve_type {
	CDISSOLVE_CROP_SHRINK = 0,
	CDISSOLVE_SCALE_SHRINK = 1,
	CDISSOLVE_FADE = 2,
};

enum constellations_logo_display {
	CLOGO_FADE = 0,
	CLOGO_SLIDE = 1,
};

enum constellations_shape_zoom_dir {
	CZOOM_IN = 0,
	CZOOM_OUT = 1,
};

#define CONSTELLATIONS_MAX_ITEMS 32

void constellations_register_pattern_source(void);
void constellations_register_pattern_filter(void);
void constellations_register_shapes_transition(void);
