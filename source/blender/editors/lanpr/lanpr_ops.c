/*
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
 */

/** \file
 * \ingroup editor/lanpr
 */

#include "ED_lanpr.h"

#include "BLI_listbase.h"
#include "BLI_linklist.h"
#include "BLI_math_matrix.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"
#include "BLI_alloca.h"

#include "ED_lanpr.h"
#include "BKE_object.h"
#include "DNA_mesh_types.h"
#include "DNA_camera_types.h"
#include "DNA_modifier_types.h"
#include "DNA_text_types.h"
#include "DNA_lanpr_types.h"
#include "DNA_scene_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_meshdata_types.h"
#include "BKE_customdata.h"
#include "DEG_depsgraph_query.h"
#include "BKE_camera.h"
#include "BKE_gpencil.h"
#include "BKE_collection.h"
#include "BKE_report.h"
#include "BKE_screen.h"
#include "BKE_text.h"
#include "BKE_context.h"
#include "MEM_guardedalloc.h"

#include "bmesh.h"
#include "bmesh_class.h"
#include "bmesh_tools.h"

#include "WM_types.h"
#include "WM_api.h"

#include "ED_svg.h"
#include "BKE_text.h"



extern LANPR_SharedResource lanpr_share;
extern const char *RE_engine_id_BLENDER_LANPR;
struct Object;

void ED_lanpr_update_data_for_external(struct Depsgraph *depsgraph);

int lanpr_count_chain(LANPR_RenderLineChain *rlc);

void lanpr_chain_clear_picked_flag(struct LANPR_RenderBuffer *rb);

int lanpr_compute_feature_lines_internal(struct Depsgraph *depsgraph, int instersections_only);

void lanpr_destroy_render_data(struct LANPR_RenderBuffer *rb);

bool ED_lanpr_dpix_shader_error();

bool ED_lanpr_disable_edge_splits(struct Scene *s);

void ED_lanpr_copy_data(struct Scene *from, struct Scene *to);

void ED_lanpr_free_everything(struct Scene *s);


/* Calculations */





/* SVG bindings */

static int lanpr_export_svg_exec(struct bContext *C, wmOperator *op)
{
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;
  SceneLANPR *lanpr =
      &rb->scene->lanpr; /* XXX: This is not evaluated for copy_on_write stuff... */
  LANPR_LineLayer *ll;

  for (ll = lanpr->line_layers.first; ll; ll = ll->next) {
    Text *ta = BKE_text_add(CTX_data_main(C), "exported_svg");
    ED_svg_data_from_lanpr_chain(ta, rb, ll);
  }

  return OPERATOR_FINISHED;
}

static bool lanpr_render_buffer_found(struct bContext *C)
{
  if (lanpr_share.render_buffer_shared) {
    return true;
  }
  return false;
}

void SCENE_OT_lanpr_export_svg(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Export LANPR to SVG";
  ot->description = "Export LANPR render result into a SVG file";
  ot->idname = "SCENE_OT_lanpr_export_svg";

  /* callbacks */
  ot->exec = lanpr_export_svg_exec;
  ot->poll = lanpr_render_buffer_found;

  /* flag */
  ot->flag = OPTYPE_USE_EVAL_DATA;

  /* properties */
  /* Should have: facing, layer, visibility, file split... */
}


/* Access */

/* Probably remove this in the future. */
void ED_lanpr_update_data_for_external(Depsgraph *depsgraph)
{
  Scene *scene = DEG_get_evaluated_scene(depsgraph);
  SceneLANPR *lanpr = &scene->lanpr;
  if (lanpr->master_mode != LANPR_MASTER_MODE_SOFTWARE) {
    return;
  }
  if (!lanpr_share.render_buffer_shared ||
      lanpr_share.render_buffer_shared->cached_for_frame != scene->r.cfra) {
    lanpr_compute_feature_lines_internal(depsgraph, 0);
  }
}

bool ED_lanpr_dpix_shader_error()
{
  return lanpr_share.dpix_shader_error;
}
bool ED_lanpr_disable_edge_splits(Scene *s)
{
  return (s->lanpr.enabled && s->lanpr.disable_edge_splits);
}

void ED_lanpr_copy_data(Scene *from, Scene *to)
{
  SceneLANPR *lanpr = &from->lanpr;
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared, *new_rb;
  LANPR_LineLayer *ll, *new_ll;
  LANPR_LineLayerComponent *llc, *new_llc;

  list_handle_empty(&to->lanpr.line_layers);

  for (ll = lanpr->line_layers.first; ll; ll = ll->next) {
    new_ll = MEM_callocN(sizeof(LANPR_LineLayer), "Copied Line Layer");
    memcpy(new_ll, ll, sizeof(LANPR_LineLayer));
    list_handle_empty(&new_ll->components);
    new_ll->next = new_ll->prev = NULL;
    BLI_addtail(&to->lanpr.line_layers, new_ll);
    for (llc = ll->components.first; llc; llc = llc->next) {
      new_llc = MEM_callocN(sizeof(LANPR_LineLayerComponent), "Copied Line Layer Component");
      memcpy(new_llc, llc, sizeof(LANPR_LineLayerComponent));
      new_llc->next = new_llc->prev = NULL;
      BLI_addtail(&new_ll->components, new_llc);
    }
  }

  /*  render_buffer now only accessible from lanpr_share */
}

void ED_lanpr_free_everything(Scene *s)
{
  SceneLANPR *lanpr = &s->lanpr;
  LANPR_LineLayer *ll;
  LANPR_LineLayerComponent *llc;

  while (ll = BLI_pophead(&lanpr->line_layers)) {
    while (llc = BLI_pophead(&ll->components))
      MEM_freeN(llc);
    MEM_freeN(ll);
  }
}



/* GPencil bindings */

static void lanpr_generate_gpencil_from_chain(Depsgraph *depsgraph,
                                       Object *ob,
                                       bGPDlayer *gpl,
                                       bGPDframe *gpf,
                                       int qi_begin,
                                       int qi_end,
                                       int material_nr,
                                       Collection *col,
                                       int types)
{
  Scene *scene = DEG_get_evaluated_scene(depsgraph);
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;

  if (rb == NULL) {
    printf("NULL LANPR rb!\n");
    return;
  }
  if (scene->lanpr.master_mode != LANPR_MASTER_MODE_SOFTWARE) {
    return;
  }

  int color_idx = 0;
  int tot_points = 0;
  short thickness = 1;

  float mat[4][4];

  unit_m4(mat);

  /*  Split countour lines at occlution points and deselect occluded segment */
  LANPR_RenderLine *rl;
  LANPR_RenderLineSegment *rls, *irls;

  LANPR_RenderLineChain *rlc;
  LANPR_RenderLineChainItem *rlci;
  for (rlc = rb->chains.first; rlc; rlc = rlc->next) {

    if (rlc->picked) {
      continue;
    }
    if (ob && !rlc->object_ref) {
      continue; /* intersection lines are all in the first collection running into here */
    }
    if (!(rlc->type & types)) {
      continue;
    }
    if (rlc->level > qi_end || rlc->level < qi_begin) {
      continue;
    }
    if (ob && &ob->id != rlc->object_ref->id.orig_id) {
      continue;
    }
    if (col && rlc->object_ref) {
      if (!BKE_collection_has_object_recursive(col, (Object *)rlc->object_ref->id.orig_id)) {
        continue;
      }
    }

    rlc->picked = 1;

    int array_idx = 0;
    int count = lanpr_count_chain(rlc);
    bGPDstroke *gps = BKE_gpencil_add_stroke(gpf, color_idx, count, thickness);

    float *stroke_data = BLI_array_alloca(stroke_data, count * GP_PRIM_DATABUF_SIZE);

    for (rlci = rlc->chain.first; rlci; rlci = rlci->next) {
      float opatity = 1.0f; /* rlci->occlusion ? 0.0f : 1.0f; */
      stroke_data[array_idx] = rlci->gpos[0];
      stroke_data[array_idx + 1] = rlci->gpos[1];
      stroke_data[array_idx + 2] = rlci->gpos[2];
      stroke_data[array_idx + 3] = 1;       /*  thickness */
      stroke_data[array_idx + 4] = opatity; /*  hardness? */
      array_idx += 5;
    }

    BKE_gpencil_stroke_add_points(gps, stroke_data, count, mat);
    gps->mat_nr = material_nr;
  }
}
static void lanpr_clear_gp_lanpr_flags(Depsgraph *dg, int frame)
{
  DEG_OBJECT_ITER_BEGIN (dg,
                         o,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY | DEG_ITER_OBJECT_FLAG_VISIBLE |
                             DEG_ITER_OBJECT_FLAG_DUPLI | DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    if (o->type == OB_GPENCIL) {
      bGPdata *gpd = ((Object *)o->id.orig_id)->data;
      bGPDlayer *gpl;
      for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
        bGPDframe *gpf = BKE_gpencil_layer_find_frame(gpl, frame);
        if (!gpf) {
          continue;
        }
        gpf->flag &= ~GP_FRAME_LANPR_CLEARED;
      }
    }
  }
  DEG_OBJECT_ITER_END;
}
static void lanpr_update_gp_strokes_recursive(
    Depsgraph *dg, struct Collection *col, int frame, Object *source_only, Object *target_only)
{
  Object *ob;
  Object *gpobj;
  ModifierData *md;
  bGPdata *gpd;
  bGPDlayer *gpl;
  bGPDframe *gpf;
  CollectionObject *co;
  CollectionChild *cc;

  for (co = col->gobject.first; co || source_only; co = co->next) {
    ob = source_only ? source_only : co->ob;
    for (md = ob->modifiers.first; md; md = md->next) {
      if (md->type == eModifierType_FeatureLine) {
        FeatureLineModifierData *flmd = (FeatureLineModifierData *)md;
        if (flmd->target && flmd->target->type == OB_GPENCIL) {
          gpobj = flmd->target;

          if (target_only && target_only != gpobj) {
            continue;
          }

          gpd = gpobj->data;
          gpl = BKE_gpencil_layer_get_index(gpd, flmd->layer, 1);
          if (!gpl) {
            gpl = BKE_gpencil_layer_addnew(gpd, "lanpr_layer", true);
          }
          gpf = BKE_gpencil_layer_getframe(gpl, frame, GP_GETFRAME_ADD_NEW);

          if (gpf->strokes.first &&
              !lanpr_share.render_buffer_shared->scene->lanpr.gpencil_overwrite) {
            continue;
          }

          if (!(gpf->flag & GP_FRAME_LANPR_CLEARED)) {
            BKE_gpencil_free_strokes(gpf);
            gpf->flag |= GP_FRAME_LANPR_CLEARED;
          }

          lanpr_generate_gpencil_from_chain(dg,
                                            ob,
                                            gpl,
                                            gpf,
                                            flmd->level_begin,
                                            flmd->use_multiple_levels ? flmd->level_end :
                                                                        flmd->level_begin,
                                            flmd->material,
                                            NULL,
                                            flmd->types);
          DEG_id_tag_update(&gpd->id,
                            ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
        }
      }
    }
    if (source_only) {
      return;
    }
  }
  for (cc = col->children.first; cc; cc = cc->next) {
    lanpr_update_gp_strokes_recursive(dg, cc->collection, frame, source_only, target_only);
  }
}
static void lanpr_update_gp_strokes_collection(
    Depsgraph *dg, struct Collection *col, int frame, int this_only, Object *target_only)
{
  Object *ob;
  Object *gpobj;
  ModifierData *md;
  bGPdata *gpd;
  bGPDlayer *gpl;
  bGPDframe *gpf;
  CollectionObject *co;
  CollectionChild *cc;

  /* depth first */
  if (!this_only) {
    for (cc = col->children.first; cc; cc = cc->next) {
      lanpr_update_gp_strokes_collection(dg, cc->collection, frame, this_only, target_only);
    }
  }

  if (col->lanpr.usage != COLLECTION_LANPR_INCLUDE || !col->lanpr.target) {
    return;
  }

  gpobj = col->lanpr.target;

  if (target_only && target_only != gpobj) {
    return;
  }

  gpd = gpobj->data;
  gpl = BKE_gpencil_layer_get_index(gpd, col->lanpr.layer, 1);
  if (!gpl) {
    gpl = BKE_gpencil_layer_addnew(gpd, "lanpr_layer", true);
  }
  gpf = BKE_gpencil_layer_getframe(gpl, frame, GP_GETFRAME_ADD_NEW);

  if (gpf->strokes.first && !lanpr_share.render_buffer_shared->scene->lanpr.gpencil_overwrite) {
    return;
  }

  if (!(gpf->flag & GP_FRAME_LANPR_CLEARED)) {
    BKE_gpencil_free_strokes(gpf);
    gpf->flag |= GP_FRAME_LANPR_CLEARED;
  }

  lanpr_generate_gpencil_from_chain(dg,
                                    NULL,
                                    gpl,
                                    gpf,
                                    col->lanpr.level_begin,
                                    col->lanpr.use_multiple_levels ? col->lanpr.level_end :
                                                                     col->lanpr.level_begin,
                                    col->lanpr.material,
                                    col,
                                    col->lanpr.types);
  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
}
static int lanpr_update_gp_strokes_exec(struct bContext *C, struct wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph(C);
  SceneLANPR *lanpr = &scene->lanpr;
  int frame = scene->r.cfra;

  if (!lanpr_share.render_buffer_shared ||
      lanpr_share.render_buffer_shared->cached_for_frame != frame) {
    lanpr_compute_feature_lines_internal(dg, 0);
  }

  lanpr_chain_clear_picked_flag(lanpr_share.render_buffer_shared);

  lanpr_update_gp_strokes_recursive(dg, scene->master_collection, frame, NULL, NULL);

  lanpr_update_gp_strokes_collection(dg, scene->master_collection, frame, 0, NULL);

  lanpr_clear_gp_lanpr_flags(dg, frame);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_bake_gp_strokes_exec(struct bContext *C, struct wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph(C);
  SceneLANPR *lanpr = &scene->lanpr;
  int frame, current_frame = scene->r.cfra;
  int frame_begin = scene->r.sfra;
  int frame_end = scene->r.efra;

  for (frame = frame_begin; frame <= frame_end; frame++) {
    // BKE_scene_frame_set(scene,frame);
    DEG_evaluate_on_framechange(CTX_data_main(C), dg, frame);

    lanpr_compute_feature_lines_internal(dg, 0);

    lanpr_chain_clear_picked_flag(lanpr_share.render_buffer_shared);

    lanpr_update_gp_strokes_recursive(dg, scene->master_collection, frame, NULL, NULL);

    lanpr_update_gp_strokes_collection(dg, scene->master_collection, frame, 0, NULL);
  }

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_update_gp_target_exec(struct bContext *C, struct wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph(C);
  SceneLANPR *lanpr = &scene->lanpr;
  Object *gpo = CTX_data_active_object(C);

  int frame = scene->r.cfra;

  if (!lanpr_share.render_buffer_shared ||
      lanpr_share.render_buffer_shared->cached_for_frame != frame) {
    lanpr_compute_feature_lines_internal(dg, 0);
  }

  lanpr_chain_clear_picked_flag(lanpr_share.render_buffer_shared);

  lanpr_update_gp_strokes_recursive(dg, scene->master_collection, frame, NULL, gpo);

  lanpr_update_gp_strokes_collection(dg, scene->master_collection, frame, 0, gpo);

  lanpr_clear_gp_lanpr_flags(dg, frame);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_update_gp_source_exec(struct bContext *C, struct wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph(C);
  SceneLANPR *lanpr = &scene->lanpr;
  Object *source_obj = CTX_data_active_object(C);

  int frame = scene->r.cfra;

  if (!lanpr_share.render_buffer_shared ||
      lanpr_share.render_buffer_shared->cached_for_frame != frame) {
    lanpr_compute_feature_lines_internal(dg, 0);
  }

  lanpr_chain_clear_picked_flag(lanpr_share.render_buffer_shared);

  lanpr_update_gp_strokes_recursive(dg, scene->master_collection, frame, source_obj, NULL);

  lanpr_update_gp_strokes_collection(dg, scene->master_collection, frame, 0, NULL);

  lanpr_clear_gp_lanpr_flags(dg, frame);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}

static bool lanpr_active_is_gpencil_object(bContext *C)
{
  Object *o = CTX_data_active_object(C);
  return o->type == OB_GPENCIL;
}
static bool lanpr_active_is_source_object(bContext *C)
{
  Object *o = CTX_data_active_object(C);
  if (o->type != OB_MESH) {
    return false;
  }
  else {
    ModifierData *md;
    for (md = o->modifiers.first; md; md = md->next) {
      if (md->type == eModifierType_FeatureLine) {
        return true;
      }
    }
  }
  return false;
}

void SCENE_OT_lanpr_update_gp_strokes(struct wmOperatorType *ot)
{
  ot->name = "Update LANPR Strokes";
  ot->description = "Update strokes for LANPR grease pencil targets";
  ot->idname = "SCENE_OT_lanpr_update_gp_strokes";

  ot->exec = lanpr_update_gp_strokes_exec;
}
void SCENE_OT_lanpr_bake_gp_strokes(struct wmOperatorType *ot)
{
  ot->name = "Bake LANPR Strokes";
  ot->description = "Bake strokes for LANPR grease pencil targets in all frames";
  ot->idname = "SCENE_OT_lanpr_bake_gp_strokes";

  ot->exec = lanpr_bake_gp_strokes_exec;
}
void OBJECT_OT_lanpr_update_gp_target(struct wmOperatorType *ot)
{
  ot->name = "Update Strokes";
  ot->description = "Update LANPR strokes for selected GPencil object.";
  ot->idname = "OBJECT_OT_lanpr_update_gp_target";

  ot->poll = lanpr_active_is_gpencil_object;
  ot->exec = lanpr_update_gp_target_exec;
}
/* Not working due to lack of GP flags for the object */
void OBJECT_OT_lanpr_update_gp_source(struct wmOperatorType *ot)
{
  ot->name = "Update Strokes";
  ot->description = "Update LANPR strokes for selected Mesh object.";
  ot->idname = "OBJECT_OT_lanpr_update_gp_source";

  ot->poll = lanpr_active_is_source_object;
  ot->exec = lanpr_update_gp_source_exec;
}
