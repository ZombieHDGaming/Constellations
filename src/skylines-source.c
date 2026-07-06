/*
Constellations - Skylines Background / Filter
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>

#include "shape-defs.h"

struct csky_source {
	obs_source_t *self;
	bool is_filter;

	uint32_t width, height;
	int seed;

	float height_variation;
	struct vec4 building_color;
	struct vec4 window_on_color;
	struct vec4 window_off_color;

	bool drift;
	float drift_speed;

	bool twinkle;
	float twinkle_speed;

	/* Integrated each tick so speed changes steer the motion instead of
	 * teleporting the skyline or snapping every window's fade. */
	double drift_px;
	double twinkle_phase;

	gs_effect_t *effect;
};

static const char *csky_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.SkylineSource.Name");
}

static const char *csky_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.SkylineFilter.Name");
}

static void csky_update(void *data, obs_data_t *settings)
{
	struct csky_source *s = data;

	if (!s->is_filter) {
		s->width = (uint32_t)obs_data_get_int(settings, "width");
		s->height = (uint32_t)obs_data_get_int(settings, "height");
		if (s->width == 0)
			s->width = 1920;
		if (s->height == 0)
			s->height = 1080;
	}

	s->seed = (int)obs_data_get_int(settings, "seed");

	s->height_variation = (float)obs_data_get_double(settings, "height_variation");
	vec4_from_rgba(&s->building_color, (uint32_t)obs_data_get_int(settings, "building_color"));
	vec4_from_rgba(&s->window_on_color, (uint32_t)obs_data_get_int(settings, "window_on_color"));
	vec4_from_rgba(&s->window_off_color, (uint32_t)obs_data_get_int(settings, "window_off_color"));

	s->drift = obs_data_get_bool(settings, "location_drift");
	s->drift_speed = (float)obs_data_get_double(settings, "location_drift_speed");

	s->twinkle = obs_data_get_bool(settings, "window_twinkle");
	s->twinkle_speed = (float)obs_data_get_double(settings, "twinkle_speed");
	if (s->twinkle_speed < 0.0f)
		s->twinkle_speed = 0.0f;
}

static void *csky_create_common(obs_data_t *settings, obs_source_t *source, bool is_filter)
{
	struct csky_source *s = bzalloc(sizeof(*s));
	s->self = source;
	s->is_filter = is_filter;

	char *path = obs_module_file("effects/skylines.effect");
	if (path) {
		obs_enter_graphics();
		char *errors = NULL;
		s->effect = gs_effect_create_from_file(path, &errors);
		obs_leave_graphics();
		if (!s->effect)
			obs_log(LOG_ERROR, "Constellations: failed to compile skylines.effect: %s",
				errors ? errors : "(no detail)");
		bfree(errors);
		bfree(path);
	} else {
		obs_log(LOG_ERROR, "Constellations: skylines.effect not found");
	}

	csky_update(s, settings);
	return s;
}

static void *csky_source_create(obs_data_t *settings, obs_source_t *source)
{
	return csky_create_common(settings, source, false);
}

static void *csky_filter_create(obs_data_t *settings, obs_source_t *source)
{
	return csky_create_common(settings, source, true);
}

static void csky_destroy(void *data)
{
	struct csky_source *s = data;
	if (s->effect) {
		obs_enter_graphics();
		gs_effect_destroy(s->effect);
		obs_leave_graphics();
	}
	bfree(s);
}

static void csky_tick(void *data, float seconds)
{
	struct csky_source *s = data;
	/* Positive drift speed slides the buildings to the right. */
	if (s->drift && s->drift_speed != 0.0f)
		s->drift_px -= (double)s->drift_speed * (double)seconds;
	if (s->twinkle && s->twinkle_speed > 0.0f)
		s->twinkle_phase += (double)s->twinkle_speed * (double)seconds;
}

static void csky_draw(struct csky_source *s, uint32_t width, uint32_t height)
{
	if (!s->effect || width == 0 || height == 0)
		return;

	bool twinkle_active = s->twinkle && s->twinkle_speed > 0.0f;

	gs_eparam_t *p;
	struct vec2 v2;

	vec2_set(&v2, (float)width, (float)height);
	p = gs_effect_get_param_by_name(s->effect, "canvas_size");
	if (p)
		gs_effect_set_vec2(p, &v2);
	p = gs_effect_get_param_by_name(s->effect, "noise_seed");
	if (p)
		gs_effect_set_float(p, (float)s->seed);
	p = gs_effect_get_param_by_name(s->effect, "drift_px");
	if (p)
		gs_effect_set_float(p, (float)s->drift_px);
	p = gs_effect_get_param_by_name(s->effect, "height_variation");
	if (p)
		gs_effect_set_float(p, s->height_variation);
	p = gs_effect_get_param_by_name(s->effect, "building_color");
	if (p)
		gs_effect_set_vec4(p, &s->building_color);
	p = gs_effect_get_param_by_name(s->effect, "window_on_color");
	if (p)
		gs_effect_set_vec4(p, &s->window_on_color);
	p = gs_effect_get_param_by_name(s->effect, "window_off_color");
	if (p)
		gs_effect_set_vec4(p, &s->window_off_color);
	p = gs_effect_get_param_by_name(s->effect, "twinkle_active");
	if (p)
		gs_effect_set_float(p, twinkle_active ? 1.0f : 0.0f);
	p = gs_effect_get_param_by_name(s->effect, "twinkle_phase");
	if (p)
		gs_effect_set_float(p, (float)s->twinkle_phase);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(s->effect, "Draw"))
		gs_draw_sprite(NULL, 0, width, height);
	gs_blend_state_pop();
}

static void csky_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct csky_source *s = data;
	csky_draw(s, s->width, s->height);
}

static void csky_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct csky_source *s = data;

	obs_source_t *target = obs_filter_get_target(s->self);
	uint32_t w = target ? obs_source_get_base_width(target) : 0;
	uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(s->self);
		return;
	}

	if (!obs_source_process_filter_begin(s->self, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;
	obs_source_process_filter_end(s->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), w, h);

	csky_draw(s, w, h);
}

static uint32_t csky_width(void *data)
{
	struct csky_source *s = data;
	return s->width;
}

static uint32_t csky_height(void *data)
{
	struct csky_source *s = data;
	return s->height;
}

static bool csky_drift_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool drift = obs_data_get_bool(settings, "location_drift");
	obs_property_t *pp = obs_properties_get(props, "location_drift_speed");
	if (pp)
		obs_property_set_visible(pp, drift);
	return true;
}

static bool csky_twinkle_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool twinkle = obs_data_get_bool(settings, "window_twinkle");
	obs_property_t *pp = obs_properties_get(props, "twinkle_speed");
	if (pp)
		obs_property_set_visible(pp, twinkle);
	return true;
}

static obs_properties_t *csky_props_common(bool is_filter)
{
	obs_properties_t *props = obs_properties_create();

	if (!is_filter) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	}

	obs_properties_t *buildings = obs_properties_create();
	obs_properties_add_int_slider(buildings, "seed", obs_module_text("Constellations.Skyline.Seed"), 0, 10000, 1);
	obs_properties_add_float_slider(buildings, "height_variation",
					obs_module_text("Constellations.Skyline.HeightVariation"), 0.0, 1.0, 0.01);
	obs_properties_add_color_alpha(buildings, "building_color",
				       obs_module_text("Constellations.Skyline.BuildingColor"));
	obs_properties_add_group(props, "buildings_group", obs_module_text("Constellations.Skyline.Group.Buildings"),
				 OBS_GROUP_NORMAL, buildings);

	obs_properties_t *windows = obs_properties_create();
	obs_properties_add_color_alpha(windows, "window_on_color",
				       obs_module_text("Constellations.Skyline.WindowOnColor"));
	obs_properties_add_color_alpha(windows, "window_off_color",
				       obs_module_text("Constellations.Skyline.WindowOffColor"));
	obs_property_t *tw =
		obs_properties_add_bool(windows, "window_twinkle", obs_module_text("Constellations.Skyline.Twinkle"));
	obs_property_set_modified_callback(tw, csky_twinkle_modified);
	obs_properties_add_float_slider(windows, "twinkle_speed",
					obs_module_text("Constellations.Skyline.TwinkleSpeed"), 0.0, 5.0, 0.05);
	obs_properties_add_group(props, "windows_group", obs_module_text("Constellations.Skyline.Group.Windows"),
				 OBS_GROUP_NORMAL, windows);

	obs_properties_t *motion = obs_properties_create();
	obs_property_t *ld =
		obs_properties_add_bool(motion, "location_drift", obs_module_text("Constellations.Skyline.Drift"));
	obs_property_set_modified_callback(ld, csky_drift_modified);
	obs_properties_add_float_slider(motion, "location_drift_speed",
					obs_module_text("Constellations.Skyline.DriftSpeed"), -500.0, 500.0, 1.0);
	obs_properties_add_group(props, "motion_group", obs_module_text("Constellations.Group.Motion"),
				 OBS_GROUP_NORMAL, motion);

	return props;
}

static obs_properties_t *csky_source_props(void *data)
{
	UNUSED_PARAMETER(data);
	return csky_props_common(false);
}

static obs_properties_t *csky_filter_props(void *data)
{
	UNUSED_PARAMETER(data);
	return csky_props_common(true);
}

static void csky_defaults_common(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "seed", 1234);
	obs_data_set_default_double(settings, "height_variation", 0.5);
	obs_data_set_default_int(settings, "building_color", 0xFF3A2A1E);
	obs_data_set_default_int(settings, "window_on_color", 0xFF66D9FF);
	obs_data_set_default_int(settings, "window_off_color", 0xFF241A12);
	obs_data_set_default_bool(settings, "location_drift", false);
	obs_data_set_default_double(settings, "location_drift_speed", 20.0);
	obs_data_set_default_bool(settings, "window_twinkle", false);
	obs_data_set_default_double(settings, "twinkle_speed", 1.0);
}

static void csky_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	csky_defaults_common(settings);
}

static void csky_filter_defaults(obs_data_t *settings)
{
	csky_defaults_common(settings);
}

static struct obs_source_info csky_source_info = {
	.id = "constellations_skylines_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = csky_source_get_name,
	.create = csky_source_create,
	.destroy = csky_destroy,
	.update = csky_update,
	.video_tick = csky_tick,
	.video_render = csky_source_render,
	.get_width = csky_width,
	.get_height = csky_height,
	.get_properties = csky_source_props,
	.get_defaults = csky_source_defaults,
};

static struct obs_source_info csky_filter_info = {
	.id = "constellations_skylines_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = csky_filter_get_name,
	.create = csky_filter_create,
	.destroy = csky_destroy,
	.update = csky_update,
	.video_tick = csky_tick,
	.video_render = csky_filter_render,
	.get_properties = csky_filter_props,
	.get_defaults = csky_filter_defaults,
};

void constellations_register_skylines(void)
{
	obs_register_source(&csky_source_info);
	obs_register_source(&csky_filter_info);
}
