/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_palette.c
 *  \ingroup RNA
 */

#include <stdlib.h>

#include "BLI_utildefines.h"
#include "BLI_string_utils.h"

#include "RNA_define.h"
#include "RNA_access.h"

#include "rna_internal.h"

#include "WM_types.h"
#include "ED_gpencil.h"

#include "BLT_translation.h"

#include "DNA_gpencil_types.h"
#include "DNA_brush_types.h"

#ifdef RNA_RUNTIME

#include "WM_api.h"
#include "ED_gpencil.h"

#include "BKE_animsys.h"
#include "BKE_paint.h"
#include "BKE_report.h"
#include "BKE_gpencil.h"

static void rna_GPencil_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *UNUSED(ptr))
{
	WM_main_add_notifier(NC_GPENCIL | NA_EDITED, NULL);
}

static PaletteColor *rna_Palette_color_new(Palette *palette)
{
	PaletteColor *color = BKE_palette_color_add(palette);
	return color;
}

static void rna_Palette_color_remove(Palette *palette, ReportList *reports, PointerRNA *color_ptr)
{
	PaletteColor *color = color_ptr->data;

	if (BLI_findindex(&palette->colors, color) == -1) {
		BKE_reportf(reports, RPT_ERROR, "Palette '%s' does not contain color given", palette->id.name + 2);
		return;
	}

	BKE_palette_color_remove(palette, color);

	RNA_POINTER_INVALIDATE(color_ptr);
}

static void rna_Palette_color_clear(Palette *palette)
{
	BKE_palette_clear(palette);
}

static PointerRNA rna_Palette_active_color_get(PointerRNA *ptr)
{
	Palette *palette = ptr->data;
	PaletteColor *color;

	color = BLI_findlink(&palette->colors, palette->active_color);

	if (color)
		return rna_pointer_inherit_refine(ptr, &RNA_PaletteColor, color);

	return rna_pointer_inherit_refine(ptr, NULL, NULL);
}

static void rna_Palette_active_color_set(PointerRNA *ptr, PointerRNA value)
{
	Palette *palette = ptr->data;
	PaletteColor *color = value.data;

	/* -1 is ok for an unset index */
	if (color == NULL)
		palette->active_color = -1;
	else
		palette->active_color = BLI_findindex(&palette->colors, color);
}

static char *rna_Palette_color_path(PointerRNA *ptr)
{
	PaletteColor *palcolor = (PaletteColor *)ptr->data;
	char name_esc[sizeof(palcolor->info) * 2];

	BLI_strescape(name_esc, palcolor->info, sizeof(name_esc));

	return BLI_sprintfN("colors[\"%s\"]", name_esc);

}

static void rna_PaletteColor_info_set(PointerRNA *ptr, const char *value)
{
	Palette *palette = (Palette *)ptr->id.data;
	PaletteColor *palcolor = BLI_findlink(&palette->colors, palette->active_color);
	
	char oldname[64] = "";
	BLI_strncpy(oldname, palcolor->info, sizeof(oldname));

	/* copy the new name into the name slot */
	BLI_strncpy_utf8(palcolor->info, value, sizeof(palcolor->info));
	BLI_uniquename(&palette->colors, palcolor, DATA_("Color"), '.', offsetof(PaletteColor, info),
		sizeof(palcolor->info));

	/* rename all for gp datablocks */
	BKE_gpencil_palettecolor_allnames(palcolor, palcolor->info);

	/* now fix animation paths */
	BKE_animdata_fix_paths_rename_all(&palette->id, "colors", oldname, palcolor->info);
}

static int rna_PaletteColor_is_stroke_visible_get(PointerRNA *ptr)
{
	PaletteColor *pcolor = (PaletteColor *)ptr->data;
	return (pcolor->rgb[3] > GPENCIL_ALPHA_OPACITY_THRESH);
}

static int rna_PaletteColor_is_fill_visible_get(PointerRNA *ptr)
{
	PaletteColor *pcolor = (PaletteColor *)ptr->data;
	return (pcolor->fill[3] > GPENCIL_ALPHA_OPACITY_THRESH);
}

#else

/* palette.colors */
static void rna_def_palettecolors(BlenderRNA *brna, PropertyRNA *cprop)
{
	StructRNA *srna;
	PropertyRNA *prop;

	FunctionRNA *func;
	PropertyRNA *parm;

	RNA_def_property_srna(cprop, "PaletteColors");
	srna = RNA_def_struct(brna, "PaletteColors", NULL);
	RNA_def_struct_sdna(srna, "Palette");
	RNA_def_struct_ui_text(srna, "Palette Splines", "Collection of palette colors");

	func = RNA_def_function(srna, "new", "rna_Palette_color_new");
	RNA_def_function_ui_description(func, "Add a new color to the palette");
	parm = RNA_def_pointer(func, "color", "PaletteColor", "", "The newly created color");
	RNA_def_function_return(func, parm);

	func = RNA_def_function(srna, "remove", "rna_Palette_color_remove");
	RNA_def_function_ui_description(func, "Remove a color from the palette");
	RNA_def_function_flag(func, FUNC_USE_REPORTS);
	parm = RNA_def_pointer(func, "color", "PaletteColor", "", "The color to remove");
	RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
	RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

	func = RNA_def_function(srna, "clear", "rna_Palette_color_clear");
	RNA_def_function_ui_description(func, "Remove all colors from the palette");

	prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
	RNA_def_property_struct_type(prop, "PaletteColor");
	RNA_def_property_pointer_funcs(prop, "rna_Palette_active_color_get", "rna_Palette_active_color_set", NULL, NULL);
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Active Palette Color", "");
}

static void rna_def_palettecolor(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	/* stroke styles */
	static EnumPropertyItem stroke_style_items[] = {
		{ STROKE_STYLE_SOLID, "SOLID", 0, "Solid", "Draw strokes with solid color" },
		{ STROKE_STYLE_VOLUMETRIC, "VOLUMETRIC", 0, "Volumetric", "Draw strokes with dots" },
		{ 0, NULL, 0, NULL, NULL }
	};

	/* fill styles */
	static EnumPropertyItem fill_style_items[] = {
		{ FILL_STYLE_SOLID, "SOLID", 0, "Solid", "Fill area with solid color" },
		{ 0, NULL, 0, NULL, NULL }
	};

	srna = RNA_def_struct(brna, "PaletteColor", NULL);
	RNA_def_struct_ui_text(srna, "Palette Color", "");
	RNA_def_struct_path_func(srna, "rna_Palette_color_path");

	prop = RNA_def_property(srna, "color", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_float_sdna(prop, NULL, "rgb");
	RNA_def_property_array(prop, 3);
	RNA_def_property_ui_text(prop, "Color", "");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	prop = RNA_def_property(srna, "strength", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_float_sdna(prop, NULL, "value");
	RNA_def_property_ui_text(prop, "Value", "");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

	prop = RNA_def_property(srna, "weight", PROP_FLOAT, PROP_NONE);
	RNA_def_property_range(prop, 0.0, 1.0);
	RNA_def_property_float_sdna(prop, NULL, "value");
	RNA_def_property_ui_text(prop, "Weight", "");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

	prop = RNA_def_property(srna, "alpha", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "rgb[3]");
	RNA_def_property_range(prop, 0.0, 1.0f);
	RNA_def_property_ui_text(prop, "Opacity", "Color Opacity");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	/* Name */
	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_property_string_sdna(prop, NULL, "info");
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_string_funcs(prop, NULL, NULL, "rna_PaletteColor_info_set");
	RNA_def_property_ui_text(prop, "Name", "Color name");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	/* Fill Drawing Color */
	prop = RNA_def_property(srna, "fill_color", PROP_FLOAT, PROP_COLOR_GAMMA);
	RNA_def_property_float_sdna(prop, NULL, "fill");
	RNA_def_property_array(prop, 3);
	RNA_def_property_range(prop, 0.0f, 1.0f);
	RNA_def_property_ui_text(prop, "Fill Color", "Color for filling region bounded by each stroke");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	/* Fill alpha */
	prop = RNA_def_property(srna, "fill_alpha", PROP_FLOAT, PROP_NONE);
	RNA_def_property_float_sdna(prop, NULL, "fill[3]");
	RNA_def_property_range(prop, 0.0, 1.0f);
	RNA_def_property_ui_text(prop, "Fill Opacity", "Opacity for filling region bounded by each stroke");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS | ND_DATA | NC_GPENCIL, "rna_GPencil_update");

	/* Flags */
	prop = RNA_def_property(srna, "hide", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", PAC_COLOR_HIDE);
	RNA_def_property_ui_icon(prop, ICON_RESTRICT_VIEW_OFF, 1);
	RNA_def_property_ui_text(prop, "Hide", "Set color Visibility");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	prop = RNA_def_property(srna, "lock", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", PAC_COLOR_LOCKED);
	RNA_def_property_ui_icon(prop, ICON_UNLOCKED, 1);
	RNA_def_property_ui_text(prop, "Locked", "Protect color from further editing and/or frame changes");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	prop = RNA_def_property(srna, "ghost", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", PAC_COLOR_ONIONSKIN);
	RNA_def_property_ui_icon(prop, ICON_GHOST_ENABLED, 0);
	RNA_def_property_ui_text(prop, "Show in Ghosts", "Display strokes using this color when showing onion skins");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	/* pass index for future compositing and editing tools */
	prop = RNA_def_property(srna, "pass_index", PROP_INT, PROP_UNSIGNED);
	RNA_def_property_int_sdna(prop, NULL, "index");
	RNA_def_property_ui_text(prop, "Pass Index", "Index number for the \"Color Index\" pass");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

#if 0   /* integrated as stroke style */
	/* Draw Style */
	prop = RNA_def_property(srna, "use_volumetric_strokes", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_sdna(prop, NULL, "flag", PAC_COLOR_VOLUMETRIC);
	RNA_def_property_ui_text(prop, "Volumetric Strokes", "Draw strokes as a series of circular blobs, resulting in "
		"a volumetric effect");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");
#endif

	/* stroke style */
	prop = RNA_def_property(srna, "stroke_style", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "stroke_style");
	RNA_def_property_enum_items(prop, stroke_style_items);
	RNA_def_property_ui_text(prop, "Style", "Select style used to draw strokes");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	/* fill style */
	prop = RNA_def_property(srna, "fill_style", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_bitflag_sdna(prop, NULL, "fill_style");
	RNA_def_property_enum_items(prop, fill_style_items);
	RNA_def_property_ui_text(prop, "Style", "Select style used to fill strokes");
	RNA_def_property_update(prop, NC_GPENCIL | ND_DATA, "rna_GPencil_update");

	/* Read-only state props (for simpler UI code) */
	prop = RNA_def_property(srna, "is_stroke_visible", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_funcs(prop, "rna_PaletteColor_is_stroke_visible_get", NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Is Stroke Visible", "True when opacity of stroke is set high enough to be visible");

	prop = RNA_def_property(srna, "is_fill_visible", PROP_BOOLEAN, PROP_NONE);
	RNA_def_property_boolean_funcs(prop, "rna_PaletteColor_is_fill_visible_get", NULL);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Is Fill Visible", "True when opacity of fill is set high enough to be visible");

}

static void rna_def_palette(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "Palette", "ID");
	RNA_def_struct_ui_text(srna, "Palette", "");
	RNA_def_struct_ui_icon(srna, ICON_COLOR);

	prop = RNA_def_property(srna, "colors", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_struct_type(prop, "PaletteColor");
	rna_def_palettecolors(brna, prop);

	/* Animation Data */
	rna_def_animdata_common(srna);

	prop = RNA_def_property(srna, "active_index", PROP_INT, PROP_NONE);
	RNA_def_property_int_sdna(prop, NULL, "active_color");
	RNA_def_property_ui_text(prop, "Active Index", "");
	RNA_def_property_update(prop, NC_SCENE | ND_TOOLSETTINGS, NULL);

}

void RNA_def_palette(BlenderRNA *brna)
{
	rna_def_palettecolor(brna);

	/* *** Non-Animated *** */
	RNA_define_animate_sdna(false);
	rna_def_palette(brna);
	RNA_define_animate_sdna(true);
}

#endif
