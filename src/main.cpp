// Matrix Screensaver
// Copyright (c) J Brown 2003 (catch22.net)
// Copyright (c) 2011-2018 Henry++

#include <windows.h>
//#include <scrnsave.h>

#include "main.hpp"
#include "rapp.hpp"
#include "resource.hpp"

rapp app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_COPYRIGHT);

ULONGLONG _crc_reg = 0;

UINT nAmount = app.ConfigGet (L"NumGlyphs", AMOUNT_DEFAULT).AsUint ();
UINT nDensity = app.ConfigGet (L"Density", DENSITY_DEFAULT).AsUint ();
UINT nHue = app.ConfigGet (L"Hue", HUE_DEFAULT).AsUint ();

HWND hmatrix = nullptr;

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
WORD crc_msgrand (WORD reg)
{
	if (reg & 1)
		reg = (reg >> 1) ^ RND_MASK;
	else
		reg = (reg >> 1);

	return reg;
}

UINT crc_rand ()
{
	if (_crc_reg & 1)
		_crc_reg = (_crc_reg >> 1) ^ RND_MASK;
	else
		_crc_reg = (_crc_reg >> 1);

	return (UINT)_crc_reg;
}

static double HuetoRGB (double m1, double m2, double h)
{
	if (h < 0) h += 1.0;
	if (h > 1) h -= 1.0;

	if (6.0*h < 1)
		return (m1 + (m2 - m1)*h*6.0);

	if (2.0*h < 1)
		return m2;

	if (3.0*h < 2.0)
		return (m1 + (m2 - m1)*((2.0 / 3.0) - h)*6.0);

	return m1;
}

COLORREF HSLtoRGB (double H, double S, double L)
{
	double r, g, b;
	double m1, m2;

	if (S == 0)
	{
		r = g = b = L;
	}
	else
	{
		if (L <= 0.5)
			m2 = L * (1.0 + S);
		else
			m2 = L + S - L * S;
		m1 = 2.0*L - m2;

		r = HuetoRGB (m1, m2, H + 1.0 / 3.0);
		g = HuetoRGB (m1, m2, H);
		b = HuetoRGB (m1, m2, H - 1.0 / 3.0);

	}

	return RGB ((BYTE)(r * 255), (BYTE)(g * 255), (BYTE)(b * 255));
}

void RGBtoHSL (COLORREF col, double *H, double *S, double *L)
{
	double r = GetRValue (col) / 255.0;
	double g = GetGValue (col) / 255.0;
	double b = GetBValue (col) / 255.0;

	double cMin = min (min (r, g), b);
	double cMax = max (max (r, g), b);
	double l = (cMax + cMin) / 2.0;
	double h, s;
	double delta = cMax - cMin;

	if (delta == 0)
	{
		s = 0;
		h = 0;
	}
	else
	{
		if (l <= 0.5)
			s = (cMax - cMin) / (cMax + cMin);
		else
			s = (cMax - cMin) / (2.0 - cMax - cMin);

		if (r == cMax)
			h = (g - b) / delta;
		else if (g == cMax)
			h = 2.0 + (b - r) / delta;
		else
			h = 4.0 + (r - g) / delta;

		h /= 6.0;
		if (h < 0)
			h += 1.0;

	}

	*H = h;
	*S = s;
	*L = l;
}

GLYPH GlyphIntensity (GLYPH glyph)
{
	return ((glyph & 0x7f00) >> 8);
}

GLYPH RandomGlyph (int intensity)
{
	return GLYPH_REDRAW | (intensity << 8) | (crc_rand () % nAmount);
}

GLYPH DarkenGlyph (GLYPH glyph)
{
	int intensity = GlyphIntensity (glyph);

	if (intensity > 0)
		return GLYPH_REDRAW | ((intensity - 1) << 8) | (glyph & 0x00FF);

	else
		return glyph;
}

void DrawGlyph (MATRIX *matrix, HDC hdc, int xpos, int ypos, GLYPH glyph)
{
	int intensity = GlyphIntensity (glyph);
	int glyphidx = glyph & 0xff;

	BitBlt (hdc, xpos, ypos, GLYPH_WIDTH, GLYPH_HEIGHT, matrix->hdcBitmap, glyphidx * GLYPH_WIDTH, intensity * GLYPH_HEIGHT, SRCCOPY);
}

void RedrawBlip (GLYPH *glypharr, int blippos)
{
	glypharr[blippos + 0] |= GLYPH_REDRAW;
	glypharr[blippos + 1] |= GLYPH_REDRAW;
	glypharr[blippos + 8] |= GLYPH_REDRAW;
	glypharr[blippos + 9] |= GLYPH_REDRAW;
}

void ScrollMatrixColumn (MATRIX_COLUMN* col)
{
	GLYPH lastglyph = 0;
	GLYPH thisglyph = 0;

	// wait until we are allowed to scroll
	if (!col->started)
	{
		if (--col->countdown <= 0)
			col->started = true;

		return;
	}

	// "seed" the glyph-run
	lastglyph = col->state ? 0 : (MAX_INTENSITY << 8);

	//
	// loop over the entire length of the column, looking for changes
	// in intensity/darkness. This change signifies the start/end
	// of a run of glyphs.
	//
	for (int y = 0; y < col->length; y++)
	{
		thisglyph = col->glyph[y];

		// bottom-most part of "run". Insert a new character (glyph)
		// at the end to lengthen the run down the screen..gives the
		// impression that the run is "falling" down the screen
		if (
			GlyphIntensity (thisglyph) < GlyphIntensity (lastglyph) &&
			GlyphIntensity (thisglyph) == 0)
		{
			col->glyph[y] = RandomGlyph (MAX_INTENSITY - 1);
			y++;
		}
		// top-most part of "run". Delete a character off the top by
		// darkening the glyph until it eventually disappears (turns black). 
		// this gives the effect that the run as dropped downwards
		else if (GlyphIntensity (thisglyph) > GlyphIntensity (lastglyph))
		{
			col->glyph[y] = DarkenGlyph (thisglyph);

			// if we've just darkened the last bit, skip on so
			// the whole run doesn't go dark
			if (GlyphIntensity (thisglyph) == MAX_INTENSITY - 1)
				y++;
		}

		lastglyph = col->glyph[y];
	}

	// change state from blanks <-> runs when the current run as expired
	if (--col->runlen <= 0)
	{
		const UINT density = DENSITY_MAX - nDensity + DENSITY_MIN;

		if (col->state ^= 1)
			col->runlen = crc_rand () % (3 * density / 2) + DENSITY_MIN;
		else
			col->runlen = crc_rand () % (DENSITY_MAX + 1 - density) + (DENSITY_MIN * 2);
	}

	//
	// make a "blip" run down this column at double-speed
	//

	// mark current blip as redraw so it gets "erased"
	if (col->blippos >= 0 && col->blippos < col->length)
		RedrawBlip (col->glyph, col->blippos);

	// advance down screen at double-speed
	col->blippos += 2;

	// if the blip gets to the end of a run, start it again (for a random
	// length so that the blips never get synched together)
	if (col->blippos >= col->bliplen)
	{
		col->bliplen = col->length + crc_rand () % 50;
		col->blippos = 0;
	}

	// now redraw blip at new position
	if (col->blippos >= 0 && col->blippos < col->length)
		RedrawBlip (col->glyph, col->blippos);
}

//
// randomly change a small collection glyphs in a column
//
void RandomMatrixColumn (MATRIX_COLUMN *col)
{
	for (int i = 1, y = 0; i < 16; i++)
	{
		// find a run
		while (GlyphIntensity (col->glyph[y]) < MAX_INTENSITY - 1 && y < col->length)
			y++;

		if (y >= col->length)
			break;

		col->glyph[y] = (col->glyph[y] & 0xff00) | (crc_rand () % nAmount);
		col->glyph[y] |= GLYPH_REDRAW;

		y += crc_rand () % 10;
	}
}

void RedrawMatrixColumn (MATRIX_COLUMN *col, MATRIX *matrix, HDC hdc, int xpos)
{
	// loop down the length of the column redrawing only what needs doing
	for (int y = 0; y < col->length; y++)
	{
		GLYPH glyph = col->glyph[y];

		// does this glyph (character) need to be redrawn?
		if (glyph & GLYPH_REDRAW)
		{
			if ((y == col->blippos + 0 || y == col->blippos + 1 ||
				y == col->blippos + 8 || y == col->blippos + 9) &&
				GlyphIntensity (glyph) >= MAX_INTENSITY - 1)
				glyph |= MAX_INTENSITY << 8;

			DrawGlyph (matrix, hdc, xpos, y * GLYPH_HEIGHT, glyph);

			// clear redraw state
			col->glyph[y] &= ~GLYPH_REDRAW;
		}
	}
}

HBITMAP MakeBitmap (HDC hdc, HINSTANCE hinst, UINT, double hue)
{
	DIBSECTION dib = {0};
	LPBITMAPINFOHEADER lpbih;
	LPDWORD dest = nullptr;

	RGBQUAD pal[256] = {0};

	// load the 8bit image
	const HBITMAP hBmp = (HBITMAP)LoadImage (hinst, MAKEINTRESOURCE (IDB_GLYPH), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

	// extract the colour table
	const HDC hdc_c = CreateCompatibleDC (hdc);
	const HANDLE hOldBmp = SelectObject (hdc_c, hBmp);
	GetDIBColorTable (hdc_c, 0, 256, pal);
	SelectObject (hdc_c, hOldBmp);

	GetObject (hBmp, sizeof (dib), &dib);
	LPBYTE src = (LPBYTE)dib.dsBm.bmBits;
	lpbih = &dib.dsBmih;

	// change to a 32bit bitmap
	dib.dsBm.bmBitsPixel = 32;
	dib.dsBm.bmPlanes = 1;
	dib.dsBm.bmWidthBytes = dib.dsBm.bmWidth * 4;
	dib.dsBmih.biBitCount = 32;
	dib.dsBmih.biPlanes = 1;
	dib.dsBmih.biCompression = BI_RGB;
	dib.dsBmih.biSizeImage = (dib.dsBmih.biWidth * dib.dsBmih.biHeight) * 4;

	// create a new (blank) 32bit DIB section
	const HBITMAP hDIB = CreateDIBSection (hdc_c, (LPBITMAPINFO)&dib.dsBmih, DIB_RGB_COLORS, (LPVOID*)&dest, 0, 0);

	// copy each pixel
	for (int i = 0; i < lpbih->biWidth * lpbih->biHeight; i++)
	{
		// convert 8bit palette entry to 32bit colour
		const RGBQUAD rgb = pal[*src++];
		const COLORREF col = RGB (rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue);

		// convert the RGB colour to H,S,L values
		double h, s, l;
		RGBtoHSL (col, &h, &s, &l);

		// create the new colour 
		*dest++ = HSLtoRGB (hue, s, l);
	}

	DeleteObject (hBmp);
	DeleteDC (hdc_c);

	return hDIB;
}

void SetMatrixBitmap (HDC hdc, MATRIX *matrix, INT hue)
{
	hue %= 255;

	// create the new bitmap
	const HBITMAP hBmp = MakeBitmap (hdc, app.GetHINSTANCE (), IDB_GLYPH, hue / 255.0);
	DeleteObject (SelectObject (matrix->hdcBitmap, hBmp));

	matrix->hbmBitmap = hBmp;
	SelectObject (matrix->hdcBitmap, matrix->hbmBitmap);
}

void DecodeMatrix (HWND hwnd, MATRIX *matrix)
{
	const HDC hdc = GetDC (hwnd);

	for (int x = 0; x < matrix->numcols; x++)
	{
		RandomMatrixColumn (&matrix->column[x]);
		ScrollMatrixColumn (&matrix->column[x]);
		RedrawMatrixColumn (&matrix->column[x], matrix, hdc, x * GLYPH_WIDTH);
	}

	if (app.ConfigGet (L"Random", HUE_RANDOM).AsBool ())
		nHue = (UINT)_r_rand (HUE_MIN, HUE_MAX);

	SetMatrixBitmap (hdc, matrix, nHue);

	ReleaseDC (hwnd, hdc);
}

void RefreshMatrix (HWND hwnd)
{
	MATRIX *matrix = (MATRIX*)GetWindowLongPtr (hwnd, GWLP_USERDATA);

	if (!matrix)
		return;

	for (int x = 0; x < matrix->numcols; x++)
	{
		for (int y = 0; y < matrix->column[x].length; y++)
			matrix->column[x].glyph[y] |= GLYPH_REDRAW;
	}

	SendMessage (hwnd, WM_TIMER, 0, 0);
}

//
//	Allocate matrix structures
//
MATRIX *CreateMatrix (int width, int height)
{
	const int rows = height / GLYPH_HEIGHT + 1;
	const int cols = width / GLYPH_WIDTH + 1;

	// allocate matrix!
	MATRIX *matrix = (MATRIX*)malloc (sizeof (MATRIX) + (sizeof (MATRIX_COLUMN) * cols));

	if (!matrix)
		return 0;

	matrix->numcols = cols;
	matrix->numrows = rows;
	matrix->width = width;
	matrix->height = height;

	for (int x = 0; x < cols; x++)
	{
		matrix->column[x].length = rows;
		matrix->column[x].started = false;
		matrix->column[x].countdown = crc_rand () % 100;
		matrix->column[x].state = crc_rand () % 2;
		matrix->column[x].runlen = crc_rand () % 20 + 3;

		matrix->column[x].glyph = (GLYPH*)malloc (sizeof (GLYPH) * (rows + 16));

		for (int y = 0; y < rows; y++)
			matrix->column[x].glyph[y] = 0;
	}

	// Load bitmap!!
	const HDC hdc = GetDC (nullptr);

	matrix->hbmBitmap = MakeBitmap (hdc, app.GetHINSTANCE (), IDB_GLYPH, (double)nHue / 255.0);
	matrix->hdcBitmap = CreateCompatibleDC (hdc);
	SelectObject (matrix->hdcBitmap, matrix->hbmBitmap);
	ReleaseDC (nullptr, hdc);

	return matrix;
}

//
//	Free up matrix structures
//
void DestroyMatrix (MATRIX *matrix)
{
	// free the matrix columns
	for (int x = 0; x < matrix->numcols; x++)
		free (matrix->column[x].glyph);

	DeleteDC (matrix->hdcBitmap);
	DeleteObject (matrix->hbmBitmap);

	// now delete the matrix!
	free (matrix);
}

LRESULT CALLBACK ScreensaverProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	MATRIX* matrix = (MATRIX*)GetWindowLongPtr (hwnd, GWLP_USERDATA);

	static POINT ptLast = {0};
	static POINT ptCursor = {0};
	static bool fFirstTime = true;

	switch (msg)
	{
		case WM_NCCREATE:
		{
			if (!hmatrix)
				hmatrix = hwnd;

			matrix = CreateMatrix ((LPCREATESTRUCT (lparam)->cx), (LPCREATESTRUCT (lparam)->cy));

			if (!matrix)
				return FALSE;

			SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR)matrix);
			SetTimer (hwnd, UID, ((SPEED_MAX - app.ConfigGet (L"Speed", SPEED_DEFAULT).AsUint ()) + SPEED_MIN) * 10, 0);

			return TRUE;
		}

		case WM_NCDESTROY:
		{
			DestroyMatrix (matrix);
			PostQuitMessage (0);

			return FALSE;
		}

		case WM_TIMER:
		{
			DecodeMatrix (hwnd, matrix);
			return FALSE;
		}

		case WM_CLOSE:
		{
			//if (VerifyPassword (hwnd))
			{
				hmatrix = nullptr;

				KillTimer (hwnd, UID);
				DestroyWindow (hwnd);
			}

			return FALSE;
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			PostMessage (hwnd, WM_CLOSE, 0, 0);
			return FALSE;
		}

		case WM_MOUSEMOVE:
		{
			if (GetParent (hwnd))
				return FALSE;

			if (fFirstTime)
			{
				GetCursorPos (&ptLast);
				fFirstTime = false;
			}

			GetCursorPos (&ptCursor);

			if (
				abs (ptCursor.x - ptLast.x) >= GetSystemMetrics (SM_CXSMICON) / 2 ||
				abs (ptCursor.y - ptLast.y) >= GetSystemMetrics (SM_CYSMICON) / 2
				)
			{
				PostMessage (hwnd, WM_CLOSE, 0, 0);
			}

			ptLast = ptCursor;

			return FALSE;
		}

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		{
			if (GetParent (hwnd))
				return FALSE;

			PostMessage (hwnd, WM_CLOSE, 0, 0);

			break;
		}
	}

	return DefWindowProc (hwnd, msg, wparam, lparam);
}

BOOL CALLBACK MonitorEnumProc (HMONITOR, HDC, LPRECT lprc, LPARAM lparam)
{
	const HWND hparent = (HWND)lparam;
	const DWORD style = hparent ? WS_CHILD : WS_POPUP;

	const HWND hwnd = CreateWindowEx (WS_EX_TOPMOST, APP_NAME_SHORT, APP_NAME, WS_VISIBLE | style, lprc->left, lprc->top, _R_RECT_WIDTH (lprc), _R_RECT_HEIGHT (lprc), hparent, nullptr, app.GetHINSTANCE (), nullptr);

	if (hwnd)
	{
		SetWindowPos (hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	}

	return TRUE;
}

void StartScreensaver (HWND hparent)
{
	MSG msg = {0};
	UINT state = 0;

	if (!hparent)
		SystemParametersInfo (SPI_SETSCREENSAVERRUNNING, TRUE, &state, SPIF_SENDCHANGE);

	if (!hparent)
	{
		EnumDisplayMonitors (nullptr, nullptr, &MonitorEnumProc, 0);
	}
	else
	{
		RECT rc = {0};
		GetClientRect (hparent, &rc);

		MonitorEnumProc (nullptr, nullptr, &rc, (LPARAM)hparent);
	}

	if (!hparent)
		SystemParametersInfo (SPI_SETSCREENSAVERRUNNING, FALSE, &state, SPIF_SENDCHANGE);
}

INT_PTR CALLBACK SettingsProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	static HWND hpreview = nullptr;

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			hpreview = GetDlgItem (hwnd, IDC_PREVIEW);

			// localize window
			SetWindowText (hwnd, APP_NAME);

			_r_ctrl_settext (hwnd, IDC_AMOUNT_RANGE, L"%d-%d", AMOUNT_MIN, AMOUNT_MAX);
			_r_ctrl_settext (hwnd, IDC_DENSITY_RANGE, L"%d-%d", DENSITY_MIN, DENSITY_MAX);
			_r_ctrl_settext (hwnd, IDC_SPEED_RANGE, L"%d-%d", SPEED_MIN, SPEED_MAX);
			_r_ctrl_settext (hwnd, IDC_HUE_RANGE, L"%d-%d", HUE_MIN, HUE_MAX);

			SendDlgItemMessage (hwnd, IDC_AMOUNT, UDM_SETRANGE32, AMOUNT_MIN, AMOUNT_MAX);
			SendDlgItemMessage (hwnd, IDC_AMOUNT, UDM_SETPOS32, 0, app.ConfigGet (L"NumGlyphs", AMOUNT_DEFAULT).AsUint ());

			SendDlgItemMessage (hwnd, IDC_DENSITY, UDM_SETRANGE32, DENSITY_MIN, DENSITY_MAX);
			SendDlgItemMessage (hwnd, IDC_DENSITY, UDM_SETPOS32, 0, app.ConfigGet (L"Density", DENSITY_DEFAULT).AsUint ());

			SendDlgItemMessage (hwnd, IDC_SPEED, UDM_SETRANGE32, SPEED_MIN, SPEED_MAX);
			SendDlgItemMessage (hwnd, IDC_SPEED, UDM_SETPOS32, 0, app.ConfigGet (L"Speed", SPEED_DEFAULT).AsUint ());

			SendDlgItemMessage (hwnd, IDC_HUE, UDM_SETRANGE32, HUE_MIN, HUE_MAX);
			SendDlgItemMessage (hwnd, IDC_HUE, UDM_SETPOS32, 0, app.ConfigGet (L"Hue", HUE_DEFAULT).AsUint ());

			CheckDlgButton (hwnd, IDC_RANDOMIZECOLORS_CHK, app.ConfigGet (L"Random", HUE_RANDOM).AsBool ());

			SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_RANDOMIZECOLORS_CHK, 0), 0);

			_r_ctrl_settext (hwnd, IDC_ABOUT, L"<a href=\"%s\">Website</a> | <a href=\"%s\">Github</a>", _APP_WEBSITE_URL, _APP_GITHUB_URL);

			_r_wnd_addstyle (hwnd, IDC_RESET, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
			_r_wnd_addstyle (hwnd, IDC_CLOSE, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

			StartScreensaver (hpreview);

			break;
		}

		case WM_CLOSE:
		{
			DestroyWindow (hwnd);
			break;
		}

		case WM_DESTROY:
		{
			PostQuitMessage (0);
			break;
		}

		case WM_CTLCOLORSTATIC:
		{
			const UINT ctrl_id = GetDlgCtrlID ((HWND)lparam);

			if (
				ctrl_id == IDC_AMOUNT_RANGE ||
				ctrl_id == IDC_DENSITY_RANGE ||
				ctrl_id == IDC_SPEED_RANGE ||
				ctrl_id == IDC_HUE_RANGE
				)
			{
				SetBkMode ((HDC)wparam, TRANSPARENT); // background-hack
				SetTextColor ((HDC)wparam, GetSysColor (COLOR_GRAYTEXT));

				return (INT_PTR)GetSysColorBrush (COLOR_BTNFACE);
			}

			break;
		}

		case WM_VSCROLL:
		case WM_HSCROLL:
		{
			const UINT ctrl_id = GetDlgCtrlID ((HWND)lparam);

			SendMessage (hwnd, WM_COMMAND, MAKEWORD (ctrl_id - 1, 0), 0);

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case NM_CLICK:
				case NM_RETURN:
				{
					PNMLINK nmlink = (PNMLINK)lparam;

					if (nmlink->item.szUrl[0] && nmlp->idFrom == IDC_ABOUT)
					{
						if (nmlink->item.szUrl && nmlink->item.szUrl[0])
							ShellExecute (hwnd, nullptr, nmlink->item.szUrl, nullptr, nullptr, SW_SHOWDEFAULT);
					}

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			switch (LOWORD (wparam))
			{
				case IDCANCEL: // process Esc key
				case IDC_CLOSE:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDC_RESET:
				{
					if (_r_msg (hwnd, MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON2, APP_NAME, nullptr, L"Are you really sure you want to reset all application settings?") == IDYES)
					{
						app.ConfigSet (L"NumGlyphs", (DWORD)AMOUNT_DEFAULT);
						nAmount = AMOUNT_DEFAULT;

						app.ConfigSet (L"Density", (DWORD)DENSITY_DEFAULT);
						nDensity = DENSITY_DEFAULT;

						app.ConfigSet (L"Hue", (DWORD)HUE_DEFAULT);
						nHue = HUE_DEFAULT;

						app.ConfigSet (L"Random", HUE_RANDOM);
						CheckDlgButton (hwnd, IDC_RANDOMIZECOLORS_CHK, HUE_RANDOM ? BST_CHECKED : BST_UNCHECKED);
						SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_RANDOMIZECOLORS_CHK, 0), 0);

						app.ConfigSet (L"Speed", (DWORD)SPEED_DEFAULT);

						SendDlgItemMessage (hwnd, IDC_AMOUNT, UDM_SETPOS32, 0, AMOUNT_DEFAULT);
						SendDlgItemMessage (hwnd, IDC_DENSITY, UDM_SETPOS32, 0, DENSITY_DEFAULT);
						SendDlgItemMessage (hwnd, IDC_SPEED, UDM_SETPOS32, 0, SPEED_DEFAULT);
						SendDlgItemMessage (hwnd, IDC_HUE, UDM_SETPOS32, 0, HUE_DEFAULT);

						KillTimer (hmatrix, UID);
						SetTimer (hmatrix, UID, ((SPEED_MAX - SPEED_DEFAULT) + SPEED_MIN) * 10, 0);
					}

					break;
				}

				case IDC_SHOW:
				{
					RECT rc = {0};

					POINT pt = {0};
					GetCursorPos (&pt);

					MONITORINFO monitorInfo = {0};
					monitorInfo.cbSize = sizeof (monitorInfo);

					const HMONITOR hmonitor = MonitorFromPoint (pt, MONITOR_DEFAULTTONEAREST);

					if (hmonitor)
					{
						if (GetMonitorInfo (hmonitor, &monitorInfo))
						{
							CopyRect (&rc, &monitorInfo.rcMonitor);
						}
					}
					else
					{
						rc.right = GetSystemMetrics (SM_CXFULLSCREEN);
						rc.bottom = GetSystemMetrics (SM_CYFULLSCREEN);
					}

					MonitorEnumProc (nullptr, nullptr, &rc, 0);

					break;
				}

				case IDC_AMOUNT_CTRL:
				{
					const DWORD new_value = (DWORD)SendDlgItemMessage (hwnd, IDC_AMOUNT, UDM_GETPOS32, 0, 0);

					app.ConfigSet (L"NumGlyphs", new_value);
					nAmount = new_value;

					break;
				}

				case IDC_DENSITY_CTRL:
				{
					const DWORD new_value = (DWORD)SendDlgItemMessage (hwnd, IDC_DENSITY, UDM_GETPOS32, 0, 0);

					app.ConfigSet (L"Density", new_value);
					nDensity = new_value;

					break;
				}

				case IDC_SPEED_CTRL:
				{
					const DWORD new_value = (DWORD)SendDlgItemMessage (hwnd, IDC_SPEED, UDM_GETPOS32, 0, 0);

					app.ConfigSet (L"Speed", new_value);

					KillTimer (hmatrix, UID);
					SetTimer (hmatrix, UID, ((SPEED_MAX - new_value) + SPEED_MIN) * 10, 0);

					break;
				}

				case IDC_HUE_CTRL:
				{
					const DWORD new_value = (DWORD)SendDlgItemMessage (hwnd, IDC_HUE, UDM_GETPOS32, 0, 0);

					app.ConfigSet (L"Hue", new_value);
					nHue = new_value;

					break;
				}

				case IDC_RANDOMIZECOLORS_CHK:
				{
					const bool is_enabled = (IsDlgButtonChecked (hwnd, LOWORD (wparam)) == BST_CHECKED);

					_r_ctrl_enable (hwnd, IDC_HUE_CTRL, !is_enabled);
					_r_ctrl_enable (hwnd, IDC_HUE, !is_enabled);

					app.ConfigSet (L"Random", (IsDlgButtonChecked (hwnd, LOWORD (wparam)) == BST_CHECKED));
					nHue = app.ConfigGet (L"Hue", HUE_DEFAULT).AsUint (); // reset hue

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (HINSTANCE hinst, HINSTANCE, LPWSTR cmdline, INT)
{
	_crc_reg = _r_sys_gettickcount ();

	// init controls
	{
		INITCOMMONCONTROLSEX icex = {0};

		icex.dwSize = sizeof (icex);
		icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;

		InitCommonControlsEx (&icex);
	}

	// register class
	{
		WNDCLASSEX wcex = {0};

		wcex.cbSize = sizeof (wcex);
		wcex.hInstance = hinst;
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpszClassName = APP_NAME_SHORT;
		wcex.lpfnWndProc = &ScreensaverProc;
		wcex.hbrBackground = (HBRUSH)GetStockObject (BLACK_BRUSH);
		wcex.hCursor = ((_wcsnicmp (cmdline, L"/s", 2) != 0) ? LoadCursor (nullptr, IDC_ARROW) : LoadCursor (hinst, MAKEINTRESOURCE (IDC_CURSOR)));
		wcex.cbWndExtra = sizeof (MATRIX*);

		RegisterClassEx (&wcex);
	}

	// parse arguments
	{
		MSG msg = {0};

		if (_wcsnicmp (cmdline, L"/s", 2) == 0)
		{
			StartScreensaver (nullptr);

			while (GetMessage (&msg, nullptr, 0, 0))
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}
		else if (_wcsnicmp (cmdline, L"/p", 2) == 0)
		{
			const HWND hctrl = (HWND)wcstoll (LPCWSTR (cmdline + 3), nullptr, 10);

			if (hctrl)
			{
				StartScreensaver (hctrl);

				while (GetMessage (&msg, nullptr, 0, 0))
				{
					TranslateMessage (&msg);
					DispatchMessage (&msg);
				}
			}
		}
		else
		{
			if (app.CreateMainWindow (IDD_SETTINGS, IDI_MAIN, &SettingsProc))
			{
				while (GetMessage (&msg, nullptr, 0, 0) > 0)
				{
					if (!IsDialogMessage (app.GetHWND (), &msg))
					{
						TranslateMessage (&msg);
						DispatchMessage (&msg);
					}
				}
			}
		}

		UnregisterClass (APP_NAME_SHORT, hinst);
	}

	return ERROR_SUCCESS;
}
