/*
	Ramp.h — AE Gradient/Ramp plugin (KPX)

	Multi-stop perceptual gradient. Stops live in PF_Param_ARBITRARY_DATA with a
	custom Drawbot UI (preview bar + draggable markers). Interpolation space and
	hue path are separate popups. Render: classic (8/16) + Smart FX (8/16/32f),
	driven by the SDK-free color engine in color_math.h.
*/
#pragma once
#ifndef RAMP_H
#define RAMP_H

#define PF_DEEP_COLOR_AWARE 1

#include "AEConfig.h"

#ifdef AE_OS_WIN
	typedef unsigned short PixelType;
	#include <Windows.h>
	#include <stdlib.h>
	#include <string.h>
#endif

#include "A.h"
#include "AE_Effect.h"
#include "AE_EffectSuites.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AEGP_SuiteHandler.h"
#include "AE_EffectUI.h"
#include "AE_Macros.h"
#include "AE_AdvEffectSuites.h"
#include "AEFX_ArbParseHelper.h"
#include "AEFX_SuiteHelper.h"
#include "Param_Utils.h"
#include "AE_GeneralPlug.h"
#include "entry.h"
#include "Smart_Utils.h"

#include "color_math.h"
#include "RampShapes.h"

/* Versioning */
#define	MAJOR_VERSION	1
#define	MINOR_VERSION	0
#define	BUG_VERSION		0
#define	STAGE_VERSION	PF_Stage_DEVELOP
#define	BUILD_VERSION	1

#define	NAME			"Perceptual Ramp"
#define	DESCRIPTION		"\rPerceptual multi-stop color gradients (OKLab/OKLCh).\rKPX."

/* ----- Parameters ----- */
enum {
	RAMP_INPUT = 0,
	RAMP_GRADIENT,		// arbitrary data + custom UI
	RAMP_SHAPE,			// popup: gradient shape
	RAMP_START,			// point: start / center
	RAMP_END,			// point: end / radius handle
	RAMP_REPEAT,		// popup: tiling
	RAMP_OFFSET,		// float: phase offset [-1,1]
	RAMP_REVERSE,		// checkbox
	RAMP_SIDES,			// slider: polygon sides / star points
	RAMP_STAR_RATIO,	// float: star inner ratio [0,1]
	RAMP_TWIST,			// float: spiral turns
	RAMP_SPACE,			// popup: interpolation space
	RAMP_HUE,			// popup: hue path
	RAMP_MAP,			// layer: grayscale source (From Map shape)
	RAMP_IN_BLACK,		// float: map input black level [0,1]
	RAMP_IN_WHITE,		// float: map input white level [0,1]
	RAMP_BLEND,			// popup: blend mode over input
	RAMP_OPACITY,		// float: effect opacity [0,1]
	RAMP_NUM_PARAMS
};

enum {
	GRADIENT_DISK_ID = 1,
	SHAPE_DISK_ID,
	START_DISK_ID,
	END_DISK_ID,
	REPEAT_DISK_ID,
	OFFSET_DISK_ID,
	REVERSE_DISK_ID,
	SPACE_DISK_ID,
	HUE_DISK_ID,
	MAP_DISK_ID,
	IN_BLACK_DISK_ID,
	IN_WHITE_DISK_ID,
	SIDES_DISK_ID,
	STAR_RATIO_DISK_ID,
	TWIST_DISK_ID,
	BLEND_DISK_ID,
	OPACITY_DISK_ID
};

// Popup order MUST match cm::Blend.
#define RAMP_BLEND_CHOICES	"Normal|Multiply|Screen|Overlay|Darken|Lighten|Add|Subtract|Difference|Hard Light|Soft Light"
#define RAMP_BLEND_COUNT	11
#define RAMP_BLEND_DFLT		1	// Normal

// Popup order MUST match shapes::Shape
// (Linear,Radial,Angular,Reflected,Diamond,Ellipse,Star,Polygon,Spiral,Square,Map).
#define RAMP_SHAPE_CHOICES	"Linear|Radial|Angular|Reflected|Diamond|Ellipse|Star|Polygon|Spiral|Square|From Map"
#define RAMP_SHAPE_COUNT	11
#define RAMP_SHAPE_DFLT		1	// Linear

// Popup order MUST match shapes::Repeat (None,Repeat,Mirror).
#define RAMP_REPEAT_CHOICES	"None|Repeat|Mirror"
#define RAMP_REPEAT_COUNT	3
#define RAMP_REPEAT_DFLT	1	// None

// Popup order MUST match cm::Space (sRGB,Linear,OKLab,OKLCh,Lab,LCh,HSL).
#define RAMP_SPACE_CHOICES	"sRGB|Linear RGB|OKLab|OKLCh|CIELAB|CIE LCh|HSL"
#define RAMP_SPACE_COUNT	7
#define RAMP_SPACE_DFLT		3	// OKLab (1-based)

#define RAMP_HUE_CHOICES	"Shorter|Longer|Increasing|Decreasing"
#define RAMP_HUE_COUNT		4
#define RAMP_HUE_DFLT		1

/* ----- Custom UI geometry ----- */
#define RAMP_UI_WIDTH		260
#define RAMP_UI_HEIGHT		42
#define RAMP_UI_ID			2400
#define RAMP_UI_PAD			8	// horizontal padding inside the control
#define RAMP_UI_BAR_TOP		4	// from top of control frame
#define RAMP_UI_BAR_H		20
#define RAMP_UI_MARK_H		12	// marker strip height below bar
#define RAMP_UI_HIT_PX		6	// marker hit-test half-width

/* ----- Arbitrary data (multi-stop gradient) ----- */
#define RAMP_MAX_STOPS		32
#define RAMP_ARB_VERSION	1
#define RAMP_ARB_MAGIC		0x4B505231		/* 'KPR1' */
#define ARB_REFCON			((void*)0x4B505258DEADBEEFULL)
#define RAMP_ARB_MAX_PRINT_SIZE	2048

// One POD stop. Colors are gamma-sRGB display values in [0,1].
typedef struct {
	PF_FpShort	position;		// [0,1]
	PF_FpShort	red, green, blue, alpha;
	A_u_char	easing;			// cm::Easing
	A_u_char	pad0, pad1, pad2;
	PF_FpShort	midpoint;		// [0,1], 0.5 = neutral
} RampStop;

// Fixed-size POD blob -> flatten/unflatten is a plain memcpy.
typedef struct {
	A_u_long	magic;
	A_u_char	version;
	A_u_char	num_stops;		// 2..RAMP_MAX_STOPS
	A_u_char	pad0, pad1;
	RampStop	stops[RAMP_MAX_STOPS];
} RampArb;

// Refcon handed to the pixel iterators (prepared, sorted stops + geometry).
typedef struct {
	cm::Stop			stops[RAMP_MAX_STOPS];
	int					num_stops;
	int					space;	// cm::Space
	int					hue;	// cm::HuePath
	shapes::ShapeParams	shape;	// pixel -> t geometry

	int					useMap;		// From Map mode
	PF_EffectWorld		*mapWorld;	// grayscale source (NULL if none); same bit-depth as output
	float				inputBlack;	// luma remap low  [0,1]
	float				inputWhite;	// luma remap high [0,1]

	int					blend;		// cm::Blend
	float				opacity;	// [0,1]
} RampRenderInfo;

/* ----- Entry ----- */
extern "C" {
	DllExport PF_Err EffectMain(
		PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data,
		PF_ParamDef *params[], PF_LayerDef *output, void *extra);
}

/* ----- ARB callbacks (GradientData.cpp) ----- */
PF_Err CreateDefaultArb(PF_InData*, PF_OutData*, PF_ArbitraryH *dephault);
PF_Err Arb_Copy(PF_InData*, PF_OutData*, const PF_ArbitraryH *src, PF_ArbitraryH *dst);
PF_Err Arb_Interpolate(PF_InData*, PF_OutData*, double tF,
					   const PF_ArbitraryH *l, const PF_ArbitraryH *r, PF_ArbitraryH *res);
PF_Err Arb_Compare(PF_InData*, PF_OutData*, const PF_ArbitraryH *a,
				   const PF_ArbitraryH *b, PF_ArbCompareResult *result);
PF_Err Arb_Print(PF_InData*, PF_OutData*, PF_ArbPrintFlags, PF_ArbitraryH,
				 A_u_long print_size, A_char *print_buffer);
PF_Err Arb_Scan(PF_InData*, PF_OutData*, void *refcon, const char *buf,
				unsigned long bytes, PF_ArbitraryH *arbPH);

// Normalize/sort stops in place (clamp count, sort by position).
void RampArb_Sanitize(RampArb *arbP);

// Convert a sanitized copy of the ARB blob into cm::Stop[]; returns count. (Ramp.cpp)
int RampArb_ToStops(const RampArb *arbP, cm::Stop out[RAMP_MAX_STOPS]);

/* ----- Custom UI (GradientUI.cpp) ----- */
PF_Err DrawEvent(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*, PF_EventExtra*);
PF_Err DoClick(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*, PF_EventExtra*);
PF_Err DragEvent(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*, PF_EventExtra*);
PF_Err ChangeCursor(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*, PF_EventExtra*);

#endif // RAMP_H
