/*
	GradientData.cpp — arbitrary-data handlers for the multi-stop gradient.
	The blob (RampArb) is fixed-size POD, so flatten/unflatten are plain memcpy
	(handled inline in Ramp.cpp's HandleArbitrary). Here: create/copy/interp/
	compare/print/scan + sanitize.
*/
#include "Ramp.h"

// ---------------------------------------------------------------------------
static void SetStop(RampStop *s, float pos, float r, float g, float b, float a)
{
	s->position = pos;
	s->red = r; s->green = g; s->blue = b; s->alpha = a;
	s->easing = (A_u_char)cm::Easing::Linear;
	s->pad0 = s->pad1 = s->pad2 = 0;
	s->midpoint = 0.5f;
}

PF_Err
CreateDefaultArb(PF_InData *in_data, PF_OutData *out_data, PF_ArbitraryH *dephault)
{
	PF_Err				err = PF_Err_NONE;
	AEGP_SuiteHandler	suites(in_data->pica_basicP);

	PF_Handle arbH = suites.HandleSuite1()->host_new_handle(sizeof(RampArb));
	if (!arbH) return PF_Err_OUT_OF_MEMORY;

	RampArb *arbP = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(arbH));
	if (!arbP) {
		err = PF_Err_OUT_OF_MEMORY;
	} else {
		AEFX_CLR_STRUCT(*arbP);
		arbP->magic     = RAMP_ARB_MAGIC;
		arbP->version   = RAMP_ARB_VERSION;
		arbP->num_stops = 2;
		SetStop(&arbP->stops[0], 0.0f, 0.0f,  90.0f/255.0f, 1.0f, 1.0f);	// blue
		SetStop(&arbP->stops[1], 1.0f, 1.0f, 210.0f/255.0f, 0.0f, 1.0f);	// gold
		*dephault = arbH;
		suites.HandleSuite1()->host_unlock_handle(arbH);
	}
	return err;
}

// Clamp count, clamp ranges, sort by position (insertion sort, n is tiny).
void
RampArb_Sanitize(RampArb *arbP)
{
	if (!arbP) return;
	if (arbP->num_stops < 2) arbP->num_stops = 2;
	if (arbP->num_stops > RAMP_MAX_STOPS) arbP->num_stops = RAMP_MAX_STOPS;

	for (int i = 0; i < arbP->num_stops; ++i) {
		RampStop *s = &arbP->stops[i];
		s->position = cm::clamp01(s->position);
		s->red   = cm::clamp01(s->red);
		s->green = cm::clamp01(s->green);
		s->blue  = cm::clamp01(s->blue);
		s->alpha = cm::clamp01(s->alpha);
		s->midpoint = cm::clampf(s->midpoint, 0.001f, 0.999f);
		if (s->easing > (A_u_char)cm::Easing::Ease) s->easing = 0;
	}
	for (int i = 1; i < arbP->num_stops; ++i) {
		RampStop key = arbP->stops[i];
		int j = i - 1;
		while (j >= 0 && arbP->stops[j].position > key.position) {
			arbP->stops[j + 1] = arbP->stops[j];
			--j;
		}
		arbP->stops[j + 1] = key;
	}
}

PF_Err
Arb_Copy(PF_InData *in_data, PF_OutData *out_data,
		 const PF_ArbitraryH *srcP, PF_ArbitraryH *dstP)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	PF_Handle srcH = *srcP, dstH = *dstP;
	if (srcH && dstH) {
		RampArb *s = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(srcH));
		RampArb *d = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(dstH));
		if (s && d) {
			memcpy(d, s, sizeof(RampArb));
		} else {
			err = PF_Err_OUT_OF_MEMORY;
		}
		if (s) suites.HandleSuite1()->host_unlock_handle(srcH);
		if (d) suites.HandleSuite1()->host_unlock_handle(dstH);
	}
	return err;
}

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

PF_Err
Arb_Interpolate(PF_InData *in_data, PF_OutData *out_data, double tF,
				const PF_ArbitraryH *lH, const PF_ArbitraryH *rH, PF_ArbitraryH *resH)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	RampArb *L = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(*lH));
	RampArb *R = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(*rH));
	RampArb *O = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(*resH));

	if (L && R && O) {
		float t = (float)tF;
		if (L->num_stops == R->num_stops) {
			O->magic = RAMP_ARB_MAGIC;
			O->version = RAMP_ARB_VERSION;
			O->num_stops = L->num_stops;
			for (int i = 0; i < L->num_stops; ++i) {
				RampStop *o = &O->stops[i];
				const RampStop *a = &L->stops[i], *b = &R->stops[i];
				o->position = lerpf(a->position, b->position, t);
				o->red   = lerpf(a->red,   b->red,   t);
				o->green = lerpf(a->green, b->green, t);
				o->blue  = lerpf(a->blue,  b->blue,  t);
				o->alpha = lerpf(a->alpha, b->alpha, t);
				o->midpoint = lerpf(a->midpoint, b->midpoint, t);
				o->easing = a->easing;
				o->pad0 = o->pad1 = o->pad2 = 0;
			}
		} else {
			memcpy(O, (t < 0.5f) ? L : R, sizeof(RampArb));
		}
		RampArb_Sanitize(O);
	} else {
		err = PF_Err_OUT_OF_MEMORY;
	}
	if (L) suites.HandleSuite1()->host_unlock_handle(*lH);
	if (R) suites.HandleSuite1()->host_unlock_handle(*rH);
	if (O) suites.HandleSuite1()->host_unlock_handle(*resH);
	return err;
}

PF_Err
Arb_Compare(PF_InData *in_data, PF_OutData *out_data,
			const PF_ArbitraryH *aH, const PF_ArbitraryH *bH, PF_ArbCompareResult *resultP)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	*resultP = PF_ArbCompare_EQUAL;

	RampArb *A = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(*aH));
	RampArb *B = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(*bH));
	if (A && B) {
		PF_Boolean equal = (A->num_stops == B->num_stops);
		double sa = 0, sb = 0;
		int na = A->num_stops, nb = B->num_stops;
		for (int i = 0; i < na; ++i)
			sa += A->stops[i].position + A->stops[i].red + A->stops[i].green +
				  A->stops[i].blue + A->stops[i].alpha + A->stops[i].midpoint;
		for (int i = 0; i < nb; ++i)
			sb += B->stops[i].position + B->stops[i].red + B->stops[i].green +
				  B->stops[i].blue + B->stops[i].alpha + B->stops[i].midpoint;
		if (equal) {
			for (int i = 0; i < na && equal; ++i)
				if (memcmp(&A->stops[i], &B->stops[i], sizeof(RampStop)) != 0) equal = FALSE;
		}
		if (equal)        *resultP = PF_ArbCompare_EQUAL;
		else if (sa > sb) *resultP = PF_ArbCompare_MORE;
		else if (sa < sb) *resultP = PF_ArbCompare_LESS;
		else              *resultP = PF_ArbCompare_NOT_EQUAL;
	}
	if (A) suites.HandleSuite1()->host_unlock_handle(*aH);
	if (B) suites.HandleSuite1()->host_unlock_handle(*bH);
	return err;
}

PF_Err
Arb_Print(PF_InData *in_data, PF_OutData *out_data, PF_ArbPrintFlags print_flags,
		  PF_ArbitraryH arbH, A_u_long print_size, A_char *print_buffer)
{
	PF_Err err = PF_Err_NONE;
	if (!arbH || !print_buffer || !print_size) return err;

	AEGP_SuiteHandler suites(in_data->pica_basicP);
	RampArb *arbP = reinterpret_cast<RampArb*>(suites.HandleSuite1()->host_lock_handle(arbH));
	if (arbP && !print_flags) {
		A_char line[128];
		print_buffer[0] = 0x00;
		PF_SPRINTF(line, "Ramp gradient: %d stops", (int)arbP->num_stops);
#ifdef AE_OS_WIN
		strncat_s(print_buffer, print_size, line, _TRUNCATE);
#else
		strncat(print_buffer, line, print_size - 1);
#endif
		for (int i = 0; i < arbP->num_stops && i < 16; ++i) {
			const RampStop *s = &arbP->stops[i];
			PF_SPRINTF(line, "\t[%d] @%.3f rgba(%.3f,%.3f,%.3f,%.3f)",
					   i, s->position, s->red, s->green, s->blue, s->alpha);
#ifdef AE_OS_WIN
			strncat_s(print_buffer, print_size, line, _TRUNCATE);
#else
			strncat(print_buffer, line, print_size - strlen(print_buffer) - 1);
#endif
		}
	} else if (print_buffer && print_size) {
		print_buffer[0] = 0x00;
	}
	if (arbP) suites.HandleSuite1()->host_unlock_handle(arbH);
	return err;
}

PF_Err
Arb_Scan(PF_InData *in_data, PF_OutData *out_data, void *refcon,
		 const char *buf, unsigned long bytes, PF_ArbitraryH *arbPH)
{
	// Text round-trip parsing is not supported; create a default instead.
	return CreateDefaultArb(in_data, out_data, arbPH);
}
