/*
	GradientUI.cpp — custom Effect-Controls UI for the multi-stop gradient.

	Layout (inside the control frame):
		[ gradient preview bar ]      <- live, uses the selected space/hue
		 ^   ^         ^   ^          <- square markers, one per stop

	Interactions:
		click empty bar         -> add a stop at that position
		click marker            -> select it (then drag to move)
		drag marker             -> move horizontally (clamped between neighbors)
		double-click marker     -> color picker
		Alt/Opt-click marker    -> delete (keeps >= 2 stops)
*/
#include "Ramp.h"

// Transient UI selection (single-threaded UI thread; fine as a static).
static A_long g_selected_stop = -1;

// ---------------------------------------------------------------------------
static void PFToDrawbotRect(const PF_Rect *inR, DRAWBOT_RectF32 *outR)
{
	outR->left   = inR->left + 0.5f;
	outR->top    = inR->top + 0.5f;
	outR->width  = (float)(inR->right - inR->left);
	outR->height = (float)(inR->bottom - inR->top);
}

#define kMaxShortColor 65535
static DRAWBOT_ColorRGBA QDtoDRAWBOTColor(const PF_App_Color *c)
{
	const float inv = 1.0f / (float)kMaxShortColor;
	DRAWBOT_ColorRGBA col;
	col.red = c->red * inv; col.green = c->green * inv; col.blue = c->blue * inv; col.alpha = 1.0f;
	return col;
}

static PF_Err
AcquireBackgroundColor(PF_InData *in_data, PF_OutData *out_data, DRAWBOT_ColorRGBA *bg)
{
	PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
	PFAppSuite4 *app = NULL;
	PF_App_Color local = {0,0,0};
	ERR(AEFX_AcquireSuite(in_data, out_data, kPFAppSuite, kPFAppSuiteVersion4, NULL, (void**)&app));
	if (app) {
		ERR(app->PF_AppGetBgColor(&local));
		if (!err && bg) *bg = QDtoDRAWBOTColor(&local);
	}
	ERR2(AEFX_ReleaseSuite(in_data, out_data, kPFAppSuite, kPFAppSuiteVersion4, NULL));
	return err;
}

// Geometry derived from the control frame.
typedef struct {
	PF_Rect	bar;			// preview bar rect
	A_long	mark_top, mark_bottom;
} RampUIGeom;

static void ComputeGeom(const PF_Rect *frame, RampUIGeom *g)
{
	g->bar.left   = frame->left + RAMP_UI_PAD;
	g->bar.right  = frame->right - RAMP_UI_PAD;
	g->bar.top    = frame->top + RAMP_UI_BAR_TOP;
	g->bar.bottom = g->bar.top + RAMP_UI_BAR_H;
	g->mark_top    = g->bar.bottom + 1;
	g->mark_bottom = g->mark_top + RAMP_UI_MARK_H;
}

static A_long MarkerX(const RampUIGeom *g, float pos)
{
	A_long w = g->bar.right - g->bar.left;
	return g->bar.left + (A_long)(pos * (float)w + 0.5f);
}

// Hit-test: returns stop index under the mouse (marker strip), or -1.
static A_long HitMarker(const RampUIGeom *g, const RampArb *arbP, const PF_Point *pt)
{
	if (pt->v < g->mark_top - 2 || pt->v > g->mark_bottom + 2) return -1;
	A_long best = -1, bestDist = RAMP_UI_HIT_PX + 1;
	for (int i = 0; i < arbP->num_stops; ++i) {
		A_long mx = MarkerX(g, arbP->stops[i].position);
		A_long d = (pt->h > mx) ? (pt->h - mx) : (mx - pt->h);
		if (d <= RAMP_UI_HIT_PX && d < bestDist) { bestDist = d; best = i; }
	}
	return best;
}

// ---------------------------------------------------------------------------
PF_Err
DrawEvent(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
		  PF_LayerDef *output, PF_EventExtra *extra)
{
	PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
	if (extra->effect_win.area != PF_EA_CONTROL) return err;

	AEGP_SuiteHandler suites(in_data->pica_basicP);

	DRAWBOT_DrawRef     drawing_ref  = NULL;
	DRAWBOT_SurfaceRef  surface_ref  = NULL;
	DRAWBOT_SupplierRef supplier_ref = NULL;
	DRAWBOT_PathRef     path_ref     = NULL;
	DRAWBOT_BrushRef    brush_ref    = NULL;
	DRAWBOT_PenRef      pen_ref      = NULL;
	DRAWBOT_Suites      db;
	DRAWBOT_ColorRGBA   bg;

	ERR(AEFX_AcquireDrawbotSuites(in_data, out_data, &db));

	PF_EffectCustomUISuite1 *uiP = NULL;
	ERR(AEFX_AcquireSuite(in_data, out_data, kPFEffectCustomUISuite,
						  kPFEffectCustomUISuiteVersion1, NULL, (void**)&uiP));
	if (!err && uiP) {
		err = (*uiP->PF_GetDrawingReference)(extra->contextH, &drawing_ref);
		AEFX_ReleaseSuite(in_data, out_data, kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, NULL);
	}
	ERR(db.drawbot_suiteP->GetSupplier(drawing_ref, &supplier_ref));
	ERR(db.drawbot_suiteP->GetSurface(drawing_ref, &surface_ref));
	ERR(AcquireBackgroundColor(in_data, out_data, &bg));

	const PF_Rect *frame = &extra->effect_win.current_frame;
	RampUIGeom geom; ComputeGeom(frame, &geom);

	// Background
	DRAWBOT_RectF32 frameR;
	{
		PF_Rect fr = *frame; fr.bottom += 1;
		PFToDrawbotRect(&fr, &frameR);
	}
	ERR(db.surface_suiteP->PaintRect(surface_ref, &bg, &frameR));

	// Prepare gradient evaluation
	PF_Handle arbH = params[RAMP_GRADIENT]->u.arb_d.value;
	RampArb  *arbP = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(arbH));
	int   space = params[RAMP_SPACE]->u.pd.value - 1;
	int   hue   = params[RAMP_HUE]->u.pd.value - 1;

	if (!err && arbP) {
		cm::Stop stops[RAMP_MAX_STOPS];
		int n = RampArb_ToStops(arbP, stops);

		// Preview bar: vertical strips sampled across the gradient.
		A_long bar_w = geom.bar.right - geom.bar.left;
		const A_long step = 2;
		for (A_long sx = 0; sx < bar_w && !err; sx += step) {
			float t = (bar_w > 1) ? (float)sx / (float)(bar_w - 1) : 0.0f;
			cm::RGBA c = cm::evaluate_gradient(stops, n, t, (cm::Space)space, (cm::HuePath)hue);

			PF_Rect strip;
			strip.left   = geom.bar.left + sx;
			strip.right  = strip.left + step;
			if (strip.right > geom.bar.right) strip.right = geom.bar.right;
			strip.top    = geom.bar.top;
			strip.bottom = geom.bar.bottom;

			DRAWBOT_ColorRGBA col = { c.r, c.g, c.b, 1.0f };
			DRAWBOT_RectF32 r; PFToDrawbotRect(&strip, &r);
			ERR(db.supplier_suiteP->NewPath(supplier_ref, &path_ref));
			ERR(db.path_suiteP->AddRect(path_ref, &r));
			ERR(db.supplier_suiteP->NewBrush(supplier_ref, &col, &brush_ref));
			ERR(db.surface_suiteP->FillPath(surface_ref, brush_ref, path_ref, kDRAWBOT_FillType_Default));
			if (brush_ref) { ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)brush_ref)); brush_ref = NULL; }
			if (path_ref)  { ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path_ref));  path_ref = NULL; }
		}

		// Bar outline
		DRAWBOT_ColorRGBA dark = { 0,0,0,1 };
		ERR(db.supplier_suiteP->NewPen(supplier_ref, &dark, 1.0f, &pen_ref));
		{
			DRAWBOT_RectF32 r; PFToDrawbotRect(&geom.bar, &r);
			ERR(db.supplier_suiteP->NewPath(supplier_ref, &path_ref));
			ERR(db.path_suiteP->AddRect(path_ref, &r));
			ERR(db.surface_suiteP->StrokePath(surface_ref, pen_ref, path_ref));
			if (path_ref) { ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path_ref)); path_ref = NULL; }
		}
		if (pen_ref) { ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)pen_ref)); pen_ref = NULL; }

		// Markers
		for (int i = 0; i < n && !err; ++i) {
			A_long mx = MarkerX(&geom, arbP->stops[i].position);
			PF_Rect m;
			m.left = mx - 5; m.right = mx + 5;
			m.top = geom.mark_top; m.bottom = geom.mark_top + 10;

			DRAWBOT_ColorRGBA fill = { arbP->stops[i].red, arbP->stops[i].green, arbP->stops[i].blue, 1.0f };
			DRAWBOT_RectF32 r; PFToDrawbotRect(&m, &r);
			ERR(db.supplier_suiteP->NewPath(supplier_ref, &path_ref));
			ERR(db.path_suiteP->AddRect(path_ref, &r));
			ERR(db.supplier_suiteP->NewBrush(supplier_ref, &fill, &brush_ref));
			ERR(db.surface_suiteP->FillPath(surface_ref, brush_ref, path_ref, kDRAWBOT_FillType_Default));
			if (brush_ref) { ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)brush_ref)); brush_ref = NULL; }

			// Outline: white if selected, else black.
			DRAWBOT_ColorRGBA oc = (i == g_selected_stop) ? DRAWBOT_ColorRGBA{1,1,1,1}
														  : DRAWBOT_ColorRGBA{0,0,0,1};
			ERR(db.supplier_suiteP->NewPen(supplier_ref, &oc, (i == g_selected_stop) ? 2.0f : 1.0f, &pen_ref));
			ERR(db.surface_suiteP->StrokePath(surface_ref, pen_ref, path_ref));
			if (pen_ref)  { ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)pen_ref)); pen_ref = NULL; }
			if (path_ref) { ERR2(db.supplier_suiteP->ReleaseObject((DRAWBOT_ObjectRef)path_ref)); path_ref = NULL; }
		}
		suites.HandleSuite1()->host_unlock_handle(arbH);
	}

	ERR2(AEFX_ReleaseDrawbotSuites(in_data, out_data));
	extra->evt_out_flags = PF_EO_HANDLED_EVENT;
	return err;
}

// ---------------------------------------------------------------------------
static PF_Err
PickColor(PF_InData *in_data, RampArb *arbP, A_long idx)
{
	PF_Err err = PF_Err_NONE;
	if (idx < 0 || idx >= arbP->num_stops) return err;
	if (in_data->appl_id == kAppID_Premiere) return err;

	AEGP_SuiteHandler suites(in_data->pica_basicP);
	PF_PixelFloat c;
	c.red = arbP->stops[idx].red; c.green = arbP->stops[idx].green;
	c.blue = arbP->stops[idx].blue; c.alpha = arbP->stops[idx].alpha;
	ERR(suites.AppSuite4()->PF_AppColorPickerDialog("Stop Color", &c, TRUE, &c));
	if (!err) {
		arbP->stops[idx].red = cm::clamp01(c.red);
		arbP->stops[idx].green = cm::clamp01(c.green);
		arbP->stops[idx].blue = cm::clamp01(c.blue);
	}
	return err;
}

PF_Err
DoClick(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
		PF_LayerDef *output, PF_EventExtra *extra)
{
	PF_Err err = PF_Err_NONE;
	if (extra->effect_win.area != PF_EA_CONTROL) return err;

	AEGP_SuiteHandler suites(in_data->pica_basicP);
	PF_Handle arbH = params[RAMP_GRADIENT]->u.arb_d.value;
	RampArb  *arbP = reinterpret_cast<RampArb*>(PF_LOCK_HANDLE(arbH));
	if (!arbP) return PF_Err_OUT_OF_MEMORY;

	PF_Point mouse = extra->u.do_click.screen_point;
	const PF_Rect *frame = &extra->effect_win.current_frame;
	RampUIGeom geom; ComputeGeom(frame, &geom);

	PF_Boolean changed = FALSE;
	A_long hit = HitMarker(&geom, arbP, &mouse);

	if (hit >= 0) {
		g_selected_stop = hit;
		if (extra->u.do_click.modifiers & PF_Mod_OPT_ALT_KEY) {
			// delete (keep at least 2)
			if (arbP->num_stops > 2) {
				for (int j = hit; j < arbP->num_stops - 1; ++j)
					arbP->stops[j] = arbP->stops[j + 1];
				arbP->num_stops--;
				g_selected_stop = -1;
				changed = TRUE;
			}
		} else if (extra->u.do_click.num_clicks >= 2) {
			err = PickColor(in_data, arbP, hit);
			changed = TRUE;
		} else {
			extra->u.do_click.send_drag = TRUE;	// begin dragging this stop
		}
	} else {
		// Click on the bar -> add a stop here.
		PF_Boolean inBar = (mouse.h >= geom.bar.left && mouse.h <= geom.bar.right &&
							mouse.v >= geom.bar.top - 2 && mouse.v <= geom.bar.bottom + 2);
		if (inBar && arbP->num_stops < RAMP_MAX_STOPS) {
			A_long w = geom.bar.right - geom.bar.left;
			float pos = (w > 0) ? cm::clamp01((float)(mouse.h - geom.bar.left) / (float)w) : 0.0f;

			cm::Stop tmp[RAMP_MAX_STOPS];
			int n = RampArb_ToStops(arbP, tmp);
			int space = params[RAMP_SPACE]->u.pd.value - 1;
			int hue   = params[RAMP_HUE]->u.pd.value - 1;
			cm::RGBA c = cm::evaluate_gradient(tmp, n, pos, (cm::Space)space, (cm::HuePath)hue);

			RampStop ns;
			ns.position = pos; ns.red = c.r; ns.green = c.g; ns.blue = c.b; ns.alpha = c.a;
			ns.easing = (A_u_char)cm::Easing::Linear; ns.pad0 = ns.pad1 = ns.pad2 = 0; ns.midpoint = 0.5f;
			arbP->stops[arbP->num_stops++] = ns;
			RampArb_Sanitize(arbP);
			// reselect the new stop by position
			for (int i = 0; i < arbP->num_stops; ++i)
				if (arbP->stops[i].position == pos) { g_selected_stop = i; break; }
			extra->u.do_click.send_drag = TRUE;
			changed = TRUE;
		}
	}

	PF_UNLOCK_HANDLE(arbH);

	PF_Rect inval = extra->effect_win.current_frame;
	ERR(suites.AppSuite4()->PF_InvalidateRect(extra->contextH, &inval));
	extra->evt_out_flags |= PF_EO_HANDLED_EVENT | PF_EO_UPDATE_NOW;
	if (changed) params[RAMP_GRADIENT]->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
	return err;
}

PF_Err
DragEvent(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
		  PF_LayerDef *output, PF_EventExtra *extra)
{
	PF_Err err = PF_Err_NONE;
	if (g_selected_stop < 0) return err;
	if (extra->effect_win.area != PF_EA_CONTROL) return err;

	AEGP_SuiteHandler suites(in_data->pica_basicP);
	PF_Handle arbH = params[RAMP_GRADIENT]->u.arb_d.value;
	RampArb  *arbP = reinterpret_cast<RampArb*>(PF_LOCK_HANDLE(arbH));
	if (!arbP) return PF_Err_OUT_OF_MEMORY;

	if (g_selected_stop < arbP->num_stops) {
		PF_Point mouse = extra->u.do_click.screen_point;
		const PF_Rect *frame = &extra->effect_win.current_frame;
		RampUIGeom geom; ComputeGeom(frame, &geom);
		A_long w = geom.bar.right - geom.bar.left;
		float pos = (w > 0) ? cm::clamp01((float)(mouse.h - geom.bar.left) / (float)w) : 0.0f;

		// Clamp between neighbors so the index stays stable (no reordering).
		float lo = (g_selected_stop > 0) ? arbP->stops[g_selected_stop - 1].position + 0.0005f : 0.0f;
		float hi = (g_selected_stop < arbP->num_stops - 1) ? arbP->stops[g_selected_stop + 1].position - 0.0005f : 1.0f;
		if (pos < lo) pos = lo;
		if (pos > hi) pos = hi;
		arbP->stops[g_selected_stop].position = pos;
	}

	PF_UNLOCK_HANDLE(arbH);

	PF_Rect inval = extra->effect_win.current_frame;
	ERR(suites.AppSuite4()->PF_InvalidateRect(extra->contextH, &inval));
	extra->evt_out_flags |= PF_EO_HANDLED_EVENT | PF_EO_UPDATE_NOW;
	params[RAMP_GRADIENT]->uu.change_flags |= PF_ChangeFlag_CHANGED_VALUE;
	return err;
}

PF_Err
ChangeCursor(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
			 PF_LayerDef *output, PF_EventExtra *extra)
{
	if (extra->effect_win.area != PF_EA_CONTROL) return PF_Err_NONE;

	AEGP_SuiteHandler suites(in_data->pica_basicP);
	PF_Handle arbH = params[RAMP_GRADIENT]->u.arb_d.value;
	RampArb  *arbP = reinterpret_cast<RampArb*>(PF_LOCK_HANDLE(arbH));
	if (arbP) {
		PF_Point mouse = extra->u.adjust_cursor.screen_point;
		const PF_Rect *frame = &extra->effect_win.current_frame;
		RampUIGeom geom; ComputeGeom(frame, &geom);
		if (HitMarker(&geom, arbP, &mouse) >= 0) {
			extra->u.adjust_cursor.set_cursor = PF_Cursor_HOLLOW_ARROW;
		} else if (mouse.h >= geom.bar.left && mouse.h <= geom.bar.right &&
				   mouse.v >= geom.bar.top && mouse.v <= geom.bar.bottom) {
			extra->u.adjust_cursor.set_cursor = PF_Cursor_CROSSHAIRS;
		}
		PF_UNLOCK_HANDLE(arbH);
	}
	return PF_Err_NONE;
}
