// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2020 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_md2.c
/// \brief MD2 Handling
///	Inspired from md2.c by Mete Ciragan (mete@swissquake.ch)

#ifdef __GNUC__
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../d_main.h"
#include "../doomdef.h"
#include "../doomstat.h"
#include "../fastcmp.h"

#ifdef HWRENDER
#include "hw_drv.h"
#include "hw_light.h"
#include "hw_md2.h"
#include "../d_main.h"
#include "../r_bsp.h"
#include "../r_main.h"
#include "../m_misc.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../r_things.h"
#include "../r_draw.h"
#include "../p_tick.h"
#include "../r_model.h"
#include "../r_modeltextures.h"

#include "hw_main.h"
#include "../v_video.h"

// Define for getting accurate color brightness readings according to how the human eye sees them.
// https://en.wikipedia.org/wiki/Relative_luminance
// 0.2126 to red
// 0.7152 to green
// 0.0722 to blue
#define SETBRIGHTNESS(brightness,r,g,b) \
	brightness = (UINT8)(((1063*(UINT16)(r))/5000) + ((3576*(UINT16)(g))/5000) + ((361*(UINT16)(b))/5000))

static void HWR_CreateBlendedTexture(GLPatch_t *gpatch, GLPatch_t *blendgpatch, GLMipmap_t *grmip, INT32 skinnum, skincolors_t color)
{
	UINT16 w = gpatch->width, h = gpatch->height;
	UINT32 size = w*h;
	RGBA_t *image, *blendimage, *cur, blendcolor;

	// vanilla port
	UINT8 translation[16];
	memset(translation, 0, sizeof(translation));

	if (grmip->width == 0)
	{
		grmip->width = gpatch->width;
		grmip->height = gpatch->height;

		// no wrap around, no chroma key
		grmip->flags = 0;
		// setup the texture info
		grmip->grInfo.format = GR_RGBA;
	}

	if (grmip->grInfo.data)
	{
		Z_Free(grmip->grInfo.data);
		grmip->grInfo.data = NULL;
	}

	cur = Z_Malloc(size*4, PU_HWRMODELTEXTURE, &grmip->grInfo.data);
	memset(cur, 0x00, size*4);

	image = gpatch->mipmap->grInfo.data;
	blendimage = blendgpatch->mipmap->grInfo.data;
	blendcolor = V_GetColor(0); // initialize

	if (color != SKINCOLOR_NONE)
		memcpy(&translation, &Color_Index[color - 1], 16);

	while (size--)
	{
		if (skinnum == TC_BOSS)
		{
			// Turn everything below a certain threshold white
			if ((image->s.red == image->s.green) && (image->s.green == image->s.blue) && image->s.blue <= 82)
			{
				// Lactozilla: Invert the colors
				cur->s.red = cur->s.green = cur->s.blue = (255 - image->s.blue);
			}
			else
			{
				cur->s.red = image->s.red;
				cur->s.green = image->s.green;
				cur->s.blue = image->s.blue;
			}

			cur->s.alpha = image->s.alpha;
		}
		else if (skinnum == TC_METALSONIC)
		{
			// Turn everything below a certain blue threshold white
			if (image->s.red == 0 && image->s.green == 0 && image->s.blue <= 82)
			{
				cur->s.red = cur->s.green = cur->s.blue = 255;
			}
			else
			{
				cur->s.red = image->s.red;
				cur->s.green = image->s.green;
				cur->s.blue = image->s.blue;
			}

			cur->s.alpha = image->s.alpha;
		}
		else if (skinnum == TC_DASHMODE)
		{
			if (image->s.alpha == 0 && blendimage->s.alpha == 0)
			{
				// Don't bother with blending the pixel if the alpha of the blend pixel is 0
				cur->rgba = image->rgba;
			}
			else
			{
				UINT8 ialpha = 255 - blendimage->s.alpha, balpha = blendimage->s.alpha;
				RGBA_t icolor = *image, bcolor;

				memset(&bcolor, 0x00, sizeof(RGBA_t));

				if (blendimage->s.alpha)
				{
					bcolor.s.blue = 0;
					bcolor.s.red = 255;
					bcolor.s.green = (blendimage->s.red + blendimage->s.green + blendimage->s.blue) / 3;
				}
				if (image->s.alpha && image->s.red > image->s.green << 1) // this is pretty arbitrary, but it works well for Metal Sonic
				{
					icolor.s.red = image->s.blue;
					icolor.s.blue = image->s.red;
				}
				cur->s.red = (ialpha * icolor.s.red + balpha * bcolor.s.red)/255;
				cur->s.green = (ialpha * icolor.s.green + balpha * bcolor.s.green)/255;
				cur->s.blue = (ialpha * icolor.s.blue + balpha * bcolor.s.blue)/255;
				cur->s.alpha = image->s.alpha;
			}
		}
		else if (skinnum == TC_ALLWHITE)
		{
			// Turn everything white
			cur->s.red = cur->s.green = cur->s.blue = 255;
			cur->s.alpha = image->s.alpha;
		}
		else
		{
			UINT16 brightness;

			// Don't bother with blending the pixel if the alpha of the blend pixel is 0
			if (skinnum == TC_RAINBOW)
			{
				if (image->s.alpha == 0 && blendimage->s.alpha == 0)
				{
					cur->rgba = image->rgba;
					cur++; image++; blendimage++;
					continue;
				}
				else
				{
					UINT16 imagebright, blendbright;
					SETBRIGHTNESS(imagebright,image->s.red,image->s.green,image->s.blue);
					SETBRIGHTNESS(blendbright,blendimage->s.red,blendimage->s.green,blendimage->s.blue);
					// slightly dumb average between the blend image color and base image colour, usually one or the other will be fully opaque anyway
					brightness = (imagebright*(255-blendimage->s.alpha))/255 + (blendbright*blendimage->s.alpha)/255;
				}
			}
			else
			{
				if (blendimage->s.alpha == 0)
				{
					cur->rgba = image->rgba;
					cur++; image++; blendimage++;
					continue;
				}
				else
				{
					SETBRIGHTNESS(brightness,blendimage->s.red,blendimage->s.green,blendimage->s.blue);
				}
			}

			// Calculate a sort of "gradient" for the skincolor
			// (Me splitting this into a function didn't work, so I had to ruin this entire function's groove...)
			{
				RGBA_t nextcolor;
				UINT8 firsti, secondi, mul;
				UINT32 r, g, b;

				// Rainbow needs to find the closest match to the textures themselves, instead of matching brightnesses to other colors.
				// Ensue horrible mess.
				if (skinnum == TC_RAINBOW)
				{
					UINT16 brightdif = 256;
					UINT8 colorbrightnesses[16];
					INT32 compare, m, d;
					UINT8 i;

					// Ignore pure white & pitch black
					if (brightness > 253 || brightness < 2)
					{
						cur->rgba = image->rgba;
						cur++; image++; blendimage++;
						continue;
					}

					firsti = 0;
					mul = 0;

					for (i = 0; i < 16; i++)
					{
						RGBA_t tempc = V_GetColor(translation[i]);
						SETBRIGHTNESS(colorbrightnesses[i], tempc.s.red, tempc.s.green, tempc.s.blue); // store brightnesses for comparison
					}

					for (i = 0; i < 16; i++)
					{
						if (brightness > colorbrightnesses[i]) // don't allow greater matches (because calculating a makeshift gradient for this is already a huge mess as is)
							continue;
						compare = abs((INT16)(colorbrightnesses[i]) - (INT16)(brightness));
						if (compare < brightdif)
						{
							brightdif = (UINT16)compare;
							firsti = i; // best matching color that's equal brightness or darker
						}
					}

					secondi = firsti+1; // next color in line
					if (secondi == 16)
					{
						m = (INT16)brightness; // - 0;
						d = (INT16)colorbrightnesses[firsti]; // - 0;
					}
					else
					{
						m = (INT16)brightness - (INT16)colorbrightnesses[secondi];
						d = (INT16)colorbrightnesses[firsti] - (INT16)colorbrightnesses[secondi];
					}

					if (m >= d)
						m = d-1;

					// calculate the "gradient" multiplier based on how close this color is to the one next in line
					if (m <= 0 || d <= 0)
						mul = 0;
					else
						mul = 15 - ((m * 16) / d);
				}
				else
				{
					// Thankfully, it's normally way more simple.
					// Just convert brightness to a skincolor value, use remainder to find the gradient multipler
					firsti = ((UINT8)(255-brightness) / 16);
					secondi = firsti+1;
					mul = ((UINT8)(255-brightness) % 16);
				}

				blendcolor = V_GetColor(translation[firsti]);

				if (mul > 0 // If it's 0, then we only need the first color.
					&& translation[firsti] != translation[secondi]) // Some colors have duplicate colors in a row, so let's just save the process
				{
					if (secondi == 16) // blend to black
						nextcolor = V_GetColor(31);
					else
						nextcolor = V_GetColor(translation[secondi]);

					// Find difference between points
					r = (UINT32)(nextcolor.s.red - blendcolor.s.red);
					g = (UINT32)(nextcolor.s.green - blendcolor.s.green);
					b = (UINT32)(nextcolor.s.blue - blendcolor.s.blue);

					// Find the gradient of the two points
					r = ((mul * r) / 16);
					g = ((mul * g) / 16);
					b = ((mul * b) / 16);

					// Add gradient value to color
					blendcolor.s.red += r;
					blendcolor.s.green += g;
					blendcolor.s.blue += b;
				}
			}

			if (skinnum == TC_RAINBOW)
			{
				UINT32 tempcolor;
				UINT16 colorbright;

				SETBRIGHTNESS(colorbright,blendcolor.s.red,blendcolor.s.green,blendcolor.s.blue);
				if (colorbright == 0)
					colorbright = 1; // no dividing by 0 please

				tempcolor = (brightness * blendcolor.s.red) / colorbright;
				tempcolor = min(255, tempcolor);
				cur->s.red = (UINT8)tempcolor;

				tempcolor = (brightness * blendcolor.s.green) / colorbright;
				tempcolor = min(255, tempcolor);
				cur->s.green = (UINT8)tempcolor;

				tempcolor = (brightness * blendcolor.s.blue) / colorbright;
				tempcolor = min(255, tempcolor);
				cur->s.blue = (UINT8)tempcolor;
				cur->s.alpha = image->s.alpha;
			}
			else
			{
				// Color strength depends on image alpha
				INT32 tempcolor;

				tempcolor = ((image->s.red * (255-blendimage->s.alpha)) / 255) + ((blendcolor.s.red * blendimage->s.alpha) / 255);
				tempcolor = min(255, tempcolor);
				cur->s.red = (UINT8)tempcolor;

				tempcolor = ((image->s.green * (255-blendimage->s.alpha)) / 255) + ((blendcolor.s.green * blendimage->s.alpha) / 255);
				tempcolor = min(255, tempcolor);
				cur->s.green = (UINT8)tempcolor;

				tempcolor = ((image->s.blue * (255-blendimage->s.alpha)) / 255) + ((blendcolor.s.blue * blendimage->s.alpha) / 255);
				tempcolor = min(255, tempcolor);
				cur->s.blue = (UINT8)tempcolor;
				cur->s.alpha = image->s.alpha;
			}
		}

		cur++; image++; blendimage++;
	}

	return;
}

#undef SETBRIGHTNESS

static void HWR_GetBlendedTexture(GLPatch_t *gpatch, GLPatch_t *blendgpatch, INT32 skinnum, const UINT8 *colormap, skincolors_t color)
{
	// mostly copied from HWR_GetMappedPatch, hence the similarities and comment
	GLMipmap_t *grmip, *newmip;

	if ((colormap == colormaps || colormap == NULL) && (skinnum > TC_DEFAULT))
	{
		// Don't do any blending
		HWD.pfnSetTexture(gpatch->mipmap);
		return;
	}

	// search for the mipmap
	// skip the first (no colormap translated)
	for (grmip = gpatch->mipmap; grmip->nextcolormap; )
	{
		grmip = grmip->nextcolormap;
		if (grmip->colormap == colormap || (skinnum < TC_DEFAULT && grmip->tcindex == skinnum))
		{
			if (grmip->downloaded && grmip->grInfo.data)
			{
				HWD.pfnSetTexture(grmip); // found the colormap, set it to the correct texture
				Z_ChangeTag(grmip->grInfo.data, PU_HWRMODELTEXTURE);
				return;
			}
		}
	}

	// If here, the blended texture has not been created
	// So we create it

	//BP: WARNING: don't free it manually without clearing the cache of harware renderer
	//              (it have a liste of mipmap)
	//    this malloc is cleared in HWR_FreeTextureCache
	//    (...) unfortunately z_malloc fragment alot the memory :(so malloc is better
	newmip = calloc(1, sizeof (*newmip));
	if (newmip == NULL)
		I_Error("%s: Out of memory", "HWR_GetMappedPatch");
	grmip->nextcolormap = newmip;
	newmip->colormap = colormap;
	newmip->tcindex = skinnum;

	HWR_CreateBlendedTexture(gpatch, blendgpatch, newmip, skinnum, color);

	HWD.pfnSetTexture(newmip);
	Z_ChangeTag(newmip->grInfo.data, PU_HWRMODELTEXTURE);
}

#define NORMALFOG 0x00000000
#define FADEFOG 0x19000000

//
// HWR_DrawModel
//

boolean HWR_DrawModel(modelinfo_t *md2, gr_vissprite_t *spr)
{
	FSurfaceInfo Surf;
	INT32 frame = 0;
	INT32 nextFrame = -1;
	UINT8 spr2 = 0;
	FTransform p;
	UINT8 color[4];

	if (md2->error)
		return false; // we already failed loading this before :(

	if (spr->precip)
		return false;

	// Lactozilla: Disallow certain models from rendering
	if (!Model_AllowRendering(spr->mobj))
		return false;

	memset(&p, 0x00, sizeof(FTransform));

	// MD2 colormap fix
	// colormap test
	if (spr->mobj->subsector)
	{
		sector_t *sector = spr->mobj->subsector->sector;
		UINT8 lightlevel = 255;
		extracolormap_t *colormap = sector->extra_colormap;

		if (sector->numlights)
		{
			INT32 light;

			light = R_GetPlaneLight(sector, spr->mobj->z + spr->mobj->height, false); // Always use the light at the top instead of whatever I was doing before

			if (!(spr->mobj->frame & FF_FULLBRIGHT))
				lightlevel = *sector->lightlist[light].lightlevel;

			if (*sector->lightlist[light].extra_colormap)
				colormap = *sector->lightlist[light].extra_colormap;
		}
		else
		{
			if (!(spr->mobj->frame & FF_FULLBRIGHT))
				lightlevel = sector->lightlevel;

			if (sector->extra_colormap)
				colormap = sector->extra_colormap;
		}

		if (colormap)
			Surf.FlatColor.rgba = HWR_Lighting(lightlevel, colormap->rgba, colormap->fadergba, false, false);
		else
			Surf.FlatColor.rgba = HWR_Lighting(lightlevel, NORMALFOG, FADEFOG, false, false);
	}
	else
		Surf.FlatColor.rgba = 0xFFFFFFFF;

	// Look at HWR_ProjectSprite for more
	{
		GLPatch_t *gpatch;
		INT32 durs = spr->mobj->state->tics;
		INT32 tics = spr->mobj->tics;
		//mdlframe_t *next = NULL;
		const UINT8 flip = (UINT8)(!(spr->mobj->eflags & MFE_VERTICALFLIP) != !(spr->mobj->frame & FF_VERTICALFLIP));
		spritedef_t *sprdef;
		spriteframe_t *sprframe;
#ifdef ROTSPRITE
		spriteinfo_t *sprinfo;
		angle_t ang;
#endif
		INT32 mod;
		float finalscale;

		if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY)
		{
			sprdef = &((skin_t *)spr->mobj->skin)->sprites[spr->mobj->sprite2];
#ifdef ROTSPRITE
			sprinfo = &((skin_t *)spr->mobj->skin)->sprinfo[spr->mobj->sprite2];
#endif
		}
		else
		{
			sprdef = &sprites[spr->mobj->sprite];
#ifdef ROTSPRITE
			sprinfo = &spriteinfo[spr->mobj->sprite];
#endif
		}

		sprframe = &sprdef->spriteframes[spr->mobj->frame & FF_FRAMEMASK];

		if (spr->mobj->flags2 & MF2_SHADOW)
			Surf.FlatColor.s.alpha = 0x40;
		else if (spr->mobj->frame & FF_TRANSMASK)
			HWR_TranstableToAlpha((spr->mobj->frame & FF_TRANSMASK)>>FF_TRANSSHIFT, &Surf);
		else
			Surf.FlatColor.s.alpha = 0xFF;

		//HWD.pfnSetBlend(blend); // This seems to actually break translucency?
		finalscale = md2->scale;
		//Hurdler: arf, I don't like that implementation at all... too much crappy
		gpatch = md2->texture->grpatch;
		if (!gpatch || !gpatch->mipmap->grInfo.format || !gpatch->mipmap->downloaded)
		{
			if (Model_LoadTexture(md2))
			{
				gpatch = md2->texture->grpatch; // Load it again, because it isn't loaded into gpatch after Model_LoadTexture...
				HWD.pfnSetTexture(gpatch->mipmap);
			}
		}

		if ((gpatch && gpatch->mipmap->grInfo.format) // don't load the blend texture if the base texture isn't available
			&& (!md2->texture->blendgrpatch || !((GLPatch_t *)md2->texture->blendgrpatch)->mipmap->grInfo.format || !((GLPatch_t *)md2->texture->blendgrpatch)->mipmap->downloaded))
		{
			if (Model_LoadBlendTexture(md2))
				HWD.pfnSetTexture(((GLPatch_t *)md2->texture->blendgrpatch)->mipmap); // We do need to do this so that it can be cleared and knows to recreate it when necessary
		}

		if (gpatch && gpatch->mipmap->grInfo.format) // else if meant that if a texture couldn't be loaded, it would just end up using something else's texture
		{
			if (md2->texture->blendgrpatch && ((GLPatch_t *)md2->texture->blendgrpatch)->mipmap->grInfo.format
				&& gpatch->width == ((GLPatch_t *)md2->texture->blendgrpatch)->width && gpatch->height == ((GLPatch_t *)md2->texture->blendgrpatch)->height)
			{
				INT32 skinnum = INT32_MAX;
				if ((spr->mobj->flags & (MF_ENEMY|MF_BOSS)) && (spr->mobj->flags2 & MF2_FRET) && !(spr->mobj->flags & MF_GRENADEBOUNCE) && (leveltime & 1)) // Bosses "flash"
				{
					if (spr->mobj->type == MT_CYBRAKDEMON || spr->mobj->colorized)
						skinnum = TC_ALLWHITE;
					else if (spr->mobj->type == MT_METALSONIC_BATTLE)
						skinnum = TC_METALSONIC;
					else
						skinnum = TC_BOSS;
				}
				else if ((skincolors_t)spr->mobj->color != SKINCOLOR_NONE)
				{
					if (spr->mobj->colorized)
						skinnum = TC_RAINBOW;
					else if (spr->mobj->player && spr->mobj->player->dashmode >= DASHMODE_THRESHOLD
						&& (spr->mobj->player->charflags & SF_DASHMODE)
						&& ((leveltime/2) & 1))
					{
						if (spr->mobj->player->charflags & SF_MACHINE)
							skinnum = TC_DASHMODE;
						else
							skinnum = TC_RAINBOW;
					}
					else if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY)
						skinnum = (INT32)((skin_t*)spr->mobj->skin-skins);
					else
						skinnum = TC_DEFAULT;
				}

				// Translation or skin number found
				if (skinnum != INT32_MAX)
					HWR_GetBlendedTexture(gpatch, (GLPatch_t *)md2->texture->blendgrpatch, skinnum, spr->colormap, (skincolors_t)spr->mobj->color);
				else
				{
					// Sorry nothing
					HWD.pfnSetTexture(gpatch->mipmap);
				}
			}
			else
			{
				// This is safe, since we know the texture has been downloaded
				HWD.pfnSetTexture(gpatch->mipmap);
			}
		}
		else
		{
			// Sprite
			gpatch = spr->gpatch;
			HWR_GetMappedPatch(gpatch, spr->colormap);
		}

		if (spr->mobj->frame & FF_ANIMATE)
		{
			// set duration and tics to be the correct values for FF_ANIMATE states
			durs = spr->mobj->state->var2;
			tics = spr->mobj->anim_duration;
		}

		frame = (spr->mobj->frame & FF_FRAMEMASK);
		if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY && md2->model->spr2frames)
		{
			spr2 = Model_GetSprite2(md2, spr->mobj->skin, spr->mobj->sprite2, spr->mobj->player);
			mod = md2->model->spr2frames[spr2].numframes;
#ifndef DONTHIDEDIFFANIMLENGTH // by default, different anim length is masked by the mod
			if (mod > (INT32)((skin_t *)spr->mobj->skin)->sprites[spr2].numframes)
				mod = ((skin_t *)spr->mobj->skin)->sprites[spr2].numframes;
#endif
			if (!mod)
				mod = 1;
			frame = md2->model->spr2frames[spr2].frames[frame%mod];
		}
		else
		{
			mod = md2->model->meshes[0].numFrames;
			if (!mod)
				mod = 1;
		}

#ifdef USE_MODEL_NEXTFRAME
#define INTERPOLERATION_LIMIT TICRATE/4
		if (cv_modelinterpolation.value && tics <= durs && tics <= INTERPOLERATION_LIMIT)
		{
			if (durs > INTERPOLERATION_LIMIT)
				durs = INTERPOLERATION_LIMIT;

			if (spr->mobj->skin && spr->mobj->sprite == SPR_PLAY && md2->model->spr2frames)
			{
				if (Model_CanInterpolateSprite2(&md2->model->spr2frames[spr2])
					&& (spr->mobj->frame & FF_ANIMATE
					|| (spr->mobj->state->nextstate != S_NULL
					&& states[spr->mobj->state->nextstate].sprite == SPR_PLAY
					&& ((P_GetSkinSprite2(spr->mobj->skin, (((spr->mobj->player && spr->mobj->player->powers[pw_super]) ? FF_SPR2SUPER : 0)|states[spr->mobj->state->nextstate].frame) & FF_FRAMEMASK, spr->mobj->player) == spr->mobj->sprite2)))))
				{
					nextFrame = (spr->mobj->frame & FF_FRAMEMASK) + 1;
					if (nextFrame >= mod)
						nextFrame = 0;
					if (frame || !(spr->mobj->state->frame & FF_SPR2ENDSTATE))
						nextFrame = md2->model->spr2frames[spr2].frames[nextFrame];
					else
						nextFrame = -1;
				}
			}
			else if (Model_CanInterpolate(spr->mobj, md2->model))
			{
				// frames are handled differently for states with FF_ANIMATE, so get the next frame differently for the interpolation
				if (spr->mobj->frame & FF_ANIMATE)
				{
					nextFrame = (spr->mobj->frame & FF_FRAMEMASK) + 1;
					if (nextFrame >= (INT32)(spr->mobj->state->var1 + (spr->mobj->state->frame & FF_FRAMEMASK)))
						nextFrame = (spr->mobj->state->frame & FF_FRAMEMASK) % mod;
				}
				else
				{
					if (spr->mobj->state->nextstate != S_NULL && states[spr->mobj->state->nextstate].sprite != SPR_NULL
					&& !(spr->mobj->player && (spr->mobj->state->nextstate == S_PLAY_WAIT) && spr->mobj->state == &states[S_PLAY_STND]))
						nextFrame = (states[spr->mobj->state->nextstate].frame & FF_FRAMEMASK) % mod;
				}
			}
		}
#undef INTERPOLERATION_LIMIT
#endif

		//Hurdler: it seems there is still a small problem with mobj angle
		p.x = FIXED_TO_FLOAT(spr->mobj->x);
		p.y = FIXED_TO_FLOAT(spr->mobj->y)+md2->offset;

		if (flip)
			p.z = FIXED_TO_FLOAT(spr->mobj->z + spr->mobj->height);
		else
			p.z = FIXED_TO_FLOAT(spr->mobj->z);

		if (sprframe->rotate)
		{
			fixed_t anglef = AngleFixed(spr->mobj->angle);

			if (spr->mobj->player)
				anglef = AngleFixed(spr->mobj->player->drawangle);
			else
				anglef = AngleFixed(spr->mobj->angle);

			p.angley = FIXED_TO_FLOAT(anglef);
		}
		else
		{
			const fixed_t anglef = AngleFixed((R_PointToAngle(spr->mobj->x, spr->mobj->y))-ANGLE_180);
			p.angley = FIXED_TO_FLOAT(anglef);
		}

#ifdef ROTSPRITE
		p.rollangle = 0.0f;
		p.rollflip = 0;
		p.rotaxis = 0;
		if (spr->mobj->rollangle)
		{
			fixed_t anglef = AngleFixed(spr->mobj->rollangle);
			p.rollangle = FIXED_TO_FLOAT(anglef);
			p.roll = true;

			// rotation pivot
			p.centerx = FIXED_TO_FLOAT(spr->mobj->radius/2);
			p.centery = FIXED_TO_FLOAT(spr->mobj->height/2);

			// rotation axis
			if (sprinfo->available)
				p.rotaxis = (UINT8)(sprinfo->pivot[(spr->mobj->frame & FF_FRAMEMASK)].rotaxis);

			// for NiGHTS specifically but should work everywhere else
			ang = R_PointToAngle (spr->mobj->x, spr->mobj->y) - (spr->mobj->player ? spr->mobj->player->drawangle : spr->mobj->angle);
			if ((sprframe->rotate & SRF_RIGHT) && (ang < ANGLE_180)) // See from right
				p.rollflip = 1;
			else if ((sprframe->rotate & SRF_LEFT) && (ang >= ANGLE_180)) // See from left
				p.rollflip = -1;
		}
#endif

		p.anglex = 0.0f;

#ifdef USE_FTRANSFORM_ANGLEZ
		// Slope rotation from Kart
		p.anglez = 0.0f;
		if (spr->mobj->standingslope)
		{
			fixed_t tempz = spr->mobj->standingslope->normal.z;
			fixed_t tempy = spr->mobj->standingslope->normal.y;
			fixed_t tempx = spr->mobj->standingslope->normal.x;
			fixed_t tempangle = AngleFixed(R_PointToAngle2(0, 0, FixedSqrt(FixedMul(tempy, tempy) + FixedMul(tempz, tempz)), tempx));
			p.anglez = FIXED_TO_FLOAT(tempangle);
			tempangle = -AngleFixed(R_PointToAngle2(0, 0, tempz, tempy));
			p.anglex = FIXED_TO_FLOAT(tempangle);
		}
#endif

		color[0] = Surf.FlatColor.s.red;
		color[1] = Surf.FlatColor.s.green;
		color[2] = Surf.FlatColor.s.blue;
		color[3] = Surf.FlatColor.s.alpha;

		// SRB2CBTODO: MD2 scaling support
		finalscale *= FIXED_TO_FLOAT(spr->mobj->scale);

		p.flip = atransform.flip;
#ifdef USE_FTRANSFORM_MIRROR
		p.mirror = atransform.mirror; // from Kart
#endif

		HWD.pfnDrawModel(md2->model, frame, durs, tics, nextFrame, &p, finalscale, flip, color);
	}

	return true;
}

#endif //HWRENDER
