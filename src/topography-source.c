/*
Constellations - Topography Lines Source
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <math.h>

#include "shape-defs.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CTOPO_CONTOUR_LEVELS 12.0f

struct ctopo_source {
	obs_source_t *self;

	uint32_t width, height;
	int seed;

	float line_size;
	struct vec4 line_color;

	bool glow;
	struct vec4 glow_color;
	float glow_size;
	bool glow_pulse;
	float glow_pulse_speed;

	bool drift;
	float drift_speed;
	float drift_angle_deg;

	float smoothness;
	float zoom;

	bool grid;
	struct vec4 grid_color;
	float grid_size;

	/* Accumulated map-space slide; integrating each tick means speed and
	 * angle changes steer the drift instead of teleporting the terrain. */
	double drift_x, drift_y;
	double elapsed_time;

	gs_effect_t *effect;
};

static const char *ctopo_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.TopoSource.Name");
}

static void ctopo_update(void *data, obs_data_t *settings)
{
	struct ctopo_source *s = data;

	s->width = (uint32_t)obs_data_get_int(settings, "width");
	s->height = (uint32_t)obs_data_get_int(settings, "height");
	if (s->width == 0)
		s->width = 1920;
	if (s->height == 0)
		s->height = 1080;

	s->seed = (int)obs_data_get_int(settings, "seed");

	s->line_size = (float)obs_data_get_double(settings, "line_size");
	if (s->line_size < 0.5f)
		s->line_size = 0.5f;
	vec4_from_rgba(&s->line_color, (uint32_t)obs_data_get_int(settings, "line_color"));

	s->glow = obs_data_get_bool(settings, "glow");
	vec4_from_rgba(&s->glow_color, (uint32_t)obs_data_get_int(settings, "glow_color"));
	s->glow_size = (float)obs_data_get_double(settings, "glow_size");
	s->glow_pulse = obs_data_get_bool(settings, "glow_pulse");
	s->glow_pulse_speed = (float)obs_data_get_double(settings, "glow_pulse_speed");

	s->drift = obs_data_get_bool(settings, "location_drift");
	s->drift_speed = (float)obs_data_get_double(settings, "location_drift_speed");
	s->drift_angle_deg = (float)obs_data_get_double(settings, "location_drift_angle");

	s->smoothness = (float)obs_data_get_double(settings, "smoothness");
	s->zoom = (float)obs_data_get_double(settings, "zoom");
	if (s->zoom < 0.05f)
		s->zoom = 0.05f;

	s->grid = obs_data_get_bool(settings, "grid_backdrop");
	vec4_from_rgba(&s->grid_color, (uint32_t)obs_data_get_int(settings, "grid_color"));
	s->grid_size = (float)obs_data_get_double(settings, "grid_size");
}

static void *ctopo_create(obs_data_t *settings, obs_source_t *source)
{
	struct ctopo_source *s = bzalloc(sizeof(*s));
	s->self = source;

	char *path = obs_module_file("effects/topography.effect");
	if (path) {
		obs_enter_graphics();
		char *errors = NULL;
		s->effect = gs_effect_create_from_file(path, &errors);
		obs_leave_graphics();
		if (!s->effect)
			obs_log(LOG_ERROR, "Constellations: failed to compile topography.effect: %s",
				errors ? errors : "(no detail)");
		bfree(errors);
		bfree(path);
	} else {
		obs_log(LOG_ERROR, "Constellations: topography.effect not found");
	}

	ctopo_update(s, settings);
	return s;
}

static void ctopo_destroy(void *data)
{
	struct ctopo_source *s = data;
	if (s->effect) {
		obs_enter_graphics();
		gs_effect_destroy(s->effect);
		obs_leave_graphics();
	}
	bfree(s);
}

static void ctopo_tick(void *data, float seconds)
{
	struct ctopo_source *s = data;
	s->elapsed_time += (double)seconds;
	if (s->drift && s->drift_speed != 0.0f) {
		/* Speed is on-screen px/s at zoom 1; one map unit spans 100 px.
		 * Subtracting makes the terrain slide toward the drift angle. */
		double ang = (double)s->drift_angle_deg * (M_PI / 180.0);
		double step = (double)s->drift_speed * (double)seconds / 100.0;
		s->drift_x -= cos(ang) * step;
		s->drift_y -= sin(ang) * step;
	}
}

static void ctopo_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct ctopo_source *s = data;
	if (!s->effect || s->width == 0 || s->height == 0)
		return;

	float glow_level = 1.0f;
	if (s->glow_pulse) {
		float ph = (float)(s->elapsed_time * (double)s->glow_pulse_speed * 2.0 * M_PI);
		glow_level = 0.15f + 0.85f * (0.5f + 0.5f * sinf(ph));
	}

	gs_eparam_t *p;
	struct vec2 v2;

	vec2_set(&v2, (float)s->width, (float)s->height);
	p = gs_effect_get_param_by_name(s->effect, "canvas_size");
	if (p)
		gs_effect_set_vec2(p, &v2);
	p = gs_effect_get_param_by_name(s->effect, "noise_seed");
	if (p)
		gs_effect_set_float(p, (float)s->seed);
	p = gs_effect_get_param_by_name(s->effect, "zoom_amount");
	if (p)
		gs_effect_set_float(p, s->zoom);
	p = gs_effect_get_param_by_name(s->effect, "smoothness");
	if (p)
		gs_effect_set_float(p, s->smoothness);
	vec2_set(&v2, (float)s->drift_x, (float)s->drift_y);
	p = gs_effect_get_param_by_name(s->effect, "drift_offset");
	if (p)
		gs_effect_set_vec2(p, &v2);
	p = gs_effect_get_param_by_name(s->effect, "contour_levels");
	if (p)
		gs_effect_set_float(p, CTOPO_CONTOUR_LEVELS);

	p = gs_effect_get_param_by_name(s->effect, "line_half_px");
	if (p)
		gs_effect_set_float(p, s->line_size * 0.5f);
	p = gs_effect_get_param_by_name(s->effect, "line_color");
	if (p)
		gs_effect_set_vec4(p, &s->line_color);

	p = gs_effect_get_param_by_name(s->effect, "glow_enabled");
	if (p)
		gs_effect_set_float(p, s->glow ? 1.0f : 0.0f);
	p = gs_effect_get_param_by_name(s->effect, "glow_size_px");
	if (p)
		gs_effect_set_float(p, s->glow_size);
	p = gs_effect_get_param_by_name(s->effect, "glow_level");
	if (p)
		gs_effect_set_float(p, glow_level);
	p = gs_effect_get_param_by_name(s->effect, "glow_color");
	if (p)
		gs_effect_set_vec4(p, &s->glow_color);

	p = gs_effect_get_param_by_name(s->effect, "grid_enabled");
	if (p)
		gs_effect_set_float(p, s->grid ? 1.0f : 0.0f);
	p = gs_effect_get_param_by_name(s->effect, "grid_size_px");
	if (p)
		gs_effect_set_float(p, s->grid_size);
	p = gs_effect_get_param_by_name(s->effect, "grid_color");
	if (p)
		gs_effect_set_vec4(p, &s->grid_color);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(s->effect, "Draw"))
		gs_draw_sprite(NULL, 0, s->width, s->height);
	gs_blend_state_pop();
}

static uint32_t ctopo_width(void *data)
{
	struct ctopo_source *s = data;
	return s->width;
}

static uint32_t ctopo_height(void *data)
{
	struct ctopo_source *s = data;
	return s->height;
}

static bool ctopo_glow_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool glow = obs_data_get_bool(settings, "glow");
	bool pulse = obs_data_get_bool(settings, "glow_pulse");
	obs_property_t *pp;
	pp = obs_properties_get(props, "glow_color");
	if (pp)
		obs_property_set_visible(pp, glow);
	pp = obs_properties_get(props, "glow_size");
	if (pp)
		obs_property_set_visible(pp, glow);
	pp = obs_properties_get(props, "glow_pulse");
	if (pp)
		obs_property_set_visible(pp, glow);
	pp = obs_properties_get(props, "glow_pulse_speed");
	if (pp)
		obs_property_set_visible(pp, glow && pulse);
	return true;
}

static bool ctopo_drift_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool drift = obs_data_get_bool(settings, "location_drift");
	obs_property_t *pp;
	pp = obs_properties_get(props, "location_drift_speed");
	if (pp)
		obs_property_set_visible(pp, drift);
	pp = obs_properties_get(props, "location_drift_angle");
	if (pp)
		obs_property_set_visible(pp, drift);
	return true;
}

static bool ctopo_grid_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool grid = obs_data_get_bool(settings, "grid_backdrop");
	obs_property_t *pp;
	pp = obs_properties_get(props, "grid_color");
	if (pp)
		obs_property_set_visible(pp, grid);
	pp = obs_properties_get(props, "grid_size");
	if (pp)
		obs_property_set_visible(pp, grid);
	return true;
}

static obs_properties_t *ctopo_props(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_properties_t *canvas = obs_properties_create();
	obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
	obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
	obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
				 OBS_GROUP_NORMAL, canvas);

	obs_properties_t *terrain = obs_properties_create();
	obs_properties_add_int_slider(terrain, "seed", obs_module_text("Constellations.Topo.Seed"), 0, 10000, 1);
	obs_properties_add_float_slider(terrain, "zoom", obs_module_text("Constellations.Topo.Zoom"), 0.1, 10.0, 0.01);
	obs_properties_add_float_slider(terrain, "smoothness", obs_module_text("Constellations.Topo.Smoothness"), 0.0,
					1.0, 0.01);
	obs_property_t *ld =
		obs_properties_add_bool(terrain, "location_drift", obs_module_text("Constellations.Topo.Drift"));
	obs_property_set_modified_callback(ld, ctopo_drift_modified);
	obs_properties_add_float_slider(terrain, "location_drift_speed",
					obs_module_text("Constellations.Topo.DriftSpeed"), 0.0, 500.0, 1.0);
	obs_properties_add_float_slider(terrain, "location_drift_angle",
					obs_module_text("Constellations.Topo.DriftAngle"), 0.0, 360.0, 1.0);
	obs_properties_add_group(props, "terrain_group", obs_module_text("Constellations.Topo.Group.Terrain"),
				 OBS_GROUP_NORMAL, terrain);

	obs_properties_t *lines = obs_properties_create();
	obs_properties_add_float_slider(lines, "line_size", obs_module_text("Constellations.Topo.LineSize"), 0.5, 20.0,
					0.1);
	obs_properties_add_color_alpha(lines, "line_color", obs_module_text("Constellations.Topo.LineColor"));
	obs_property_t *gl = obs_properties_add_bool(lines, "glow", obs_module_text("Constellations.Topo.Glow"));
	obs_property_set_modified_callback(gl, ctopo_glow_modified);
	obs_properties_add_color_alpha(lines, "glow_color", obs_module_text("Constellations.Topo.GlowColor"));
	obs_properties_add_float_slider(lines, "glow_size", obs_module_text("Constellations.Topo.GlowSize"), 1.0, 100.0,
					0.5);
	obs_property_t *gp =
		obs_properties_add_bool(lines, "glow_pulse", obs_module_text("Constellations.Topo.GlowPulse"));
	obs_property_set_modified_callback(gp, ctopo_glow_modified);
	obs_properties_add_float_slider(lines, "glow_pulse_speed",
					obs_module_text("Constellations.Topo.GlowPulseSpeed"), 0.05, 5.0, 0.05);
	obs_properties_add_group(props, "lines_group", obs_module_text("Constellations.Topo.Group.Lines"),
				 OBS_GROUP_NORMAL, lines);

	obs_properties_t *grid = obs_properties_create();
	obs_property_t *ge =
		obs_properties_add_bool(grid, "grid_backdrop", obs_module_text("Constellations.Topo.GridEnabled"));
	obs_property_set_modified_callback(ge, ctopo_grid_modified);
	obs_properties_add_color_alpha(grid, "grid_color", obs_module_text("Constellations.Topo.GridColor"));
	obs_properties_add_float_slider(grid, "grid_size", obs_module_text("Constellations.Topo.GridSize"), 10.0, 500.0,
					1.0);
	obs_properties_add_group(props, "grid_group", obs_module_text("Constellations.Topo.Group.Grid"),
				 OBS_GROUP_NORMAL, grid);

	return props;
}

static void ctopo_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	obs_data_set_default_int(settings, "seed", 1234);
	obs_data_set_default_double(settings, "zoom", 1.0);
	obs_data_set_default_double(settings, "smoothness", 1.0);
	obs_data_set_default_bool(settings, "location_drift", false);
	obs_data_set_default_double(settings, "location_drift_speed", 20.0);
	obs_data_set_default_double(settings, "location_drift_angle", 0.0);
	obs_data_set_default_double(settings, "line_size", 2.0);
	obs_data_set_default_int(settings, "line_color", 0xFFFFFFFF);
	obs_data_set_default_bool(settings, "glow", false);
	obs_data_set_default_int(settings, "glow_color", 0xFFFF9632);
	obs_data_set_default_double(settings, "glow_size", 12.0);
	obs_data_set_default_bool(settings, "glow_pulse", false);
	obs_data_set_default_double(settings, "glow_pulse_speed", 0.5);
	obs_data_set_default_bool(settings, "grid_backdrop", false);
	obs_data_set_default_int(settings, "grid_color", 0x64808080);
	obs_data_set_default_double(settings, "grid_size", 100.0);
}

static struct obs_source_info ctopo_source_info = {
	.id = "constellations_topography_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = ctopo_get_name,
	.create = ctopo_create,
	.destroy = ctopo_destroy,
	.update = ctopo_update,
	.video_tick = ctopo_tick,
	.video_render = ctopo_render,
	.get_width = ctopo_width,
	.get_height = ctopo_height,
	.get_properties = ctopo_props,
	.get_defaults = ctopo_defaults,
};

void constellations_register_topography_source(void)
{
	obs_register_source(&ctopo_source_info);
}
