/*
Constellations - Shared pattern renderer
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <obs-module.h>
#include <graphics/image-file.h>
#include "shape-defs.h"

struct cpat_item {
	bool enabled;
	int kind;
	int shape;
	int polygon_sides;
	float ring_thickness;
	bool outline_only;
	struct vec4 color;

	char *image_path;
	gs_image_file_t image;
	bool image_loaded;

	char *source_name;
	obs_weak_source_t *source_ref;
	gs_texrender_t *source_texrender;
	uint32_t source_w, source_h;

	float size;
	float rotation_deg;
	float density;

	float sub_offset_x;
	float sub_offset_y;
};

struct cpat_renderer {
	obs_source_t *owner;

	uint32_t width, height;
	bool has_background;
	struct vec4 background_color;
	float anchor_x_pct;
	float anchor_y_pct;

	uint32_t item_count;
	struct cpat_item items[CONSTELLATIONS_MAX_ITEMS];

	float motion_angle_deg;
	float motion_speed;
	bool alternating_lines;
	bool speed_drift;
	float speed_drift_amount;
	bool location_drift;
	float location_drift_amount;

	bool vignette_enabled;
	float vignette_size;
	float vignette_direction_deg;
	int vignette_shape;
	int vignette_polygon_sides;
	float vignette_softness;
	bool vignette_inverted;

	double phase;

	gs_effect_t *effect;
};

void cpat_renderer_init(struct cpat_renderer *r, obs_source_t *owner);
void cpat_renderer_free(struct cpat_renderer *r);

void cpat_renderer_set_defaults(obs_data_t *settings, bool include_canvas);
void cpat_renderer_get_properties(struct cpat_renderer *r, obs_properties_t *props, bool include_canvas);
void cpat_renderer_update(struct cpat_renderer *r, obs_data_t *settings, bool include_canvas);

void cpat_renderer_tick(struct cpat_renderer *r, float seconds);
void cpat_renderer_render_background(struct cpat_renderer *r);
void cpat_renderer_render_items(struct cpat_renderer *r, uint32_t w, uint32_t h);
