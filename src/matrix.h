/************************************
*	Matrix Screensaver
*	Copyright © 2011 Henry++
*************************************/

#ifndef __MATRIX_H__
#define __MATRIX_H__

// Includes
#include <windows.h>
#include <commctrl.h>

#include "resource.h"

// Defines 
#define APP_NAME L"Matrix Screensaver"
#define APP_NAME_SHORT L"matrix"
#define APP_VERSION L"1.0"
#define APP_VERSION_FULL 1,0
#define APP_WEBSITE L"http://www.henrypp.org"

// Matrix Defines
WORD _crc_reg = 0;

typedef unsigned short GLYPH;

#define GLYPH_REDRAW 0x8000
#define GLYPH_BLANK  0x4000

#define DENSITY			25
#define DENSITY_MAX		50
#define DENSITY_MIN		5

// constants inferred from matrix.bmp
#define MAX_INTENSITY	5			// number of intensity levels
#define NUM_GLYPHS		26			// number of "glyphs" in each level
#define GLYPH_WIDTH		14			// width  of each glyph (pixels)
#define GLYPH_HEIGHT	14			// height of each glyph (pixels)

#define SPEED_MAX		10
#define SPEED_MIN		1

//
//	The "matrix" is basically an array of these
//  column structures, positioned side-by-side
//
typedef struct
{
	BOOL	state;
	int		countdown;

	BOOL	started;
	int		runlen;
	
	int		blippos;
	int		bliplen;

	int		length;
	GLYPH	*glyph;

} MATRIX_COLUMN;

typedef struct
{
	int				width;
	int				height;
	int				numcols;
	int				numrows;

	// bitmap containing glyphs.
	HDC				hdcBitmap;
	HBITMAP			hbmBitmap;

	MATRIX_COLUMN	column[1];

} MATRIX;


int		g_nMatrixSpeed		 = 2;
int		g_nDensity			 = 32;
int		g_nHue				 = 100;

/*******************************
* Functions
*******************************/

//
// this isn't really a random-number generator. It's based on
// a 16bit CRC algorithm. With the right mask (0xb400) it is possible
// to call this function 65536 times and get a unique result every time
// with *NO* repeats. The results look random but they're not - if we
// call this function another 65536 times we get exactly the same results
// in the same order. This is necessary for fading in messages because
// we need to be guaranteed that all cells...it's completely uniform in
// operation but looks random enough to be very effective
//
WORD crc_msgrand(WORD reg)
{
	const WORD mask = 0xb400;

	if(reg & 1)
		reg = (reg >> 1) ^ mask;
	else
		reg = (reg >> 1);

	return reg;
}

int crc_rand()
{
	const  WORD mask = 0xb400;

	if(_crc_reg & 1)
		_crc_reg = (_crc_reg >> 1) ^ mask;
	else
		_crc_reg = (_crc_reg >> 1);

	return _crc_reg;
}

static double HuetoRGB(double m1, double m2, double h )
{
	if( h < 0 ) h += 1.0;
	if( h > 1 ) h -= 1.0;

	if( 6.0*h < 1 )
		return (m1+(m2-m1)*h*6.0);
	
	if( 2.0*h < 1 )
		return m2;

	if( 3.0*h < 2.0 )
		return (m1+(m2-m1)*((2.0/3.0)-h)*6.0);

	return m1;
}

COLORREF HSLtoRGB( double H, double S, double L )
{
	double r,g,b;
	double m1, m2;
	
	if(S==0)
	{
		r=g=b=L;
	} 
	else
	{
		if(L <=0.5)
			m2 = L*(1.0+S);
		else
			m2 = L+S-L*S;
		m1 = 2.0*L-m2;

		r = HuetoRGB(m1,m2,H+1.0/3.0);
		g = HuetoRGB(m1,m2,H);
		b = HuetoRGB(m1,m2,H-1.0/3.0);
		
	} 
	
	return RGB((BYTE)(r*255),(BYTE)(g*255),(BYTE)(b*255));
}

COLORREF RGBtoHSL(COLORREF col, double *H, double *S, double *L)
{
	double r = GetRValue(col) / 255.0;
	double g = GetGValue(col) / 255.0;
	double b = GetBValue(col) / 255.0;

	double cMin = min(min(r, g), b);
	double cMax = max(max(r, g), b);
	double l = (cMax + cMin) / 2.0;
	double h, s;
	double delta = cMax-cMin;

	if(delta == 0)
	{
		s = 0;
		h  = 0;
	}
	else
	{
		if(l <= 0.5) 
			s = (cMax - cMin) / (cMax + cMin);
		else				    
			s = (cMax - cMin) / (2.0 - cMax - cMin);

		if(r == cMax)
			h = (g - b) / delta;
		else if(g == cMax)
			h = 2.0 + (b - r) / delta;
		else
			h = 4.0 + (r - g) / delta;

		h /= 6.0;
		if(h < 0)
			h += 1.0;

	}

	*H = h;
	*S = s;
	*L = l;
	return 0;
}

MATRIX *GetMatrix(HWND hwnd)
{
	return (MATRIX*)GetWindowLong(hwnd, 0);
}

void SetMatrix(HWND hwnd, MATRIX* matrix)
{
	SetWindowLong(hwnd, 0, (LONG)matrix);
}

int GlyphIntensity(GLYPH glyph)
{
	return (int)((glyph & 0x7f00) >> 8);
}

GLYPH RandomGlyph(int intensity)
{
	return GLYPH_REDRAW | (intensity << 8) | (crc_rand() % NUM_GLYPHS);
}

GLYPH DarkenGlyph(GLYPH glyph)
{
	int intensity = GlyphIntensity(glyph);
	
	if(intensity > 0)
		return GLYPH_REDRAW | ((intensity - 1) << 8) | (glyph & 0x00FF);
	else
		return glyph;
}

void DrawGlyph(MATRIX *matrix, HDC hdc, int xpos, int ypos, GLYPH glyph)
{
	int intensity = GlyphIntensity(glyph);
	int glyphidx  = glyph & 0xff;

	BitBlt(hdc, xpos, ypos, GLYPH_WIDTH, GLYPH_HEIGHT, matrix->hdcBitmap,
		glyphidx * GLYPH_WIDTH, intensity * GLYPH_HEIGHT, SRCCOPY);
}

void RedrawBlip(GLYPH *glypharr, int blippos)
{
	glypharr[blippos+0] |= GLYPH_REDRAW;
	glypharr[blippos+1] |= GLYPH_REDRAW;
	glypharr[blippos+8] |= GLYPH_REDRAW;
	glypharr[blippos+9] |= GLYPH_REDRAW;
}

void ScrollMatrixColumn(MATRIX_COLUMN* col)
{
	int y = 0;
	GLYPH lastglyph = 0;
	GLYPH thisglyph = 0;

	// wait until we are allowed to scroll
	if(!col->started)
	{
		if(--col->countdown <= 0)
			col->started = TRUE;

		return;
	}

	// "seed" the glyph-run
	lastglyph = col->state ? (GLYPH)0 : (GLYPH)(MAX_INTENSITY << 8);

	//
	// loop over the entire length of the column, looking for changes
	// in intensity/darkness. This change signifies the start/end
	// of a run of glyphs.
	//
	for(y = 0; y < col->length; y++)
	{
		thisglyph = col->glyph[y];

		// bottom-most part of "run". Insert a new character (glyph)
		// at the end to lengthen the run down the screen..gives the
		// impression that the run is "falling" down the screen
		if(GlyphIntensity(thisglyph) < GlyphIntensity(lastglyph) && 
			GlyphIntensity(thisglyph) == 0)
		{
			col->glyph[y] = RandomGlyph(MAX_INTENSITY - 1);
			y++;
		}
		// top-most part of "run". Delete a character off the top by
		// darkening the glyph until it eventually disappears (turns black). 
		// this gives the effect that the run as dropped downwards
		else if(GlyphIntensity(thisglyph) > GlyphIntensity(lastglyph))
		{
			col->glyph[y] = DarkenGlyph(thisglyph);
			
			// if we've just darkened the last bit, skip on so
			// the whole run doesn't go dark
			if(GlyphIntensity(thisglyph) == MAX_INTENSITY - 1)
				y++;
		}

		lastglyph = col->glyph[y];
	}

	// change state from blanks <-> runs when the current run as expired
	if(--col->runlen <= 0)
	{
		int density = g_nDensity;
		density = DENSITY_MAX - density + DENSITY_MIN;
		if(col->state ^= 1)			
			col->runlen = crc_rand() % (3 * density + DENSITY_MIN);
		else
			col->runlen = crc_rand() % (DENSITY_MAX-density+1) + (DENSITY_MIN*2);
	}

	//
	// make a "blip" run down this column at double-speed
	//

	// mark current blip as redraw so it gets "erased"
	if(col->blippos >= 0 && col->blippos < col->length)
		RedrawBlip(col->glyph, col->blippos);

	// advance down screen at double-speed
	col->blippos += 2;
	
	// if the blip gets to the end of a run, start it again (for a random
	// length so that the blips never get synched together)
	if(col->blippos >= col->bliplen)
	{
		col->bliplen = col->length + crc_rand() % 50;
		col->blippos = 0;
	}

	// now redraw blip at new position
	if(col->blippos >= 0 && col->blippos < col->length)
		RedrawBlip(col->glyph, col->blippos);

}

//
// randomly change a small collection glyphs in a column
//
void RandomMatrixColumn(MATRIX_COLUMN *col)
{
	int i, y;

	for(i = 1, y = 0; i < 16; i++)
	{
		// find a run
		while(GlyphIntensity(col->glyph[y]) < MAX_INTENSITY-1 && y < col->length) 
			y++;

		if(y >= col->length)
			break;

		col->glyph[y]  = (col->glyph[y] & 0xff00) | (crc_rand() % NUM_GLYPHS);
		col->glyph[y] |= GLYPH_REDRAW;

		y += crc_rand() % 10;
	}
}

void RedrawMatrixColumn(MATRIX_COLUMN *col, MATRIX *matrix, HDC hdc, int xpos)
{
	int y;

	// loop down the length of the column redrawing only what needs doing
	for(y = 0; y < col->length; y++)
	{
		GLYPH glyph = col->glyph[y];

		// does this glyph (character) need to be redrawn?
		if(glyph & GLYPH_REDRAW)
		{
			if((y == col->blippos+0 || y == col->blippos+1 ||
				y == col->blippos+8 || y == col->blippos+9) && 
				GlyphIntensity(glyph) >= MAX_INTENSITY-1)
				glyph |= MAX_INTENSITY << 8;

			DrawGlyph(matrix, hdc, xpos, y * GLYPH_HEIGHT, glyph);
			
			// clear redraw state
			col->glyph[y] &= ~GLYPH_REDRAW;
		}
	}
}

HBITMAP MakeBitmap(HINSTANCE hInst, UINT uId, double hue)
{
	HBITMAP hDIB;
	HANDLE  hOldBmp;
	HBITMAP hBmp;
	HDC		hdc;

	DIBSECTION dib;
	BITMAPINFOHEADER *bih;
	DWORD *dest = 0;
	BYTE  *src;
	int i;

	RGBQUAD pal[256];

	// load the 8bit image 
	hBmp = (HBITMAP)LoadImage(hInst, MAKEINTRESOURCE(IDB_GLYPHS), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
	
	// extract the colour table
	hdc = CreateCompatibleDC(NULL);
	hOldBmp = SelectObject(hdc, hBmp);
	GetDIBColorTable(hdc, 0, 256, pal);
	SelectObject(hdc, hOldBmp);

	// 
	GetObject(hBmp, sizeof(dib), &dib);
	src = (BYTE*)dib.dsBm.bmBits;
	bih = &dib.dsBmih;

	// change to a 32bit bitmap
	dib.dsBm.bmBitsPixel		= 32;
	dib.dsBm.bmPlanes			= 1;
	dib.dsBm.bmWidthBytes		= dib.dsBm.bmWidth * 4;
	dib.dsBm.bmType				= 0;
	dib.dsBmih.biBitCount		= 32;
	dib.dsBmih.biPlanes			= 1;
	dib.dsBmih.biCompression	= 0;
	dib.dsBmih.biSizeImage		= dib.dsBmih.biWidth * dib.dsBmih.biHeight * 4;
	dib.dsBmih.biClrUsed		= 0;
	dib.dsBmih.biClrImportant	= 0;

	// create a new (blank) 32bit DIB section
	hDIB = CreateDIBSection(hdc, (BITMAPINFO *)&dib.dsBmih, DIB_RGB_COLORS, (void**)&dest, 0, 0);

	// copy each pixel
	for(i = 0; i < bih->biWidth * bih->biHeight; i++)
	{
		// convert 8bit palette entry to 32bit colour
		RGBQUAD  rgb = pal[*src++];
		COLORREF col = RGB(rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue);
		
		// convert the RGB colour to H,S,L values
		double h,s,l;
		RGBtoHSL(col, &h, &s, &l);

		// create the new colour 
		*dest++ = HSLtoRGB(hue, s, l);
	}

	DeleteObject(hBmp);
	DeleteDC(hdc);
	return hDIB;
}

void SetMatrixBitmap(MATRIX *matrix, int hue)
{
	HBITMAP hBmp;
	
	hue %= 255;

	// create the new bitmap
	hBmp = MakeBitmap(GetModuleHandle(0), IDB_GLYPHS, hue / 255.0);
	DeleteObject(SelectObject(matrix->hdcBitmap, hBmp));
	
	matrix->hbmBitmap = hBmp;
	SelectObject(matrix->hdcBitmap, matrix->hbmBitmap);
}

void DecodeMatrix(HWND hwnd, MATRIX *matrix)
{
	HDC hdc = GetDC(hwnd);

	for(int x = 0; x < matrix->numcols; x++)
	{
		RandomMatrixColumn(&matrix->column[x]);		
		ScrollMatrixColumn(&matrix->column[x]);
		RedrawMatrixColumn(&matrix->column[x], matrix, hdc, x * GLYPH_WIDTH);
	}

	// randomize matrix colors
	SetMatrixBitmap(matrix, g_nHue++);

	ReleaseDC(hwnd, hdc);
}

void RefreshMatrix(HWND hwnd)
{
	int x, y;
	MATRIX *matrix = GetMatrix(hwnd);
	for(x = 0; x < matrix->numcols; x++)
	{
		for(y = 0; y < matrix->column[x].length; y++)
			matrix->column[x].glyph[y] |= GLYPH_REDRAW;
	}

	SendMessage(hwnd, WM_TIMER, 0, 0);
}

//
//	Allocate matrix structures
//
MATRIX *CreateMatrix(HWND hwnd, int width, int height)
{
	MATRIX *matrix;
	HDC hdc;
	int x, y;

	int rows = height / GLYPH_HEIGHT + 1;
	int cols = width  / GLYPH_WIDTH  + 1;

	// allocate matrix!
	if((matrix = (MATRIX*)malloc(sizeof(MATRIX) + sizeof(MATRIX_COLUMN) * cols)) == 0)
		return 0;

	matrix->numcols = cols;
	matrix->numrows = rows;
	matrix->width   = width;
	matrix->height  = height;

	for(x = 0; x < cols; x++)
	{
		matrix->column[x].length       = rows;
		matrix->column[x].started      = FALSE;
		matrix->column[x].countdown    = crc_rand() % 100;
		matrix->column[x].state        = crc_rand() % 2;
		matrix->column[x].runlen       = crc_rand() % 20 + 3;

		matrix->column[x].glyph  = (GLYPH*)malloc(sizeof(GLYPH) * (rows+16));

		for(y = 0; y < rows; y++)
			matrix->column[x].glyph[y] = 0;//;
	}
	
	// Load bitmap!!
	hdc = GetDC(NULL);
	matrix->hbmBitmap = MakeBitmap(GetModuleHandle(0), IDB_GLYPHS, g_nHue / 255.0);
	matrix->hdcBitmap = CreateCompatibleDC(hdc);
	SelectObject(matrix->hdcBitmap, matrix->hbmBitmap);
	ReleaseDC(NULL, hdc);

	return matrix;
}

//
//	Free up matrix structures
//
void DestroyMatrix(MATRIX *matrix)
{
	int x;

	// free the matrix columns
	for(x = 0; x < matrix->numcols; x++)
		free(matrix->column[x].glyph);

	DeleteDC(matrix->hdcBitmap);
	DeleteObject(matrix->hbmBitmap);

	// now delete the matrix!
	free(matrix);
}
#endif
