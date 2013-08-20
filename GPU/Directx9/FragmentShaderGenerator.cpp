// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "FragmentShaderGenerator.h"
#include "../ge_constants.h"
#include "../GPUState.h"
#include <cstdio>

#define WRITE p+=sprintf

// #define DEBUG_SHADER

// GL_NV_shader_framebuffer_fetch looks interesting....

static bool IsAlphaTestTriviallyTrue() {
	GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
	int alphaTestRef = gstate.getAlphaTestRef();
	int alphaTestMask = gstate.getAlphaTestMask();
	
	switch (alphaTestFunc) {
	case GE_COMP_ALWAYS:
		return true;
	case GE_COMP_GEQUAL:
		if (alphaTestRef == 0)
			return true;

	// This breaks the trees in MotoGP, for example.
	// case GE_COMP_GREATER:
	//if (alphaTestRef == 0 && (gstate.alphaBlendEnable & 1) && gstate.getBlendFuncA() == GE_SRCBLEND_SRCALPHA && gstate.getBlendFuncB() == GE_SRCBLEND_INVSRCALPHA)
	//	return true;

	case GE_COMP_LEQUAL:
		if (alphaTestRef == 255)
			return true;
	default:
		return false;
	}
}

static bool IsColorTestTriviallyTrue() {
	GEComparison colorTestFunc = gstate.getColorTestFunction();
	switch (colorTestFunc) {
	case GE_COMP_ALWAYS:
		return true;
	default:
		return false;
	}
}

static bool CanDoubleSrcBlendMode() {
	if (!gstate.isAlphaBlendEnabled()) {
		return false;
	}

	int funcA = gstate.getBlendFuncA();
	int funcB = gstate.getBlendFuncB();
	if (funcA != GE_SRCBLEND_DOUBLESRCALPHA) {
		funcB = funcA;
		funcA = gstate.getBlendFuncB();
	}
	if (funcA != GE_SRCBLEND_DOUBLESRCALPHA) {
		return false;
	}

	// One side should be doubled.  Let's check the other side.
	// LittleBigPlanet, for example, uses 2.0 * src, 1.0 - src, which can't double.
	switch (funcB) {
	case GE_DSTBLEND_SRCALPHA:
	case GE_DSTBLEND_INVSRCALPHA:
		return false;

	default:
		return true;
	}
}


// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderID(FragmentShaderID *id) {
	memset(&id->d[0], 0, sizeof(id->d));
	if (gstate.isModeClear()) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id->d[0] = 1;
	} else {
		bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();
		bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough();
		bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue();
		bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue();
		bool enableColorDoubling = gstate.isColorDoublingEnabled();
		// This isn't really correct, but it's a hack to get doubled blend modes to work more correctly.
		bool enableAlphaDoubling = CanDoubleSrcBlendMode();
		bool doTextureProjection = gstate.getUVGenMode() == 1;
		bool doTextureAlpha = (gstate.texfunc & 0x100) != 0;

		// All texfuncs except replace are the same for RGB as for RGBA with full alpha.
		if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
			doTextureAlpha = false;

		// id->d[0] |= (gstate.isModeClear() & 1);
		if (gstate.isTextureMapEnabled()) {
			id->d[0] |= 1 << 1;
			id->d[0] |= gstate.getTextureFunction() << 2;
			id->d[0] |= (doTextureAlpha & 1) << 5; // rgb or rgba
		}
		id->d[0] |= (lmode & 1) << 7;
		id->d[0] |= gstate.isAlphaTestEnabled() << 8;
		if (enableAlphaTest)
			id->d[0] |= gstate.getAlphaTestFunction() << 9;
		id->d[0] |= gstate.isColorTestEnabled() << 12;
		if (enableColorTest)
			id->d[0] |= gstate.getColorTestFunction() << 13;	 // color test func
		id->d[0] |= (enableFog & 1) << 15;
		id->d[0] |= (doTextureProjection & 1) << 16;
		id->d[0] |= (enableColorDoubling & 1) << 17;
		id->d[0] |= (enableAlphaDoubling & 1) << 18;
	}
}

// Missing: Z depth range
// Also, logic ops etc, of course. Urgh.
#if 0
void GenerateFragmentShader(char *buffer) {
	//--------------------------------------------------------------------------------------
	// Pixel shader
	//--------------------------------------------------------------------------------------
	const char * pscode =
		" sampler s: register(s0);					   "
		" struct PS_IN                                 "
		" {                                            "
		"		float3 Uv   : TEXCOORD0;              "
		"		float4 C1    : COLOR0;                 "  // Vertex color
		"		float4 C2    : COLOR1;                 "  // Vertex color                     
		" };                                           " 
		"                                              "
		" float4 main( PS_IN In ) : COLOR              "
		" {                                            "
		//"   float4 c = In.C1;							"
		"	float4 c = tex2D(s, In.Uv.xy);			"
		"   return c;								   "
		" }                                            ";

	strcpy(buffer, pscode);
}
#else
void GenerateFragmentShader(char *buffer) {
	char *p = buffer;

	int lmode = (gstate.lmode & 1) && gstate.isLightingEnabled();
	int doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !gstate.isModeClear();
	bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue() && !gstate.isModeClear();
	bool enableColorDoubling = gstate.isColorDoublingEnabled();
	// This isn't really correct, but it's a hack to get doubled blend modes to work more correctly.
	bool enableAlphaDoubling = CanDoubleSrcBlendMode();
	bool doTextureProjection = gstate.getUVGenMode() == 1;
	bool doTextureAlpha = (gstate.texfunc & 0x100) != 0;

	if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
		doTextureAlpha = false;

	if (doTexture)
		WRITE(p, "sampler tex: register(s0);\n");

	if (enableAlphaTest || enableColorTest) {
		WRITE(p, "float4 u_alphacolorref;\n");
		WRITE(p, "float3 u_colormask;\n");
	}
	if (gstate.isTextureMapEnabled()) 
		WRITE(p, "float3 u_texenv;\n");
	if (enableFog) {
		WRITE(p, "float3 u_fogcolor;\n");
	}
	

	if (enableAlphaTest) {
		WRITE(p, "float roundAndScaleTo255f(float x) { return floor(x * 255.0f + 0.5f); }\n");
	}
	if (enableColorTest) {
		WRITE(p, "float3 roundAndScaleTo255v(float3 x) { return floor(x * 255.0f + 0.5f); }\n");
	}

	WRITE(p, " struct PS_IN                               ");
	WRITE(p, " {                                          ");
	WRITE(p, "		float4 v_texcoord: TEXCOORD0;         ");
	WRITE(p, "		float4 v_color0: COLOR0;              "); 
	WRITE(p, "		float3 v_color1: COLOR1;              ");    
	if (enableFog) {
		WRITE(p, "float v_fogdepth:FOG;\n");
	}
	WRITE(p, " };                                         "); 
	WRITE(p, "                                            ");
	WRITE(p, " float4 main( PS_IN In ) : COLOR            ");
	WRITE(p, " {									      ");

	if (gstate.isModeClear()) {
		// Clear mode does not allow any fancy shading.
		WRITE(p, "  return In.v_color0;\n");
	} else {
		const char *secondary = "";
		// Secondary color for specular on top of texture
		if (lmode) {
			WRITE(p, "  float4 s = float4(In.v_color1, 0);\n");
			secondary = " + s";
		} else {
			secondary = "";
		}

		if (gstate.isTextureMapEnabled()) {
			if (doTextureProjection) {
				WRITE(p, "  float4 t = tex2Dproj(tex, In.v_texcoord);\n");
			} else {
				WRITE(p, "  float4 t = tex2D(tex, In.v_texcoord.xy);\n");
			}
			WRITE(p, "  float4 p = In.v_color0;\n");

			if (doTextureAlpha) { // texfmt == RGBA
				switch (gstate.getTextureFunction()) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  float4 v = p * t%s;\n", secondary); break;
				case GE_TEXFUNC_DECAL:
					WRITE(p, "  float4 v = float4(lerp(p.rgb, t.rgb, t.a), p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_BLEND:
					WRITE(p, "  float4 v = float4(lerp(p.rgb, u_texenv.rgb, t.rgb), p.a * t.a)%s;\n", secondary); break;
				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  float4 v = t%s;\n", secondary); break;
				case GE_TEXFUNC_ADD:
					WRITE(p, "  float4 v = float4(p.rgb + t.rgb, p.a * t.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  float4 v = p;\n"); break;
				}

			} else {	// texfmt == RGB
				switch (gstate.getTextureFunction()) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  float4 v = float4(t.rgb * p.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_DECAL:
					WRITE(p, "  float4 v = float4(t.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_BLEND:
					WRITE(p, "  float4 v = float4(lerp(p.rgb, u_texenv.rgb, t.rgb), p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  float4 v = float4(t.rgb, p.a)%s;\n", secondary); break;
				case GE_TEXFUNC_ADD:
					WRITE(p, "  float4 v = float4(p.rgb + t.rgb, p.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  float4 v = p;\n"); break;
				}
			}
		} else {
			// No texture mapping
			WRITE(p, "  float4 v = In.v_color0 %s;\n", secondary);
			// HACK ONLY DISPLAY TEXTuRE !!!
			//WRITE(p, "  clip(-1);\n");
		}

		if (enableAlphaTest) {
			GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
			const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };	// never/always don't make sense
			if (alphaTestFuncs[alphaTestFunc][0] != '#') {
				// WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
				//WRITE(p, "clip((roundAndScaleTo255f(v.rgb) %s u_alphacolorref.a)? -1:1);\n", alphaTestFuncs[alphaTestFunc]);
				WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) clip(-1);\n", alphaTestFuncs[alphaTestFunc]);
			}
		}

		// TODO: Before or after the color test?
		if (enableColorDoubling && enableAlphaDoubling) {
			WRITE(p, "  v = v * 2.0;\n");
		} else if (enableColorDoubling) {
			WRITE(p, "  v.rgb = v.rgb * 2.0;\n");
		} else if (enableAlphaDoubling) {
			WRITE(p, "  v.a = v.a * 2.0;\n");
		}
		
		if (enableColorTest) {
			GEComparison colorTestFunc = gstate.getColorTestFunction();
			const char *colorTestFuncs[] = { "#", "#", " != ", " == " };	// never/always don't make sense
			u32 colorTestMask = gstate.getColorTestMask();
			if (colorTestFuncs[colorTestFunc][0] != '#') {
				//WRITE(p, "clip((roundAndScaleTo255v(v.rgb) %s u_alphacolorref.rgb)? -1:1);\n", colorTestFuncs[colorTestFunc]);
				//WRITE(p, "if (roundAndScaleTo255v(v.rgb) %s u_alphacolorref.rgb)  clip(-1);\n", colorTestFuncs[colorTestFunc]);

				// cleanup ?
				const char * test = colorTestFuncs[colorTestFunc];
				WRITE(p, "float3 colortest = roundAndScaleTo255v(v.rgb);\n");
				WRITE(p, "if ((colortest.r %s u_alphacolorref.r) && (colortest.g %s u_alphacolorref.g) && (colortest.b %s u_alphacolorref.b ))  clip(-1);\n", test, test, test);

			}
		}

		if (enableFog) {
			WRITE(p, "  float fogCoef = clamp(In.v_fogdepth, 0.0, 1.0);\n");
			WRITE(p, "  return lerp(float4(u_fogcolor, v.a), v, fogCoef);\n");
			// WRITE(p, "  v.x = v_depth;\n");
		} else {
			WRITE(p, "  return v;\n");
		}
	}
	WRITE(p, "}\n");
}
#endif
