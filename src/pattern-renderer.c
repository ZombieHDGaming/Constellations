/*
Constellations - Shared pattern renderer
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "pattern-renderer.h"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void cpat_item_release_source(struct cpat_item *it)
{
	if (it->source_ref) {
		obs_weak_source_release(it->source_ref);
		it->source_ref = NULL;
	}
	if (it->source_texrender) {
		obs_enter_graphics();
		gs_texrender_destroy(it->source_texrender);
		obs_leave_graphics();
		it->source_texrender = NULL;
	}
}

static void cpat_item_release_image(struct cpat_item *it)
{
	if (it->image_loaded) {
		obs_enter_graphics();
		gs_image_file_free(&it->image);
		obs_leave_graphics();
		it->image_loaded = false;
	}
}

static void cpat_item_acquire_image(struct cpat_item *it)
{
	cpat_item_release_image(it);
	if (!it->image_path || !*it->image_path)
		return;
	gs_image_file_init(&it->image, it->image_path);
	obs_enter_graphics();
	gs_image_file_init_texture(&it->image);
	obs_leave_graphics();
	it->image_loaded = it->image.loaded;
}

static void cpat_item_acquire_source(struct cpat_item *it, obs_source_t *exclude)
{
	cpat_item_release_source(it);
	if (!it->source_name || !*it->source_name)
		return;
	obs_source_t *src = obs_get_source_by_name(it->source_name);
	if (!src)
		return;
	if (src == exclude) {
		obs_source_release(src);
		return;
	}
	it->source_ref = obs_source_get_weak_source(src);
	obs_source_release(src);

	obs_enter_graphics();
	it->source_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();
}

static void cpat_item_clear(struct cpat_item *it)
{
	cpat_item_release_image(it);
	cpat_item_release_source(it);
	bfree(it->image_path);
	bfree(it->source_name);
	memset(it, 0, sizeof(*it));
}

void cpat_renderer_init(struct cpat_renderer *r, obs_source_t *owner)
{
	memset(r, 0, sizeof(*r));
	r->owner = owner;

	char *path = obs_module_file("effects/pattern.effect");
	if (!path) {
		obs_log(LOG_ERROR, "Constellations: pattern.effect not found");
		return;
	}
	obs_enter_graphics();
	char *errors = NULL;
	r->effect = gs_effect_create_from_file(path, &errors);
	obs_leave_graphics();
	if (!r->effect)
		obs_log(LOG_ERROR, "Constellations: failed to compile pattern.effect: %s",
			errors ? errors : "(no detail)");
	bfree(errors);
	bfree(path);
}

void cpat_renderer_free(struct cpat_renderer *r)
{
	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i)
		cpat_item_clear(&r->items[i]);
	if (r->effect) {
		obs_enter_graphics();
		gs_effect_destroy(r->effect);
		obs_leave_graphics();
		r->effect = NULL;
	}
}

void cpat_renderer_set_defaults(obs_data_t *settings, bool include_canvas)
{
	if (include_canvas) {
		obs_data_set_default_int(settings, "width", 1920);
		obs_data_set_default_int(settings, "height", 1080);
		obs_data_set_default_int(settings, "background_color", 0xFF101820);
	}
	obs_data_set_default_double(settings, "anchor_x_pct", 50.0);
	obs_data_set_default_double(settings, "anchor_y_pct", 50.0);
	obs_data_set_default_int(settings, "item_count", 1);
	obs_data_set_default_int(settings, "layout_mode", CLAYOUT_LAYERED);

	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i) {
		char key[64];
#define DK(name) (snprintf(key, sizeof(key), "item_%d_%s", i + 1, name), key)
		obs_data_set_default_bool(settings, DK("enabled"), i == 0);
		obs_data_set_default_int(settings, DK("kind"), CITEM_SHAPE);
		obs_data_set_default_int(settings, DK("shape"), CSHAPE_CIRCLE);
		obs_data_set_default_int(settings, DK("polygon_sides"), 6);
		obs_data_set_default_double(settings, DK("stroke_thickness"), 0.2);
		obs_data_set_default_bool(settings, DK("outline_only"), false);
		obs_data_set_default_int(settings, DK("color"), 0xFFFFFFFF);
		obs_data_set_default_string(settings, DK("image_path"), "");
		obs_data_set_default_string(settings, DK("source_name"), "");
		obs_data_set_default_double(settings, DK("size"), 24.0);
		obs_data_set_default_double(settings, DK("rotation"), 0.0);
		obs_data_set_default_double(settings, DK("density"), 2.0);
#undef DK
	}

	obs_data_set_default_double(settings, "motion_angle", 0.0);
	obs_data_set_default_double(settings, "motion_speed", 0.0);
	obs_data_set_default_bool(settings, "alternating_lines", false);
	obs_data_set_default_bool(settings, "speed_drift", false);
	obs_data_set_default_double(settings, "speed_drift_amount", 0.25);
	obs_data_set_default_bool(settings, "location_drift", false);
	obs_data_set_default_double(settings, "location_drift_amount", 0.15);

	obs_data_set_default_bool(settings, "vignette_enabled", false);
	obs_data_set_default_double(settings, "vignette_size", 0.7);
	obs_data_set_default_double(settings, "vignette_direction", 0.0);
	obs_data_set_default_int(settings, "vignette_shape", CVIGN_CIRCLE);
	obs_data_set_default_int(settings, "vignette_polygon_sides", 6);
	obs_data_set_default_double(settings, "vignette_softness", 0.35);
	obs_data_set_default_bool(settings, "vignette_inverted", false);
}

struct enum_src_ctx {
	obs_property_t *list;
	DARRAY(const char *) names;
	obs_source_t *self;
};

static bool enum_collect_source(void *data, obs_source_t *src)
{
	struct enum_src_ctx *ctx = data;
	if (src == ctx->self)
		return true;
	uint32_t flags = obs_source_get_output_flags(src);
	if ((flags & OBS_SOURCE_VIDEO) == 0)
		return true;
	enum obs_source_type type = obs_source_get_type(src);
	if (type == OBS_SOURCE_TYPE_TRANSITION)
		return true;
	const char *name = obs_source_get_name(src);
	if (!name)
		return true;
	da_push_back(ctx->names, &name);
	return true;
}

static int compare_strs_ci(const void *a, const void *b)
{
	const char *const *sa = a;
	const char *const *sb = b;
	return astrcmpi(*sa, *sb);
}

static void fill_source_dropdown(obs_property_t *list, obs_source_t *self_source)
{
	obs_property_list_clear(list);
	obs_property_list_add_string(list, obs_module_text("Constellations.Source.None"), "");
	struct enum_src_ctx ctx = {0};
	ctx.list = list;
	ctx.self = self_source;
	da_init(ctx.names);
	obs_enum_sources(enum_collect_source, &ctx);
	qsort(ctx.names.array, ctx.names.num, sizeof(char *), compare_strs_ci);
	for (size_t i = 0; i < ctx.names.num; ++i)
		obs_property_list_add_string(list, ctx.names.array[i], ctx.names.array[i]);
	da_free(ctx.names);
}

static int item_index_from_prop(obs_property_t *p)
{
	const char *name = obs_property_name(p);
	if (!name)
		return -1;
	int n = 0;
	if (sscanf(name, "item_%d_", &n) != 1)
		return -1;
	return n - 1;
}

static void apply_item_kind_visibility(obs_properties_t *props, obs_data_t *settings, int i)
{
	char key[64];
	snprintf(key, sizeof(key), "item_%d_kind", i + 1);
	int kind = (int)obs_data_get_int(settings, key);
	snprintf(key, sizeof(key), "item_%d_shape", i + 1);
	int shape_v = (int)obs_data_get_int(settings, key);
	snprintf(key, sizeof(key), "item_%d_outline_only", i + 1);
	bool outline = obs_data_get_bool(settings, key);

#define VIS(name_, on)                                                \
	do {                                                          \
		snprintf(key, sizeof(key), "item_%d_%s", i + 1, name_); \
		obs_property_t *_pp = obs_properties_get(props, key); \
		if (_pp)                                              \
			obs_property_set_visible(_pp, on);            \
	} while (0)

	VIS("shape", kind == CITEM_SHAPE);
	VIS("outline_only", kind == CITEM_SHAPE);
	VIS("polygon_sides", kind == CITEM_SHAPE && shape_v == CSHAPE_POLYGON);
	VIS("stroke_thickness", kind == CITEM_SHAPE && (shape_v == CSHAPE_CROSS || outline));
	VIS("color", kind == CITEM_SHAPE);
	VIS("image_path", kind == CITEM_IMAGE);
	VIS("source_name", kind == CITEM_SOURCE);
#undef VIS
}

static void apply_item_enabled_visibility(obs_properties_t *props, obs_data_t *settings, int i)
{
	char key[64];
	snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
	bool en = obs_data_get_bool(settings, key);
	const char *names[] = {"kind",        "shape",     "outline_only", "polygon_sides", "stroke_thickness",
			       "color",       "image_path", "source_name",  "size",          "rotation",
			       "density"};
	for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); ++k) {
		snprintf(key, sizeof(key), "item_%d_%s", i + 1, names[k]);
		obs_property_t *pp = obs_properties_get(props, key);
		if (pp)
			obs_property_set_visible(pp, en);
	}
	if (en)
		apply_item_kind_visibility(props, settings, i);
}

static void apply_item_count_visibility(obs_properties_t *props, obs_data_t *settings)
{
	int count = (int)obs_data_get_int(settings, "item_count");
	if (count < 1)
		count = 1;
	if (count > CONSTELLATIONS_MAX_ITEMS)
		count = CONSTELLATIONS_MAX_ITEMS;
	char gid[64];
	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i) {
		snprintf(gid, sizeof(gid), "item_%d_group", i + 1);
		obs_property_t *gp = obs_properties_get(props, gid);
		if (gp)
			obs_property_set_visible(gp, i < count);
	}
}

static void apply_density_visibility(obs_properties_t *props, obs_data_t *settings)
{
	int mode = (int)obs_data_get_int(settings, "layout_mode");
	char key[64];
	int first_enabled = -1;
	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; i++) {
		snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
		if (obs_data_get_bool(settings, key)) {
			first_enabled = i;
			break;
		}
	}
	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; i++) {
		snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
		bool en = obs_data_get_bool(settings, key);
		snprintf(key, sizeof(key), "item_%d_density", i + 1);
		obs_property_t *dp = obs_properties_get(props, key);
		if (!dp)
			continue;
		bool show = en;
		if (mode == CLAYOUT_STEP_REPEAT)
			show = en && (i == first_enabled);
		obs_property_set_visible(dp, show);
	}
}

static void apply_motion_visibility(obs_properties_t *props, obs_data_t *settings)
{
	float speed = (float)obs_data_get_double(settings, "motion_speed");
	bool has_motion = speed > 0.001f || speed < -0.001f;
	bool sd = obs_data_get_bool(settings, "speed_drift");
	bool ld = obs_data_get_bool(settings, "location_drift");
	obs_property_t *pp;
	pp = obs_properties_get(props, "alternating_lines");
	if (pp)
		obs_property_set_visible(pp, has_motion);
	pp = obs_properties_get(props, "speed_drift");
	if (pp)
		obs_property_set_visible(pp, has_motion);
	pp = obs_properties_get(props, "speed_drift_amount");
	if (pp)
		obs_property_set_visible(pp, has_motion && sd);
	pp = obs_properties_get(props, "location_drift_amount");
	if (pp)
		obs_property_set_visible(pp, ld);
}

static void apply_vignette_visibility(obs_properties_t *props, obs_data_t *settings)
{
	bool en = obs_data_get_bool(settings, "vignette_enabled");
	int shape = (int)obs_data_get_int(settings, "vignette_shape");
	const char *names[] = {"vignette_size",         "vignette_direction", "vignette_shape",
			       "vignette_polygon_sides", "vignette_softness",  "vignette_inverted"};
	for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
		obs_property_t *pp = obs_properties_get(props, names[k]);
		if (pp)
			obs_property_set_visible(pp, en);
	}
	if (en) {
		obs_property_t *pp = obs_properties_get(props, "vignette_polygon_sides");
		if (pp)
			obs_property_set_visible(pp, shape == CVIGN_POLYGON);
		pp = obs_properties_get(props, "vignette_direction");
		if (pp)
			obs_property_set_visible(pp, shape != CVIGN_CIRCLE);
	}
}

static bool item_count_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	apply_item_count_visibility(props, settings);
	apply_density_visibility(props, settings);
	return true;
}

static bool layout_mode_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	apply_density_visibility(props, settings);
	return true;
}

static bool motion_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	apply_motion_visibility(props, settings);
	return true;
}

static bool vignette_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	apply_vignette_visibility(props, settings);
	return true;
}

static bool item_kind_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int i = item_index_from_prop(p);
	if (i < 0)
		return false;
	apply_item_kind_visibility(props, settings, i);
	return true;
}

static bool item_enabled_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int i = item_index_from_prop(p);
	if (i < 0)
		return false;
	apply_item_enabled_visibility(props, settings, i);
	apply_density_visibility(props, settings);
	return true;
}

static void add_item_group(obs_properties_t *root, struct cpat_renderer *r, int i)
{
	char key[64];
	char label[128];

	snprintf(label, sizeof(label), "%s %d", obs_module_text("Constellations.Item"), i + 1);
	obs_properties_t *grp = obs_properties_create();

	snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
	obs_property_t *en = obs_properties_add_bool(grp, key, obs_module_text("Constellations.Item.Enabled"));
	obs_property_set_modified_callback(en, item_enabled_modified);

	snprintf(key, sizeof(key), "item_%d_kind", i + 1);
	obs_property_t *kind = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.Kind"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(kind, obs_module_text("Constellations.Item.Kind.Shape"), CITEM_SHAPE);
	obs_property_list_add_int(kind, obs_module_text("Constellations.Item.Kind.Image"), CITEM_IMAGE);
	obs_property_list_add_int(kind, obs_module_text("Constellations.Item.Kind.Source"), CITEM_SOURCE);
	obs_property_set_modified_callback(kind, item_kind_modified);

	snprintf(key, sizeof(key), "item_%d_shape", i + 1);
	obs_property_t *sh = obs_properties_add_list(grp, key, obs_module_text("Constellations.Shape"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Circle"), CSHAPE_CIRCLE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Cross"), CSHAPE_CROSS);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Square"), CSHAPE_SQUARE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Polygon"), CSHAPE_POLYGON);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Squircle"), CSHAPE_SQUIRCLE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.HLine"), CSHAPE_HLINE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.VLine"), CSHAPE_VLINE);
	obs_property_set_modified_callback(sh, item_kind_modified);

	snprintf(key, sizeof(key), "item_%d_outline_only", i + 1);
	obs_property_t *ol = obs_properties_add_bool(grp, key, obs_module_text("Constellations.Item.OutlineOnly"));
	obs_property_set_modified_callback(ol, item_kind_modified);

	snprintf(key, sizeof(key), "item_%d_polygon_sides", i + 1);
	obs_properties_add_int_slider(grp, key, obs_module_text("Constellations.Shape.PolySides"), 3, 24, 1);
	snprintf(key, sizeof(key), "item_%d_stroke_thickness", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Shape.StrokeThickness"), 0.02, 1.0,
					0.01);
	snprintf(key, sizeof(key), "item_%d_color", i + 1);
	obs_properties_add_color_alpha(grp, key, obs_module_text("Constellations.Shape.Color"));

	snprintf(key, sizeof(key), "item_%d_image_path", i + 1);
	obs_properties_add_path(grp, key, obs_module_text("Constellations.Item.ImagePath"), OBS_PATH_FILE,
				"Images (*.png *.jpg *.jpeg *.bmp *.tga *.gif);;All Files (*.*)", NULL);

	snprintf(key, sizeof(key), "item_%d_source_name", i + 1);
	obs_property_t *src = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.Source"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	fill_source_dropdown(src, r ? r->owner : NULL);

	snprintf(key, sizeof(key), "item_%d_size", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.Size"), 1.0, 1024.0, 1.0);
	snprintf(key, sizeof(key), "item_%d_rotation", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.Rotation"), 0.0, 360.0, 1.0);
	snprintf(key, sizeof(key), "item_%d_density", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.Density"), 0.1, 20.0, 0.1);

	char gid[64];
	snprintf(gid, sizeof(gid), "item_%d_group", i + 1);
	obs_properties_add_group(root, gid, label, OBS_GROUP_NORMAL, grp);
}

void cpat_renderer_get_properties(struct cpat_renderer *r, obs_properties_t *props, bool include_canvas)
{
	if (include_canvas) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_color_alpha(canvas, "background_color",
					       obs_module_text("Constellations.Canvas.BackgroundColor"));
		obs_properties_add_float_slider(canvas, "anchor_x_pct", obs_module_text("Constellations.Anchor.X"),
						0.0, 100.0, 0.5);
		obs_properties_add_float_slider(canvas, "anchor_y_pct", obs_module_text("Constellations.Anchor.Y"),
						0.0, 100.0, 0.5);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	} else {
		obs_properties_add_float_slider(props, "anchor_x_pct", obs_module_text("Constellations.Anchor.X"),
						0.0, 100.0, 0.5);
		obs_properties_add_float_slider(props, "anchor_y_pct", obs_module_text("Constellations.Anchor.Y"),
						0.0, 100.0, 0.5);
	}

	obs_property_t *ic = obs_properties_add_int_slider(props, "item_count",
							   obs_module_text("Constellations.ItemCount"), 1,
							   CONSTELLATIONS_MAX_ITEMS, 1);
	obs_property_set_modified_callback(ic, item_count_modified);

	obs_property_t *lm = obs_properties_add_list(props, "layout_mode",
						     obs_module_text("Constellations.Layout"), OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(lm, obs_module_text("Constellations.Layout.Layered"), CLAYOUT_LAYERED);
	obs_property_list_add_int(lm, obs_module_text("Constellations.Layout.StepRepeat"), CLAYOUT_STEP_REPEAT);
	obs_property_set_modified_callback(lm, layout_mode_modified);

	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i)
		add_item_group(props, r, i);

	obs_properties_t *motion = obs_properties_create();
	obs_properties_add_float_slider(motion, "motion_angle", obs_module_text("Constellations.Motion.Angle"), 0.0,
					360.0, 1.0);
	obs_property_t *ms = obs_properties_add_float_slider(
		motion, "motion_speed", obs_module_text("Constellations.Motion.Speed"), -500.0, 500.0, 1.0);
	obs_property_set_modified_callback(ms, motion_modified);
	obs_properties_add_bool(motion, "alternating_lines", obs_module_text("Constellations.Motion.Alternating"));
	obs_property_t *sd = obs_properties_add_bool(motion, "speed_drift",
						     obs_module_text("Constellations.Motion.SpeedDrift"));
	obs_property_set_modified_callback(sd, motion_modified);
	obs_properties_add_float_slider(motion, "speed_drift_amount",
					obs_module_text("Constellations.Motion.SpeedDriftAmount"), 0.0, 1.0, 0.01);
	obs_property_t *ld = obs_properties_add_bool(motion, "location_drift",
						     obs_module_text("Constellations.Motion.LocationDrift"));
	obs_property_set_modified_callback(ld, motion_modified);
	obs_properties_add_float_slider(motion, "location_drift_amount",
					obs_module_text("Constellations.Motion.LocationDriftAmount"), 0.0, 1.0, 0.01);
	obs_properties_add_group(props, "motion_group", obs_module_text("Constellations.Group.Motion"),
				 OBS_GROUP_NORMAL, motion);

	obs_properties_t *vg = obs_properties_create();
	obs_property_t *ve = obs_properties_add_bool(vg, "vignette_enabled",
						     obs_module_text("Constellations.Vignette.Enabled"));
	obs_property_set_modified_callback(ve, vignette_modified);
	obs_properties_add_float_slider(vg, "vignette_size", obs_module_text("Constellations.Vignette.Size"), 0.01,
					2.0, 0.01);
	obs_properties_add_float_slider(vg, "vignette_direction", obs_module_text("Constellations.Vignette.Direction"),
					0.0, 360.0, 1.0);
	obs_property_t *vs = obs_properties_add_list(vg, "vignette_shape",
						     obs_module_text("Constellations.Vignette.Shape"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(vs, obs_module_text("Constellations.Vignette.Shape.Circle"), CVIGN_CIRCLE);
	obs_property_list_add_int(vs, obs_module_text("Constellations.Vignette.Shape.Linear"), CVIGN_LINEAR);
	obs_property_list_add_int(vs, obs_module_text("Constellations.Vignette.Shape.Square"), CVIGN_SQUARE);
	obs_property_list_add_int(vs, obs_module_text("Constellations.Vignette.Shape.Polygon"), CVIGN_POLYGON);
	obs_property_set_modified_callback(vs, vignette_modified);
	obs_properties_add_int_slider(vg, "vignette_polygon_sides",
				      obs_module_text("Constellations.Vignette.PolygonSides"), 3, 24, 1);
	obs_properties_add_float_slider(vg, "vignette_softness", obs_module_text("Constellations.Vignette.Falloff"),
					0.0, 4.0, 0.01);
	obs_properties_add_bool(vg, "vignette_inverted", obs_module_text("Constellations.Vignette.Inverted"));
	obs_properties_add_group(props, "vignette_group", obs_module_text("Constellations.Group.Vignette"),
				 OBS_GROUP_NORMAL, vg);
}

static void cpat_item_update(struct cpat_item *it, obs_data_t *settings, int index, obs_source_t *exclude)
{
	char key[64];
#define IK(name) (snprintf(key, sizeof(key), "item_%d_%s", index + 1, name), key)
	it->enabled = obs_data_get_bool(settings, IK("enabled"));
	it->kind = (int)obs_data_get_int(settings, IK("kind"));
	it->shape = (int)obs_data_get_int(settings, IK("shape"));
	it->outline_only = obs_data_get_bool(settings, IK("outline_only"));
	it->polygon_sides = (int)obs_data_get_int(settings, IK("polygon_sides"));
	it->stroke_thickness = (float)obs_data_get_double(settings, IK("stroke_thickness"));
	uint32_t c = (uint32_t)obs_data_get_int(settings, IK("color"));
	vec4_from_rgba(&it->color, c);

	const char *ip = obs_data_get_string(settings, IK("image_path"));
	bool image_changed = !it->image_path || strcmp(ip, it->image_path) != 0;
	if (image_changed) {
		bfree(it->image_path);
		it->image_path = bstrdup(ip);
		if (it->kind == CITEM_IMAGE)
			cpat_item_acquire_image(it);
	} else if (it->kind == CITEM_IMAGE && !it->image_loaded) {
		cpat_item_acquire_image(it);
	} else if (it->kind != CITEM_IMAGE && it->image_loaded) {
		cpat_item_release_image(it);
	}

	const char *sn = obs_data_get_string(settings, IK("source_name"));
	bool source_changed = !it->source_name || strcmp(sn, it->source_name) != 0;
	if (source_changed) {
		bfree(it->source_name);
		it->source_name = bstrdup(sn);
		if (it->kind == CITEM_SOURCE)
			cpat_item_acquire_source(it, exclude);
		else
			cpat_item_release_source(it);
	} else if (it->kind == CITEM_SOURCE && !it->source_ref) {
		cpat_item_acquire_source(it, exclude);
	} else if (it->kind != CITEM_SOURCE && it->source_ref) {
		cpat_item_release_source(it);
	}

	it->size = (float)obs_data_get_double(settings, IK("size"));
	if (it->size < 1.0f)
		it->size = 1.0f;
	it->rotation_deg = (float)obs_data_get_double(settings, IK("rotation"));
	it->density = (float)obs_data_get_double(settings, IK("density"));
	if (it->density <= 0.0f)
		it->density = 0.1f;

	const float golden_x = 0.6180339887f;
	const float golden_y = 0.3819660113f;
	float fi = (float)index;
	it->sub_offset_x = fmodf(fi * golden_x, 1.0f);
	it->sub_offset_y = fmodf(fi * golden_y, 1.0f);
#undef IK
}

void cpat_renderer_update(struct cpat_renderer *r, obs_data_t *settings, bool include_canvas)
{
	if (include_canvas) {
		r->width = (uint32_t)obs_data_get_int(settings, "width");
		r->height = (uint32_t)obs_data_get_int(settings, "height");
		if (r->width == 0)
			r->width = 1920;
		if (r->height == 0)
			r->height = 1080;
		uint32_t bg = (uint32_t)obs_data_get_int(settings, "background_color");
		vec4_from_rgba(&r->background_color, bg);
		r->has_background = (r->background_color.w > 0.0001f);
	} else {
		r->has_background = false;
	}
	r->anchor_x_pct = (float)obs_data_get_double(settings, "anchor_x_pct");
	r->anchor_y_pct = (float)obs_data_get_double(settings, "anchor_y_pct");

	int ic = (int)obs_data_get_int(settings, "item_count");
	if (ic < 1)
		ic = 1;
	if (ic > CONSTELLATIONS_MAX_ITEMS)
		ic = CONSTELLATIONS_MAX_ITEMS;
	r->item_count = (uint32_t)ic;

	r->layout_mode = (int)obs_data_get_int(settings, "layout_mode");

	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i)
		cpat_item_update(&r->items[i], settings, i, r->owner);

	r->motion_angle_deg = (float)obs_data_get_double(settings, "motion_angle");
	r->motion_speed = (float)obs_data_get_double(settings, "motion_speed");
	r->alternating_lines = obs_data_get_bool(settings, "alternating_lines");
	r->speed_drift = obs_data_get_bool(settings, "speed_drift");
	r->speed_drift_amount = (float)obs_data_get_double(settings, "speed_drift_amount");
	r->location_drift = obs_data_get_bool(settings, "location_drift");
	r->location_drift_amount = (float)obs_data_get_double(settings, "location_drift_amount");

	r->vignette_enabled = obs_data_get_bool(settings, "vignette_enabled");
	r->vignette_size = (float)obs_data_get_double(settings, "vignette_size");
	r->vignette_direction_deg = (float)obs_data_get_double(settings, "vignette_direction");
	r->vignette_shape = (int)obs_data_get_int(settings, "vignette_shape");
	r->vignette_polygon_sides = (int)obs_data_get_int(settings, "vignette_polygon_sides");
	r->vignette_softness = (float)obs_data_get_double(settings, "vignette_softness");
	r->vignette_inverted = obs_data_get_bool(settings, "vignette_inverted");
}

void cpat_renderer_tick(struct cpat_renderer *r, float seconds)
{
	r->phase += (double)r->motion_speed * (double)seconds;

	uint32_t count = r->item_count;
	if (count > CONSTELLATIONS_MAX_ITEMS)
		count = CONSTELLATIONS_MAX_ITEMS;
	for (uint32_t i = 0; i < count; ++i) {
		struct cpat_item *it = &r->items[i];
		if (!it->enabled || it->kind != CITEM_SOURCE || !it->source_ref || !it->source_texrender)
			continue;
		obs_source_t *src = obs_weak_source_get_source(it->source_ref);
		if (!src)
			continue;
		uint32_t sw = obs_source_get_base_width(src);
		uint32_t sh = obs_source_get_base_height(src);
		if (sw && sh) {
			it->source_w = sw;
			it->source_h = sh;
		}
		obs_source_release(src);
	}
}

void cpat_renderer_render_background(struct cpat_renderer *r)
{
	if (!r->has_background || r->width == 0 || r->height == 0)
		return;
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_vec4(color, &r->background_color);

	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);
	gs_draw_sprite(NULL, 0, r->width, r->height);
	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

static void render_item_source_to_texrender(struct cpat_item *it)
{
	if (!it->source_ref || !it->source_texrender)
		return;
	obs_source_t *src = obs_weak_source_get_source(it->source_ref);
	if (!src)
		return;
	uint32_t sw = obs_source_get_base_width(src);
	uint32_t sh = obs_source_get_base_height(src);
	if (sw == 0 || sh == 0) {
		obs_source_release(src);
		return;
	}
	gs_texrender_reset(it->source_texrender);
	if (gs_texrender_begin(it->source_texrender, sw, sh)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)sw, 0.0f, (float)sh, -100.0f, 100.0f);
		obs_source_video_render(src);
		gs_texrender_end(it->source_texrender);
		it->source_w = sw;
		it->source_h = sh;
	}
	obs_source_release(src);
}

static void set_common_uniforms(struct cpat_renderer *r, struct cpat_item *it, uint32_t w, uint32_t h, int index,
				uint32_t step_index, uint32_t step_count, float shared_density)
{
	gs_eparam_t *p;
	struct vec2 canvas = {(float)w, (float)h};
	p = gs_effect_get_param_by_name(r->effect, "canvas_size");
	if (p)
		gs_effect_set_vec2(p, &canvas);

	bool step_repeat = (r->layout_mode == CLAYOUT_STEP_REPEAT);
	float density = step_repeat ? shared_density : it->density;
	if (density <= 0.0f)
		density = 0.1f;
	float cell = 100.0f / density;
	if (cell < 2.0f)
		cell = 2.0f;
	float sub_off_x = step_repeat ? 0.0f : it->sub_offset_x;
	float sub_off_y = step_repeat ? 0.0f : it->sub_offset_y;
	float anchor_x = (r->anchor_x_pct / 100.0f) * canvas.x + sub_off_x * cell;
	float anchor_y = (r->anchor_y_pct / 100.0f) * canvas.y + sub_off_y * cell;
	struct vec2 anchor = {anchor_x, anchor_y};
	p = gs_effect_get_param_by_name(r->effect, "anchor");
	if (p)
		gs_effect_set_vec2(p, &anchor);

	p = gs_effect_get_param_by_name(r->effect, "cell_size");
	if (p)
		gs_effect_set_float(p, cell);
	p = gs_effect_get_param_by_name(r->effect, "shape_size");
	if (p)
		gs_effect_set_float(p, it->size);
	float rot = it->rotation_deg * (float)(M_PI / 180.0);
	p = gs_effect_get_param_by_name(r->effect, "shape_rotation");
	if (p)
		gs_effect_set_float(p, rot);
	p = gs_effect_get_param_by_name(r->effect, "shape_color");
	if (p)
		gs_effect_set_vec4(p, &it->color);
	p = gs_effect_get_param_by_name(r->effect, "shape_kind");
	if (p)
		gs_effect_set_int(p, it->shape);
	p = gs_effect_get_param_by_name(r->effect, "polygon_sides");
	if (p)
		gs_effect_set_int(p, it->polygon_sides);
	p = gs_effect_get_param_by_name(r->effect, "stroke_thickness");
	if (p)
		gs_effect_set_float(p, it->stroke_thickness);
	p = gs_effect_get_param_by_name(r->effect, "outline_only");
	if (p)
		gs_effect_set_float(p, it->outline_only ? 1.0f : 0.0f);

	float mang = r->motion_angle_deg * (float)(M_PI / 180.0);
	struct vec2 mdir = {cosf(mang), sinf(mang)};
	p = gs_effect_get_param_by_name(r->effect, "motion_dir");
	if (p)
		gs_effect_set_vec2(p, &mdir);
	p = gs_effect_get_param_by_name(r->effect, "motion_phase");
	if (p)
		gs_effect_set_float(p, (float)r->phase);
	p = gs_effect_get_param_by_name(r->effect, "alt_lines");
	if (p)
		gs_effect_set_float(p, r->alternating_lines ? 1.0f : 0.0f);
	p = gs_effect_get_param_by_name(r->effect, "drift_speed_amt");
	if (p)
		gs_effect_set_float(p, r->speed_drift ? r->speed_drift_amount : 0.0f);
	p = gs_effect_get_param_by_name(r->effect, "drift_loc_amt");
	if (p)
		gs_effect_set_float(p, r->location_drift ? r->location_drift_amount : 0.0f);
	p = gs_effect_get_param_by_name(r->effect, "item_seed");
	if (p)
		gs_effect_set_float(p, (float)index * 17.31f);

	p = gs_effect_get_param_by_name(r->effect, "layout_mode");
	if (p)
		gs_effect_set_float(p, step_repeat ? 1.0f : 0.0f);
	p = gs_effect_get_param_by_name(r->effect, "step_index");
	if (p)
		gs_effect_set_float(p, (float)step_index);
	p = gs_effect_get_param_by_name(r->effect, "step_count");
	if (p)
		gs_effect_set_float(p, (float)(step_count > 0 ? step_count : 1));

	float min_dim = (canvas.x < canvas.y) ? canvas.x : canvas.y;
	float vsz = r->vignette_size * 0.5f * min_dim;
	struct vec2 vcenter = {anchor.x, anchor.y};
	float vang = r->vignette_direction_deg * (float)(M_PI / 180.0);
	p = gs_effect_get_param_by_name(r->effect, "vignette_enabled");
	if (p)
		gs_effect_set_float(p, r->vignette_enabled ? 1.0f : 0.0f);
	p = gs_effect_get_param_by_name(r->effect, "vignette_center");
	if (p)
		gs_effect_set_vec2(p, &vcenter);
	p = gs_effect_get_param_by_name(r->effect, "vignette_size_px");
	if (p)
		gs_effect_set_float(p, vsz);
	p = gs_effect_get_param_by_name(r->effect, "vignette_direction");
	if (p)
		gs_effect_set_float(p, vang);
	p = gs_effect_get_param_by_name(r->effect, "vignette_shape");
	if (p)
		gs_effect_set_int(p, r->vignette_shape);
	p = gs_effect_get_param_by_name(r->effect, "vignette_polygon_sides");
	if (p)
		gs_effect_set_int(p, r->vignette_polygon_sides);
	p = gs_effect_get_param_by_name(r->effect, "vignette_softness");
	if (p)
		gs_effect_set_float(p, r->vignette_softness);
	p = gs_effect_get_param_by_name(r->effect, "vignette_inverted");
	if (p)
		gs_effect_set_float(p, r->vignette_inverted ? 1.0f : 0.0f);
}

void cpat_renderer_render_items(struct cpat_renderer *r, uint32_t w, uint32_t h)
{
	if (!r->effect || w == 0 || h == 0)
		return;

	uint32_t count = r->item_count;
	if (count > CONSTELLATIONS_MAX_ITEMS)
		count = CONSTELLATIONS_MAX_ITEMS;

	uint32_t step_count = 0;
	float shared_density = 2.0f;
	if (r->layout_mode == CLAYOUT_STEP_REPEAT) {
		for (uint32_t i = 0; i < count; ++i) {
			if (r->items[i].enabled) {
				if (step_count == 0)
					shared_density = r->items[i].density;
				step_count++;
			}
		}
		if (step_count == 0)
			step_count = 1;
	}

	for (uint32_t i = 0; i < count; ++i) {
		struct cpat_item *it = &r->items[i];
		if (!it->enabled)
			continue;
		if (it->kind == CITEM_SOURCE)
			render_item_source_to_texrender(it);
	}

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	uint32_t next_step_index = 0;
	for (uint32_t i = 0; i < count; ++i) {
		struct cpat_item *it = &r->items[i];
		if (!it->enabled)
			continue;
		uint32_t this_step_index = next_step_index++;

		gs_texture_t *tex = NULL;
		const char *technique = "DrawShape";
		if (it->kind == CITEM_IMAGE) {
			if (!it->image_loaded || !it->image.texture)
				continue;
			tex = it->image.texture;
			technique = "DrawImage";
		} else if (it->kind == CITEM_SOURCE) {
			if (!it->source_texrender)
				continue;
			tex = gs_texrender_get_texture(it->source_texrender);
			if (!tex)
				continue;
			technique = "DrawImage";
		}

		set_common_uniforms(r, it, w, h, (int)i, this_step_index, step_count, shared_density);

		if (tex) {
			gs_eparam_t *tp = gs_effect_get_param_by_name(r->effect, "cell_texture");
			if (tp)
				gs_effect_set_texture(tp, tex);
		}

		while (gs_effect_loop(r->effect, technique))
			gs_draw_sprite(NULL, 0, w, h);
	}

	gs_blend_state_pop();
}
