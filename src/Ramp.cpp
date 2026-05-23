/*
	Ramp.cpp — entry, params (ARB gradient + popups), render.
	Classic Render (8/16) and Smart FX (8/16/32f) both sample the multi-stop
	gradient via the SDK-free color engine.
*/
#include "Ramp.h"

// ---------------------------------------------------------------------------
// Convert (a sanitized copy of) the ARB blob into cm::Stop[]. Returns count.
int
RampArb_ToStops(const RampArb *arbP, cm::Stop out[RAMP_MAX_STOPS])
{
	RampArb tmp = *arbP;
	RampArb_Sanitize(&tmp);
	for (int i = 0; i < tmp.num_stops; ++i) {
		const RampStop *s = &tmp.stops[i];
		out[i].position = s->position;
		out[i].color    = { s->red, s->green, s->blue, s->alpha };
		out[i].easing   = (cm::Easing)s->easing;
		out[i].midpoint = s->midpoint;
	}
	return tmp.num_stops;
}

static inline cm::RGBA EvalColor(const RampRenderInfo *ri, float t)
{
	return cm::evaluate_gradient(ri->stops, ri->num_stops, t,
								 (cm::Space)ri->space, (cm::HuePath)ri->hue);
}

// Remap luma through input black/white, then apply offset/repeat/reverse.
static inline float MapT(const RampRenderInfo *ri, float luma)
{
	float d = ri->inputWhite - ri->inputBlack;
	float v = (d > -1e-6f && d < 1e-6f) ? luma : (luma - ri->inputBlack) / d;
	return shapes::finalize_t(ri->shape, v);
}

static inline void ClampXY(const PF_EffectWorld *w, A_long &x, A_long &y)
{
	if (x < 0) x = 0; else if (x >= w->width)  x = w->width  - 1;
	if (y < 0) y = 0; else if (y >= w->height) y = w->height - 1;
}
static inline float MapLuma8(const PF_EffectWorld *w, A_long x, A_long y)
{
	ClampXY(w, x, y);
	const PF_Pixel8 *p = reinterpret_cast<const PF_Pixel8*>(
		reinterpret_cast<const char*>(w->data) + (size_t)y * w->rowbytes) + x;
	return (0.2126f * p->red + 0.7152f * p->green + 0.0722f * p->blue) / (float)PF_MAX_CHAN8;
}
static inline float MapLuma16(const PF_EffectWorld *w, A_long x, A_long y)
{
	ClampXY(w, x, y);
	const PF_Pixel16 *p = reinterpret_cast<const PF_Pixel16*>(
		reinterpret_cast<const char*>(w->data) + (size_t)y * w->rowbytes) + x;
	return (0.2126f * p->red + 0.7152f * p->green + 0.0722f * p->blue) / (float)PF_MAX_CHAN16;
}
static inline float MapLumaFloat(const PF_EffectWorld *w, A_long x, A_long y)
{
	ClampXY(w, x, y);
	const PF_PixelFloat *p = reinterpret_cast<const PF_PixelFloat*>(
		reinterpret_cast<const char*>(w->data) + (size_t)y * w->rowbytes) + x;
	return 0.2126f * p->red + 0.7152f * p->green + 0.0722f * p->blue;
}

// ---------------------------------------------------------------------------
static PF_Err RampFunc8(void *refcon, A_long x, A_long y, PF_Pixel8 *inP, PF_Pixel8 *outP)
{
	const RampRenderInfo *ri = reinterpret_cast<RampRenderInfo*>(refcon);
	float t = ri->useMap ? (ri->mapWorld ? MapT(ri, MapLuma8(ri->mapWorld, x, y)) : 0.0f)
						  : shapes::shape_t(ri->shape, (float)x, (float)y);
	cm::RGBA c = EvalColor(ri, t);
	outP->alpha = (A_u_char)(cm::clamp01(c.a) * PF_MAX_CHAN8 + 0.5f);
	outP->red   = (A_u_char)(cm::clamp01(c.r) * PF_MAX_CHAN8 + 0.5f);
	outP->green = (A_u_char)(cm::clamp01(c.g) * PF_MAX_CHAN8 + 0.5f);
	outP->blue  = (A_u_char)(cm::clamp01(c.b) * PF_MAX_CHAN8 + 0.5f);
	return PF_Err_NONE;
}
static PF_Err RampFunc16(void *refcon, A_long x, A_long y, PF_Pixel16 *inP, PF_Pixel16 *outP)
{
	const RampRenderInfo *ri = reinterpret_cast<RampRenderInfo*>(refcon);
	float t = ri->useMap ? (ri->mapWorld ? MapT(ri, MapLuma16(ri->mapWorld, x, y)) : 0.0f)
						  : shapes::shape_t(ri->shape, (float)x, (float)y);
	cm::RGBA c = EvalColor(ri, t);
	outP->alpha = (A_u_short)(cm::clamp01(c.a) * PF_MAX_CHAN16 + 0.5f);
	outP->red   = (A_u_short)(cm::clamp01(c.r) * PF_MAX_CHAN16 + 0.5f);
	outP->green = (A_u_short)(cm::clamp01(c.g) * PF_MAX_CHAN16 + 0.5f);
	outP->blue  = (A_u_short)(cm::clamp01(c.b) * PF_MAX_CHAN16 + 0.5f);
	return PF_Err_NONE;
}
static PF_Err RampFuncFloat(void *refcon, A_long x, A_long y, PF_PixelFloat *inP, PF_PixelFloat *outP)
{
	const RampRenderInfo *ri = reinterpret_cast<RampRenderInfo*>(refcon);
	float t = ri->useMap ? (ri->mapWorld ? MapT(ri, MapLumaFloat(ri->mapWorld, x, y)) : 0.0f)
						  : shapes::shape_t(ri->shape, (float)x, (float)y);
	cm::RGBA c = EvalColor(ri, t);
	outP->alpha = cm::clamp01(c.a);
	outP->red   = c.r; outP->green = c.g; outP->blue = c.b;	// keep float range
	return PF_Err_NONE;
}

// ---------------------------------------------------------------------------
static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "%s v%d.%d\r%s",
		NAME, MAJOR_VERSION, MINOR_VERSION, DESCRIPTION);
	return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
	out_data->out_flags  |= PF_OutFlag_CUSTOM_UI | PF_OutFlag_USE_OUTPUT_EXTENT |
							PF_OutFlag_PIX_INDEPENDENT | PF_OutFlag_DEEP_COLOR_AWARE;
	out_data->out_flags2 |= PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_SMART_RENDER |
							PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
	return PF_Err_NONE;
}

static PF_Err
ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err		err = PF_Err_NONE;
	PF_ParamDef	def;

	AEFX_CLR_STRUCT(def);
	ERR(CreateDefaultArb(in_data, out_data, &def.u.arb_d.dephault));
	PF_ADD_ARBITRARY2("Gradient",
					  RAMP_UI_WIDTH, RAMP_UI_HEIGHT,
					  0,
					  PF_PUI_CONTROL | PF_PUI_DONT_ERASE_CONTROL,
					  def.u.arb_d.dephault,
					  GRADIENT_DISK_ID,
					  ARB_REFCON);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Shape", RAMP_SHAPE_COUNT, RAMP_SHAPE_DFLT, RAMP_SHAPE_CHOICES, SHAPE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POINT("Start", 0, 50, 0, START_DISK_ID);			// % of layer (left-center)

	AEFX_CLR_STRUCT(def);
	PF_ADD_POINT("End", 100, 50, 0, END_DISK_ID);			// % of layer (right-center)

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Repeat", RAMP_REPEAT_COUNT, RAMP_REPEAT_DFLT, RAMP_REPEAT_CHOICES, REPEAT_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Offset", -1, 1, -1, 1, 0, PF_Precision_HUNDREDTHS, 0, 0, OFFSET_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOXX("Reverse", FALSE, 0, REVERSE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Interpolation", RAMP_SPACE_COUNT, RAMP_SPACE_DFLT, RAMP_SPACE_CHOICES, SPACE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Hue Path", RAMP_HUE_COUNT, RAMP_HUE_DFLT, RAMP_HUE_CHOICES, HUE_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_LAYER("Map Layer", PF_LayerDefault_MYSELF, MAP_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Input Black", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, IN_BLACK_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Input White", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, IN_WHITE_DISK_ID);

	if (!err) {
		PF_CustomUIInfo ci;
		AEFX_CLR_STRUCT(ci);
		ci.events            = PF_CustomEFlag_EFFECT;
		ci.comp_ui_width     = ci.comp_ui_height = 0;
		ci.comp_ui_alignment = PF_UIAlignment_NONE;
		ci.layer_ui_width    = ci.layer_ui_height = 0;
		ci.layer_ui_alignment = PF_UIAlignment_NONE;
		ci.preview_ui_width  = ci.preview_ui_height = 0;
		err = (*(in_data->inter.register_ui))(in_data->effect_ref, &ci);
	}
	out_data->num_params = RAMP_NUM_PARAMS;
	return err;
}

// ---------------------------------------------------------------------------
// Build the render info: stops from the ARB handle + geometry from points/popups.
static void
BuildRenderInfo(PF_Handle arbH, AEGP_SuiteHandler &suites,
				int shapeType, float sx, float sy, float ex, float ey,
				int repeat, float offset, int reverse,
				int space, int hue,
				PF_EffectWorld *mapWorld, float inBlack, float inWhite,
				RampRenderInfo *ri)
{
	AEFX_CLR_STRUCT(*ri);
	ri->space = space;
	ri->hue   = hue;

	RampArb *arbP = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(arbH));
	if (arbP) {
		ri->num_stops = RampArb_ToStops(arbP, ri->stops);
		suites.HandleSuite1()->host_unlock_handle(arbH);
	} else {
		ri->num_stops = 0;
	}

	shapes::ShapeParams &sp = ri->shape;
	sp.type   = (shapes::Shape)shapeType;
	sp.startX = sx; sp.startY = sy; sp.endX = ex; sp.endY = ey;
	sp.centerX = sx; sp.centerY = sy;	// radial/angular/diamond pivot on Start
	float dx = ex - sx, dy = ey - sy;
	sp.radius = std::sqrt(dx * dx + dy * dy);				// |End - Start|
	sp.angle  = std::atan2(dy, dx) * 57.2957795131f;		// Start->End direction
	sp.repeat = (shapes::Repeat)repeat;
	sp.offset = offset;
	sp.reverse = (reverse != 0);

	ri->useMap     = (sp.type == shapes::Shape::Map) ? 1 : 0;
	ri->mapWorld   = (mapWorld && mapWorld->data && mapWorld->width > 0) ? mapWorld : NULL;
	ri->inputBlack = inBlack;
	ri->inputWhite = inWhite;
}

static PF_Err
Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	// Secondary layer params are NOT auto-populated for classic render -> check it out.
	PF_ParamDef map_param;
	AEFX_CLR_STRUCT(map_param);
	ERR(PF_CHECKOUT_PARAM(in_data, RAMP_MAP, in_data->current_time,
						  in_data->time_step, in_data->time_scale, &map_param));
	PF_EffectWorld *mw = (!err && map_param.u.ld.data) ? &map_param.u.ld : NULL;

	RampRenderInfo ri;
	BuildRenderInfo(
		params[RAMP_GRADIENT]->u.arb_d.value, suites,
		params[RAMP_SHAPE]->u.pd.value - 1,
		(float)FIX_2_FLOAT(params[RAMP_START]->u.td.x_value),
		(float)FIX_2_FLOAT(params[RAMP_START]->u.td.y_value),
		(float)FIX_2_FLOAT(params[RAMP_END]->u.td.x_value),
		(float)FIX_2_FLOAT(params[RAMP_END]->u.td.y_value),
		params[RAMP_REPEAT]->u.pd.value - 1,
		(float)params[RAMP_OFFSET]->u.fs_d.value,
		params[RAMP_REVERSE]->u.bd.value,
		params[RAMP_SPACE]->u.pd.value - 1,
		params[RAMP_HUE]->u.pd.value - 1,
		mw,
		(float)(params[RAMP_IN_BLACK]->u.fs_d.value / 100.0),
		(float)(params[RAMP_IN_WHITE]->u.fs_d.value / 100.0),
		&ri);

	A_long linesL = output->extent_hint.bottom - output->extent_hint.top;
	if (PF_WORLD_IS_DEEP(output)) {
		ERR(suites.Iterate16Suite2()->iterate(in_data, 0, linesL, &params[RAMP_INPUT]->u.ld,
											  NULL, (void*)&ri, RampFunc16, output));
	} else {
		ERR(suites.Iterate8Suite2()->iterate(in_data, 0, linesL, &params[RAMP_INPUT]->u.ld,
											 NULL, (void*)&ri, RampFunc8, output));
	}
	ERR2(PF_CHECKIN_PARAM(in_data, &map_param));
	return err;
}

// ---------------------------------------------------------------------------
static PF_Err
PreRender(PF_InData *in_data, PF_OutData *out_data, PF_PreRenderExtra *extra)
{
	PF_Err err = PF_Err_NONE;
	PF_ParamDef arb_param;
	PF_RenderRequest req = extra->input->output_request;
	PF_CheckoutResult in_result;

	AEFX_CLR_STRUCT(arb_param);
	ERR(PF_CHECKOUT_PARAM(in_data, RAMP_GRADIENT, in_data->current_time,
						  in_data->time_step, in_data->time_scale, &arb_param));
	ERR(extra->cb->checkout_layer(in_data->effect_ref, RAMP_INPUT, RAMP_INPUT, &req,
								  in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
	if (!err) {
		UnionLRect(&in_result.result_rect, &extra->output->result_rect);
		UnionLRect(&in_result.max_result_rect, &extra->output->max_result_rect);
	}
	// Register the map layer so its pixels are available in SmartRender (id = RAMP_MAP).
	// Unioning its rect is what makes AE actually prepare/cache the layer. Tolerate
	// failure: there may be no map assigned.
	{
		PF_CheckoutResult map_result;
		PF_Err map_err = extra->cb->checkout_layer(in_data->effect_ref, RAMP_MAP, RAMP_MAP, &req,
												   in_data->current_time, in_data->time_step,
												   in_data->time_scale, &map_result);
		if (!map_err) {
			UnionLRect(&map_result.result_rect, &extra->output->result_rect);
			UnionLRect(&map_result.max_result_rect, &extra->output->max_result_rect);
		}
	}
	ERR(PF_CHECKIN_PARAM(in_data, &arb_param));
	return err;
}

static PF_Err
SmartRender(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra)
{
	PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	PF_EffectWorld *output_worldP = NULL;
	PF_WorldSuite2 *wsP = NULL;
	PF_PixelFormat  format = PF_PixelFormat_INVALID;
	PF_Point        origin = {0,0};

	PF_ParamDef p_grad, p_shape, p_start, p_end, p_repeat, p_offset, p_reverse, p_space, p_hue, p_inb, p_inw;
	AEFX_CLR_STRUCT(p_grad);   AEFX_CLR_STRUCT(p_shape);  AEFX_CLR_STRUCT(p_start);
	AEFX_CLR_STRUCT(p_end);    AEFX_CLR_STRUCT(p_repeat); AEFX_CLR_STRUCT(p_offset);
	AEFX_CLR_STRUCT(p_reverse);AEFX_CLR_STRUCT(p_space);  AEFX_CLR_STRUCT(p_hue);
	AEFX_CLR_STRUCT(p_inb);    AEFX_CLR_STRUCT(p_inw);

#define RAMP_CO(IDX, VAR) ERR(PF_CHECKOUT_PARAM(in_data, (IDX), in_data->current_time, \
								in_data->time_step, in_data->time_scale, &(VAR)))
	RAMP_CO(RAMP_GRADIENT, p_grad);  RAMP_CO(RAMP_SHAPE,  p_shape);  RAMP_CO(RAMP_START,  p_start);
	RAMP_CO(RAMP_END,      p_end);   RAMP_CO(RAMP_REPEAT, p_repeat); RAMP_CO(RAMP_OFFSET, p_offset);
	RAMP_CO(RAMP_REVERSE,  p_reverse);RAMP_CO(RAMP_SPACE, p_space);  RAMP_CO(RAMP_HUE,    p_hue);
	RAMP_CO(RAMP_IN_BLACK, p_inb);   RAMP_CO(RAMP_IN_WHITE, p_inw);
#undef RAMP_CO

	// Must checkout at least one input BEFORE checkout_output. We don't use the
	// input pixels (generator), but checking them out satisfies that rule and
	// guarantees an input even when no Map layer is assigned.
	PF_EffectWorld *inputWorld = NULL, *mapWorld = NULL;
	ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, RAMP_INPUT, &inputWorld));
	(void)extra->cb->checkout_layer_pixels(in_data->effect_ref, RAMP_MAP, &mapWorld);

	ERR(extra->cb->checkout_output(in_data->effect_ref, &output_worldP));

	if (!err && output_worldP) {
		RampRenderInfo ri;
		BuildRenderInfo(
			p_grad.u.arb_d.value, suites,
			p_shape.u.pd.value - 1,
			(float)FIX_2_FLOAT(p_start.u.td.x_value), (float)FIX_2_FLOAT(p_start.u.td.y_value),
			(float)FIX_2_FLOAT(p_end.u.td.x_value),   (float)FIX_2_FLOAT(p_end.u.td.y_value),
			p_repeat.u.pd.value - 1, (float)p_offset.u.fs_d.value, p_reverse.u.bd.value,
			p_space.u.pd.value - 1, p_hue.u.pd.value - 1,
			mapWorld, (float)(p_inb.u.fs_d.value / 100.0), (float)(p_inw.u.fs_d.value / 100.0),
			&ri);

		ERR(AEFX_AcquireSuite(in_data, out_data, kPFWorldSuite, kPFWorldSuiteVersion2,
							  "Couldn't load suite.", (void**)&wsP));
		ERR(wsP->PF_GetPixelFormat(output_worldP, &format));

		if (!err) {
			switch (format) {
			case PF_PixelFormat_ARGB128:
				ERR(suites.IterateFloatSuite2()->iterate_origin(in_data, 0, output_worldP->height,
					output_worldP, NULL, &origin, (void*)&ri, RampFuncFloat, output_worldP));
				break;
			case PF_PixelFormat_ARGB64:
				ERR(suites.Iterate16Suite2()->iterate_origin(in_data, 0, output_worldP->height,
					output_worldP, NULL, &origin, (void*)&ri, RampFunc16, output_worldP));
				break;
			case PF_PixelFormat_ARGB32:
				ERR(suites.Iterate8Suite2()->iterate_origin(in_data, 0, output_worldP->height,
					output_worldP, NULL, &origin, (void*)&ri, RampFunc8, output_worldP));
				break;
			default:
				err = PF_Err_BAD_CALLBACK_PARAM;
				break;
			}
		}
	}
	(void)extra->cb->checkin_layer_pixels(in_data->effect_ref, RAMP_INPUT);
	(void)extra->cb->checkin_layer_pixels(in_data->effect_ref, RAMP_MAP);
	ERR2(AEFX_ReleaseSuite(in_data, out_data, kPFWorldSuite, kPFWorldSuiteVersion2, "Couldn't release suite."));
	ERR2(PF_CHECKIN_PARAM(in_data, &p_grad));   ERR2(PF_CHECKIN_PARAM(in_data, &p_shape));
	ERR2(PF_CHECKIN_PARAM(in_data, &p_start));  ERR2(PF_CHECKIN_PARAM(in_data, &p_end));
	ERR2(PF_CHECKIN_PARAM(in_data, &p_repeat)); ERR2(PF_CHECKIN_PARAM(in_data, &p_offset));
	ERR2(PF_CHECKIN_PARAM(in_data, &p_reverse));ERR2(PF_CHECKIN_PARAM(in_data, &p_space));
	ERR2(PF_CHECKIN_PARAM(in_data, &p_hue));
	ERR2(PF_CHECKIN_PARAM(in_data, &p_inb));    ERR2(PF_CHECKIN_PARAM(in_data, &p_inw));
	return err;
}

// ---------------------------------------------------------------------------
static PF_Err
HandleEvent(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
			PF_LayerDef *output, PF_EventExtra *extra)
{
	PF_Err err = PF_Err_NONE;
	switch (extra->e_type) {
		case PF_Event_DO_CLICK:		err = DoClick(in_data, out_data, params, output, extra); break;
		case PF_Event_DRAG:			err = DragEvent(in_data, out_data, params, output, extra); break;
		case PF_Event_DRAW:			err = DrawEvent(in_data, out_data, params, output, extra); break;
		case PF_Event_ADJUST_CURSOR:err = ChangeCursor(in_data, out_data, params, output, extra); break;
		default: break;
	}
	return err;
}

static PF_Err
HandleArbitrary(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[],
				PF_LayerDef *output, PF_ArbParamsExtra *extra)
{
	PF_Err err = PF_Err_NONE;
	void *srcP = NULL, *dstP = NULL;

	switch (extra->which_function) {
	case PF_Arbitrary_NEW_FUNC:
		if (extra->u.new_func_params.refconPV != ARB_REFCON) err = PF_Err_INTERNAL_STRUCT_DAMAGED;
		else err = CreateDefaultArb(in_data, out_data, extra->u.new_func_params.arbPH);
		break;
	case PF_Arbitrary_DISPOSE_FUNC:
		if (extra->u.dispose_func_params.refconPV != ARB_REFCON) err = PF_Err_INTERNAL_STRUCT_DAMAGED;
		else PF_DISPOSE_HANDLE(extra->u.dispose_func_params.arbH);
		break;
	case PF_Arbitrary_COPY_FUNC:
		if (extra->u.copy_func_params.refconPV == ARB_REFCON) {
			ERR(CreateDefaultArb(in_data, out_data, extra->u.copy_func_params.dst_arbPH));
			ERR(Arb_Copy(in_data, out_data, &extra->u.copy_func_params.src_arbH, extra->u.copy_func_params.dst_arbPH));
		}
		break;
	case PF_Arbitrary_FLAT_SIZE_FUNC:
		*(extra->u.flat_size_func_params.flat_data_sizePLu) = sizeof(RampArb);
		break;
	case PF_Arbitrary_FLATTEN_FUNC:
		if (extra->u.flatten_func_params.buf_sizeLu == sizeof(RampArb)) {
			srcP = (void*)PF_LOCK_HANDLE(extra->u.flatten_func_params.arbH);
			dstP = extra->u.flatten_func_params.flat_dataPV;
			if (srcP) memcpy(dstP, srcP, sizeof(RampArb));
			PF_UNLOCK_HANDLE(extra->u.flatten_func_params.arbH);
		}
		break;
	case PF_Arbitrary_UNFLATTEN_FUNC:
		if (extra->u.unflatten_func_params.buf_sizeLu == sizeof(RampArb)) {
			PF_Handle h = PF_NEW_HANDLE(sizeof(RampArb));
			dstP = (void*)PF_LOCK_HANDLE(h);
			srcP = (void*)extra->u.unflatten_func_params.flat_dataPV;
			if (srcP && dstP) {
				memcpy(dstP, srcP, sizeof(RampArb));
				RampArb_Sanitize(reinterpret_cast<RampArb*>(dstP));
			}
			*(extra->u.unflatten_func_params.arbPH) = h;
			PF_UNLOCK_HANDLE(h);
		}
		break;
	case PF_Arbitrary_INTERP_FUNC:
		if (extra->u.interp_func_params.refconPV == ARB_REFCON) {
			ERR(CreateDefaultArb(in_data, out_data, extra->u.interp_func_params.interpPH));
			ERR(Arb_Interpolate(in_data, out_data, extra->u.interp_func_params.tF,
								&extra->u.interp_func_params.left_arbH,
								&extra->u.interp_func_params.right_arbH,
								extra->u.interp_func_params.interpPH));
		}
		break;
	case PF_Arbitrary_COMPARE_FUNC:
		ERR(Arb_Compare(in_data, out_data, &extra->u.compare_func_params.a_arbH,
						&extra->u.compare_func_params.b_arbH, extra->u.compare_func_params.compareP));
		break;
	case PF_Arbitrary_PRINT_SIZE_FUNC:
		if (extra->u.print_size_func_params.refconPV == ARB_REFCON)
			*extra->u.print_size_func_params.print_sizePLu = RAMP_ARB_MAX_PRINT_SIZE;
		else err = PF_Err_UNRECOGNIZED_PARAM_TYPE;
		break;
	case PF_Arbitrary_PRINT_FUNC:
		if (extra->u.print_func_params.refconPV == ARB_REFCON) {
			ERR(Arb_Print(in_data, out_data, extra->u.print_func_params.print_flags,
						  extra->u.print_func_params.arbH, extra->u.print_func_params.print_sizeLu,
						  extra->u.print_func_params.print_bufferPC));
		} else err = PF_Err_UNRECOGNIZED_PARAM_TYPE;
		break;
	case PF_Arbitrary_SCAN_FUNC:
		if (extra->u.scan_func_params.refconPV == ARB_REFCON) {
			ERR(Arb_Scan(in_data, out_data, extra->u.scan_func_params.refconPV,
						 extra->u.scan_func_params.bufPC, extra->u.scan_func_params.bytes_to_scanLu,
						 extra->u.scan_func_params.arbPH));
		} else err = PF_Err_UNRECOGNIZED_PARAM_TYPE;
		break;
	}
	return err;
}

// ---------------------------------------------------------------------------
extern "C" DllExport
PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
	SPBasicSuite *inSPBasicSuitePtr, const char *inHostName, const char *inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;
	result = PF_REGISTER_EFFECT_EXT2(
		inPtr, inPluginDataCallBackPtr,
		"Perceptual Ramp", "KPX Perceptual Ramp", "KPX",
		AE_RESERVED_INFO, "EffectMain", "https://example.com");
	return result;
}

PF_Err
EffectMain(PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data,
		   PF_ParamDef *params[], PF_LayerDef *output, void *extra)
{
	PF_Err err = PF_Err_NONE;
	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:			err = About(in_data, out_data, params, output); break;
			case PF_Cmd_GLOBAL_SETUP:	err = GlobalSetup(in_data, out_data, params, output); break;
			case PF_Cmd_PARAMS_SETUP:	err = ParamsSetup(in_data, out_data, params, output); break;
			case PF_Cmd_RENDER:			err = Render(in_data, out_data, params, output); break;
			case PF_Cmd_EVENT:			err = HandleEvent(in_data, out_data, params, output, reinterpret_cast<PF_EventExtra*>(extra)); break;
			case PF_Cmd_ARBITRARY_CALLBACK: err = HandleArbitrary(in_data, out_data, params, output, reinterpret_cast<PF_ArbParamsExtra*>(extra)); break;
			case PF_Cmd_SMART_PRE_RENDER:	err = PreRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra)); break;
			case PF_Cmd_SMART_RENDER:		err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra)); break;
		}
	} catch (PF_Err &thrown_err) {
		err = thrown_err;
	}
	return err;
}
