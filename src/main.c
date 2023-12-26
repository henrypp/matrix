// Matrix Screensaver
// Copyright (c) J Brown 2003 (catch22.net)
// Copyright (c) 2011-2023 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"

STATIC_DATA config = {0};

#define RND_MAX INT_MAX

VOID ReadSettings ()
{
	config.speed = _r_config_getlong (L"Speed", SPEED_DEFAULT);
	config.amount = _r_config_getlong (L"NumGlyphs", AMOUNT_DEFAULT);
	config.density = _r_config_getlong (L"Density", DENSITY_DEFAULT);
	config.hue = _r_config_getlong (L"Hue", HUE_DEFAULT);

	config.is_esc_only = _r_config_getboolean (L"IsEscOnly", FALSE);

	config.is_random = _r_config_getboolean (L"Random", HUE_RANDOM);
	config.is_smooth = _r_config_getboolean (L"RandomSmoothTransition", HUE_RANDOM_SMOOTHTRANSITION);
}

VOID SaveSettings ()
{
	_r_config_setlong (L"Speed", config.speed);
	_r_config_setlong (L"NumGlyphs", config.amount);
	_r_config_setlong (L"Density", config.density);
	_r_config_setlong (L"Hue", config.hue);

	_r_config_setboolean (L"IsEscOnly", config.is_esc_only);

	_r_config_setboolean (L"Random", config.is_random);
	_r_config_setboolean (L"RandomSmoothTransition", config.is_smooth);
}

FORCEINLINE COLORREF HSLtoRGB (
	_In_ WORD h,
	_In_ WORD s,
	_In_ WORD l
)
{
	return ColorHLSToRGB (h, l, s);
}

FORCEINLINE VOID RGBtoHSL (
	_In_ COLORREF clr,
	_Out_ PWORD h,
	_Out_ PWORD s,
	_Out_ PWORD l
)
{
	ColorRGBToHLS (clr, h, l, s);
}

FORCEINLINE GLYPH GlyphIntensity (
	_In_ GLYPH glyph
)
{
	return ((glyph & 0x7F00) >> 8);
}

FORCEINLINE GLYPH RandomGlyph (
	_In_ INT intensity
)
{
	return GLYPH_REDRAW | (intensity << 8) | (_r_math_getrandomrange (0, RND_MAX) % config.amount);
}

FORCEINLINE GLYPH DarkenGlyph (
	_In_ GLYPH glyph
)
{
	GLYPH intensity;

	intensity = GlyphIntensity (glyph);

	if (intensity > 0)
		return GLYPH_REDRAW | ((intensity - 1) << 8) | (glyph & 0x00FF);

	return glyph;
}

FORCEINLINE VOID DrawGlyph (
	_In_ PMATRIX matrix,
	_In_ HDC hdc,
	_In_ ULONG xpos,
	_In_ ULONG ypos,
	_In_ GLYPH glyph
)
{
	GLYPH intensity;
	ULONG glyph_idx;

	intensity = GlyphIntensity (glyph);
	glyph_idx = glyph & 0xFF;

	BitBlt (
		hdc,
		xpos,
		ypos,
		GLYPH_WIDTH,
		GLYPH_HEIGHT,
		matrix->hdc,
		glyph_idx * GLYPH_WIDTH,
		intensity * GLYPH_HEIGHT,
		SRCCOPY
	);
}

FORCEINLINE VOID RedrawBlip (
	_Inout_ PGLYPH glyph_arr,
	_In_ INT blip_pos
)
{
	glyph_arr[blip_pos + 0] |= GLYPH_REDRAW;
	glyph_arr[blip_pos + 1] |= GLYPH_REDRAW;
	glyph_arr[blip_pos + 8] |= GLYPH_REDRAW;
	glyph_arr[blip_pos + 9] |= GLYPH_REDRAW;
}

VOID ScrollMatrixColumn (
	_Inout_ PMATRIX_COLUMN column
)
{
	GLYPH last_glyph;
	GLYPH current_glyph;
	GLYPH current_glyph_intensity;
	LONG density;

	// wait until we are allowed to scroll
	if (!column->is_started)
	{
		if (--column->countdown <= 0)
			column->is_started = TRUE;

		return;
	}

	// "seed" the glyph-run
	last_glyph = column->state ? 0 : (MAX_INTENSITY << 8);

	//
	// loop over the entire length of the column, looking for changes
	// in intensity/darkness. This change signifies the start/end
	// of a run of glyphs.
	//
	for (ULONG_PTR y = 0; y < (ULONG_PTR)column->length; y++)
	{
		current_glyph = column->glyph[y];

		current_glyph_intensity = GlyphIntensity (current_glyph);

		// bottom-most part of "run". Insert a new character (glyph)
		// at the end to lengthen the run down the screen..gives the
		// impression that the run is "falling" down the screen
		if (current_glyph_intensity < GlyphIntensity (last_glyph) && current_glyph_intensity == 0)
		{
			column->glyph[y] = RandomGlyph (MAX_INTENSITY - 1);

			y += 1;
		}

		// top-most part of "run". Delete a character off the top by
		// darkening the glyph until it eventually disappears (turns black). 
		// this gives the effect that the run as dropped downwards
		else if (current_glyph_intensity > GlyphIntensity (last_glyph))
		{
			column->glyph[y] = DarkenGlyph (current_glyph);

			// if we've just darkened the last bit, skip on so
			// the whole run doesn't go dark
			if (current_glyph_intensity == MAX_INTENSITY - 1)
				y++;
		}

		last_glyph = column->glyph[y];
	}

	// change state from blanks <-> runs when the current run as expired
	if (--column->run_length <= 0)
	{
		density = DENSITY_MAX - config.density + DENSITY_MIN;

		if (column->state ^= 1)
		{
			column->run_length = _r_math_getrandomrange (0, RND_MAX) % (3 * density / 2) + DENSITY_MIN;
		}
		else
		{
			column->run_length = _r_math_getrandomrange (0, RND_MAX) % (DENSITY_MAX + 1 - density) + (DENSITY_MIN * 2);
		}
	}

	// mark current blip as redraw so it gets "erased"
	if (column->blip_pos >= 0 && column->blip_pos < column->length)
		RedrawBlip (column->glyph, column->blip_pos);

	// advance down screen at double-speed
	column->blip_pos += 2;

	// if the blip gets to the end of a run, start it again (for a random
	// length so that the blips never get synched together)
	if (column->blip_pos >= column->blip_length)
	{
		column->blip_length = column->length + (_r_math_getrandomrange (0, RND_MAX) % 50);
		column->blip_pos = 0;
	}

	// now redraw blip at new position
	if (column->blip_pos >= 0 && column->blip_pos < column->length)
		RedrawBlip (column->glyph, column->blip_pos);
}

//
// randomly change a small collection glyphs in a column
//
VOID RandomMatrixColumn (
	_Inout_ PMATRIX_COLUMN column
)
{
	ULONG rand;

	for (ULONG_PTR i = 1, y = 0; i < 16; i++)
	{
		// find a run
		while (y < column->length && GlyphIntensity (column->glyph[y]) < (MAX_INTENSITY - 1))
			y += 1;

		if (y >= column->length)
			break;

		rand = _r_math_getrandomrange (0, RND_MAX);

		column->glyph[y] = (column->glyph[y] & 0xFF00) | (rand % config.amount);
		column->glyph[y] |= GLYPH_REDRAW;

		y += rand % 10;
	}
}

VOID RedrawMatrixColumn (
	_Inout_ PMATRIX_COLUMN column,
	_In_ PMATRIX matrix,
	_In_ HDC hdc,
	_In_ ULONG xpos
)
{
	GLYPH glyph;

	// loop down the length of the column redrawing only what needs doing
	for (ULONG_PTR i = 0; i < column->length; i++)
	{
		glyph = column->glyph[i];

		// does this glyph (character) need to be redrawn?
		if (glyph & GLYPH_REDRAW)
		{
			if ((GlyphIntensity (glyph) >= MAX_INTENSITY - 1) && (i == column->blip_pos + 0 || i == column->blip_pos + 1 || i == column->blip_pos + 8 || i == column->blip_pos + 9))
				glyph |= MAX_INTENSITY << 8;

			DrawGlyph (matrix, hdc, xpos, (ULONG)i * GLYPH_HEIGHT, glyph);

			// clear redraw state
			column->glyph[i] &= ~GLYPH_REDRAW;
		}
	}
}

HBITMAP MakeBitmap (
	_In_ HDC hdc,
	_In_ HINSTANCE hinst,
	_In_ UINT type,
	_In_ LONG hue
)
{
	RGBQUAD pal[256] = {0};
	DIBSECTION dib = {0};
	LPBITMAPINFOHEADER lpbih;
	PULONG dest = NULL;
	PBYTE src;
	HDC hdc_c;
	HBITMAP hglyph;
	HBITMAP hdib;
	HANDLE hbitmap_old;
	RGBQUAD rgb;
	COLORREF clr;
	WORD h, s, l;

	// load the 8bit image
	hglyph = LoadImageW (hinst, MAKEINTRESOURCE (IDR_GLYPH), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

	// extract the colour table
	hdc_c = CreateCompatibleDC (hdc);
	hbitmap_old = SelectObject (hdc_c, hglyph);

	GetDIBColorTable (hdc_c, 0, RTL_NUMBER_OF (pal), pal);
	SelectObject (hdc_c, hbitmap_old);

	GetObjectW (hglyph, sizeof (dib), &dib);

	src = dib.dsBm.bmBits;
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
	hdib = CreateDIBSection (hdc_c, (LPBITMAPINFO)&dib.dsBmih, DIB_RGB_COLORS, &dest, 0, 0);

	// copy each pixel
	for (LONG i = 0; i < lpbih->biWidth * lpbih->biHeight; i++)
	{
		// convert 8bit palette entry to 32bit colour
		rgb = pal[*src++];
		clr = RGB (rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue);

		// convert the RGB colour to H,S,L values
		RGBtoHSL (clr, &h, &s, &l);

		// create the new colour
		*dest++ = HSLtoRGB ((WORD)hue, s, l);
	}

	DeleteObject (hglyph);
	DeleteDC (hdc_c);

	return hdib;
}

VOID SetMatrixBitmap (
	_In_ HDC hdc,
	_Inout_ PMATRIX matrix,
	_In_ INT hue
)
{
	HBITMAP hbitmap;
	HBITMAP hbitmap_old;

	hbitmap = MakeBitmap (hdc, _r_sys_getimagebase (), IDR_GLYPH, hue);

	if (!hbitmap)
		return;

	hbitmap_old = SelectObject (matrix->hdc, hbitmap);

	if (hbitmap_old)
		DeleteObject (hbitmap_old);

	matrix->hbitmap = hbitmap;

	SelectObject (matrix->hdc, matrix->hbitmap);
}

VOID DecodeMatrix (
	_In_ HWND hwnd,
	_In_ PMATRIX matrix
)
{
	static LONG new_hue = 0;

	PMATRIX_COLUMN column;
	HDC hdc;

	hdc = GetDC (hwnd);

	if (!hdc)
		return;

	if (!new_hue)
		new_hue = config.hue;

	for (ULONG_PTR x = 0; x < matrix->numcols; x++)
	{
		column = &matrix->column[x];

		RandomMatrixColumn (column);
		ScrollMatrixColumn (column);
		RedrawMatrixColumn (column, matrix, hdc, (ULONG)x * GLYPH_WIDTH);
	}

	if (config.is_random)
	{
		if (config.is_smooth)
		{
			new_hue = (new_hue >= HUE_MAX) ? HUE_MIN : new_hue + 1;
		}
		else
		{
			if (_r_sys_gettickcount () % 2)
				new_hue = _r_math_getrandomrange (HUE_MIN, HUE_MAX);
		}
	}
	else
	{
		new_hue = config.hue;
	}

	SetMatrixBitmap (hdc, matrix, new_hue);

	ReleaseDC (hwnd, hdc);
}

PMATRIX CreateMatrix (
	_In_ ULONG width,
	_In_ ULONG height
)
{
	PMATRIX matrix;
	HDC hdc;
	ULONG numcols;
	ULONG numrows;

	numcols = width / GLYPH_WIDTH + 1;
	numrows = height / GLYPH_HEIGHT + 1;

	matrix = _r_mem_allocate (sizeof (MATRIX) + (sizeof (MATRIX_COLUMN) * numcols));

	matrix->numcols = numcols;
	matrix->numrows = numrows;
	matrix->width = width;
	matrix->height = height;

	for (ULONG x = 0; x < numcols; x++)
	{
		matrix->column[x].length = numrows;
		matrix->column[x].countdown = _r_math_getrandomrange (0, RND_MAX) % 100;
		matrix->column[x].state = _r_math_getrandomrange (0, RND_MAX) % 2;
		matrix->column[x].run_length = _r_math_getrandomrange (0, RND_MAX) % 20 + 3;

		matrix->column[x].glyph = _r_mem_allocate (sizeof (GLYPH) * (numrows + 16));
	}

	hdc = GetDC (NULL);

	if (hdc)
	{
		matrix->hdc = CreateCompatibleDC (hdc);
		matrix->hbitmap = MakeBitmap (hdc, _r_sys_getimagebase (), IDR_GLYPH, config.hue);

		SelectObject (matrix->hdc, matrix->hbitmap);

		ReleaseDC (NULL, hdc);
	}

	return matrix;
}

VOID DestroyMatrix (
	_Inout_ PMATRIX *matrix
)
{
	PMATRIX old_matrix;
	PGLYPH glyph;

	old_matrix = *matrix;
	*matrix = NULL;

	DeleteDC (old_matrix->hdc);
	DeleteObject (old_matrix->hbitmap);

	for (ULONG_PTR x = 0; x < old_matrix->numcols; x++)
	{
		glyph = old_matrix->column[x].glyph;

		if (glyph)
		{
			old_matrix->column[x].glyph = NULL;

			_r_mem_free (glyph);
		}
	}

	_r_mem_free (old_matrix);
}

LRESULT CALLBACK ScreensaverProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	static POINT pt_last = {0};
	static POINT pt_cursor = {0};
	static BOOLEAN is_savecursor = FALSE;

	PMATRIX matrix;

	switch (msg)
	{
		case WM_NCCREATE:
		{
			LPCREATESTRUCT pcs;

			pcs = (LPCREATESTRUCT)lparam;

			matrix = CreateMatrix (pcs->cx, pcs->cy);

			if (!matrix)
				return FALSE;

			SetWindowLongPtrW (hwnd, GWLP_USERDATA, (LONG_PTR)matrix);
			SetTimer (hwnd, UID, ((SPEED_MAX - config.speed) + SPEED_MIN) * 10, 0);

			return TRUE;
		}

		case WM_NCDESTROY:
		{
			KillTimer (hwnd, UID);
			is_savecursor = FALSE;

			matrix = (PMATRIX)GetWindowLongPtr (hwnd, GWLP_USERDATA);

			if (matrix)
			{
				SetWindowLongPtrW (hwnd, GWLP_USERDATA, 0);

				DestroyMatrix (&matrix);
			}

			if (config.is_preview && !GetParent (hwnd))
				return FALSE;

			PostQuitMessage (0);

			return FALSE;
		}

		case WM_CLOSE:
		{
			DestroyWindow (hwnd);

			return FALSE;
		}

		case WM_TIMER:
		{
			matrix = (PMATRIX)GetWindowLongPtr (hwnd, GWLP_USERDATA);

			if (matrix)
				DecodeMatrix (hwnd, matrix);

			return FALSE;
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			if (wparam != VK_ESCAPE && config.is_esc_only)
				return FALSE;

			DestroyWindow (hwnd);

			return FALSE;
		}

		case WM_MOUSEMOVE:
		{
			LONG icon_size;

			if (GetParent (hwnd) || config.is_esc_only)
				return FALSE;

			if (!is_savecursor)
			{
				GetCursorPos (&pt_last);

				is_savecursor = TRUE;
			}

			GetCursorPos (&pt_cursor);

			icon_size = _r_dc_getsystemmetrics (SM_CXSMICON, _r_dc_getwindowdpi (hwnd));

			if (abs (pt_cursor.x - pt_last.x) >= (icon_size / 2) || abs (pt_cursor.y - pt_last.y) >= (icon_size / 2))
			{
				DestroyWindow (hwnd);

				return FALSE;
			}

			pt_last = pt_cursor;

			return FALSE;
		}

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		{
			if (GetParent (hwnd) || config.is_esc_only)
				return FALSE;

			DestroyWindow (hwnd);

			break;
		}
	}

	return DefWindowProc (hwnd, msg, wparam, lparam);
}

BOOL CALLBACK MonitorEnumProc (
	_In_opt_ HMONITOR hmonitor,
	_In_opt_ HDC hdc,
	_In_ PRECT rect,
	_In_opt_ LPARAM lparam
)
{
	HWND hwnd;
	ULONG style;

	style = lparam ? WS_CHILD : WS_POPUP;

	hwnd = CreateWindowEx (
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		lparam ? CLASS_PREVIEW : CLASS_FULLSCREEN,
		_r_app_getname (),
		WS_VISIBLE | style,
		rect->left,
		rect->top,
		_r_calc_rectwidth (rect),
		_r_calc_rectheight (rect),
		(HWND)lparam,
		NULL,
		_r_sys_getimagebase (),
		NULL
	);

	return TRUE;
}

VOID StartScreensaver (
	_In_opt_ HWND hparent
)
{
	RECT rect;
	UINT state = 0;

	if (hparent)
	{
		if (GetClientRect (hparent, &rect))
			MonitorEnumProc (NULL, NULL, &rect, (LPARAM)hparent);
	}
	else
	{
		EnumDisplayMonitors (NULL, NULL, &MonitorEnumProc, 0);
	}
}

INT_PTR CALLBACK SettingsProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			HWND hpreview;

			hpreview = GetDlgItem (hwnd, IDC_PREVIEW);

			// localize window
			_r_ctrl_setstringformat (hwnd, IDC_AMOUNT_RANGE, L"%d-%d", AMOUNT_MIN, AMOUNT_MAX);
			_r_ctrl_setstringformat (hwnd, IDC_DENSITY_RANGE, L"%d-%d", DENSITY_MIN, DENSITY_MAX);
			_r_ctrl_setstringformat (hwnd, IDC_SPEED_RANGE, L"%d-%d", SPEED_MIN, SPEED_MAX);
			_r_ctrl_setstringformat (hwnd, IDC_HUE_RANGE, L"%d-%d", HUE_MIN, HUE_MAX);

			SendDlgItemMessageW (hwnd, IDC_AMOUNT, UDM_SETRANGE32, AMOUNT_MIN, AMOUNT_MAX);
			SendDlgItemMessageW (hwnd, IDC_AMOUNT, UDM_SETPOS32, 0, (LPARAM)config.amount);

			SendDlgItemMessageW (hwnd, IDC_DENSITY, UDM_SETRANGE32, DENSITY_MIN, DENSITY_MAX);
			SendDlgItemMessageW (hwnd, IDC_DENSITY, UDM_SETPOS32, 0, (LPARAM)config.density);

			SendDlgItemMessageW (hwnd, IDC_SPEED, UDM_SETRANGE32, SPEED_MIN, SPEED_MAX);
			SendDlgItemMessageW (hwnd, IDC_SPEED, UDM_SETPOS32, 0, (LPARAM)config.speed);

			SendDlgItemMessageW (hwnd, IDC_HUE, UDM_SETRANGE32, HUE_MIN, HUE_MAX);
			SendDlgItemMessageW (hwnd, IDC_HUE, UDM_SETPOS32, 0, (LPARAM)config.hue);

			CheckDlgButton (hwnd, IDC_RANDOMIZECOLORS_CHK, config.is_random);
			CheckDlgButton (hwnd, IDC_RANDOMIZESMOOTH_CHK, config.is_smooth);
			CheckDlgButton (hwnd, IDC_ISCLOSEONESC_CHK, config.is_esc_only);

			SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_RANDOMIZECOLORS_CHK, 0), 0);
			SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_ISCLOSEONESC_CHK, 0), 0);

			_r_ctrl_setstringformat (hwnd, IDC_ABOUT, L"<a href=\"%s\">Github</a>", _r_app_getwebsite_url ());

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
			SaveSettings ();

			PostQuitMessage (0);

			break;
		}

		case WM_CTLCOLORSTATIC:
		{
			INT ctrl_id = GetDlgCtrlID ((HWND)lparam);

			if (ctrl_id == IDC_AMOUNT_RANGE || ctrl_id == IDC_DENSITY_RANGE || ctrl_id == IDC_SPEED_RANGE || ctrl_id == IDC_HUE_RANGE)
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
			INT ctrl_id = GetDlgCtrlID ((HWND)lparam);

			SendMessage (hwnd, WM_COMMAND, MAKEWORD (ctrl_id - 1, 0), 0);

			break;
		}

		case WM_LBUTTONDOWN:
		{
			PostMessageW (hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
			break;
		}

		case WM_ENTERSIZEMOVE:
		case WM_EXITSIZEMOVE:
		case WM_CAPTURECHANGED:
		{
			LONG_PTR exstyle = GetWindowLongPtr (hwnd, GWL_EXSTYLE);

			if ((exstyle & WS_EX_LAYERED) == 0)
				SetWindowLongPtrW (hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);

			SetLayeredWindowAttributes (hwnd, 0, (msg == WM_ENTERSIZEMOVE) ? 100 : 255, LWA_ALPHA);
			SetCursor (LoadCursor (NULL, (msg == WM_ENTERSIZEMOVE) ? IDC_SIZEALL : IDC_ARROW));

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

					if (!_r_str_isempty (nmlink->item.szUrl))
						_r_shell_opendefault (nmlink->item.szUrl);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);

			switch (ctrl_id)
			{
				//case IDCANCEL: // process Esc key
				case IDC_CLOSE:
				{
					PostMessageW (hwnd, WM_CLOSE, 0, 0);
					break;
				}

				case IDC_RESET:
				{
					if (_r_show_message (hwnd, MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON2, NULL, L"Are you really sure you want to reset all application settings?") != IDYES)
						break;

					ReadSettings ();

					CheckDlgButton (hwnd, IDC_RANDOMIZECOLORS_CHK, config.is_random ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_RANDOMIZESMOOTH_CHK, config.is_smooth ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_ISCLOSEONESC_CHK, config.is_esc_only ? BST_CHECKED : BST_UNCHECKED);

					SendDlgItemMessageW (hwnd, IDC_AMOUNT, UDM_SETPOS32, 0, AMOUNT_DEFAULT);
					SendDlgItemMessageW (hwnd, IDC_DENSITY, UDM_SETPOS32, 0, DENSITY_DEFAULT);
					SendDlgItemMessageW (hwnd, IDC_SPEED, UDM_SETPOS32, 0, SPEED_DEFAULT);
					SendDlgItemMessageW (hwnd, IDC_HUE, UDM_SETPOS32, 0, HUE_DEFAULT);

					PostMessageW (hwnd, WM_COMMAND, MAKEWPARAM (IDC_RANDOMIZECOLORS_CHK, 0), 0);
					PostMessageW (hwnd, WM_COMMAND, MAKEWPARAM (IDC_ISCLOSEONESC_CHK, 0), 0);

					break;
				}

				case IDC_SHOW:
				{
					StartScreensaver (NULL);
					break;
				}

				case IDC_AMOUNT_CTRL:
				{
					config.amount = (LONG)SendDlgItemMessageW (hwnd, IDC_AMOUNT, UDM_GETPOS32, 0, 0);
					break;
				}

				case IDC_DENSITY_CTRL:
				{
					config.density = (LONG)SendDlgItemMessageW (hwnd, IDC_DENSITY, UDM_GETPOS32, 0, 0);
					break;
				}

				case IDC_SPEED_CTRL:
				{
					LONG new_value;

					new_value = (LONG)SendDlgItemMessageW (hwnd, IDC_SPEED, UDM_GETPOS32, 0, 0);

					config.speed = new_value;

					break;
				}

				case IDC_HUE_CTRL:
				{
					LONG new_value;

					new_value = (LONG)SendDlgItemMessageW (hwnd, IDC_HUE, UDM_GETPOS32, 0, 0);

					config.hue = new_value;

					break;
				}

				case IDC_RANDOMIZECOLORS_CHK:
				{
					BOOLEAN is_enabled;

					is_enabled = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);

					_r_ctrl_enable (hwnd, IDC_HUE_CTRL, !is_enabled);
					_r_ctrl_enable (hwnd, IDC_HUE, !is_enabled);
					_r_ctrl_enable (hwnd, IDC_RANDOMIZESMOOTH_CHK, is_enabled);

					config.is_random = is_enabled;

					break;
				}

				case IDC_RANDOMIZESMOOTH_CHK:
				{
					config.is_smooth = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);
					break;
				}

				case IDC_ISCLOSEONESC_CHK:
				{
					config.is_esc_only = _r_ctrl_isbuttonchecked (hwnd, ctrl_id);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

BOOLEAN RegisterClasses (
	_In_ HINSTANCE hinst
)
{
	WNDCLASSEX wcex = {0};

	wcex.cbSize = sizeof (wcex);
	wcex.hInstance = hinst;
	wcex.style = CS_VREDRAW | CS_HREDRAW | CS_SAVEBITS | CS_PARENTDC;
	wcex.lpfnWndProc = &ScreensaverProc;
	wcex.hbrBackground = GetStockObject (BLACK_BRUSH);
	wcex.cbWndExtra = sizeof (PMATRIX);

	wcex.lpszClassName = CLASS_PREVIEW;
	wcex.hCursor = LoadCursorW (NULL, IDC_ARROW);

	if (!RegisterClassExW (&wcex))
	{
		_r_show_errormessage (NULL, NULL, PebLastError (), NULL, TRUE);

		return FALSE;
	}

	wcex.lpszClassName = CLASS_FULLSCREEN;
	wcex.hCursor = LoadCursorW (hinst, MAKEINTRESOURCE (IDR_CURSOR));

	if (!RegisterClassExW (&wcex))
	{
		_r_show_errormessage (NULL, NULL, PebLastError (), NULL, TRUE);

		return FALSE;
	}

	return TRUE;
}

INT APIENTRY wWinMain (
	_In_ HINSTANCE hinst,
	_In_opt_ HINSTANCE prev_hinst,
	_In_ LPWSTR cmdline,
	_In_ INT show_cmd
)
{
	R_STRINGREF sr;
	HWND hwnd = NULL;
	MSG msg;

	if (!_r_app_initialize (NULL))
		return ERROR_NOT_READY;

	// read settings
	ReadSettings ();

	// register classes
	if (!RegisterClasses (hinst))
		goto CleanupExit;

	_r_obj_initializestringref (&sr, cmdline);

	// parse arguments
	if (_r_str_isstartswith2 (&sr, L"/s", TRUE))
	{
		StartScreensaver (NULL);
	}
	else if (_r_str_isstartswith2 (&sr, L"/p", TRUE))
	{
		_r_obj_skipstringlength (&sr, 3 * sizeof (WCHAR));

		hwnd = (HWND)_r_str_tolong_ptr (&sr);

		if (!hwnd)
			goto CleanupExit;

		StartScreensaver (hwnd);
	}
	else
	{
		config.is_preview = TRUE;

		hwnd = _r_app_createwindow (hinst, MAKEINTRESOURCE (IDD_SETTINGS), MAKEINTRESOURCE (IDI_MAIN), &SettingsProc);

		if (!hwnd)
			goto CleanupExit;
	}

	if (hwnd)
		_r_wnd_message_callback (hwnd, NULL);

	while (GetMessageW (&msg, NULL, 0, 0) > 0)
	{
		if (config.is_preview && hwnd && IsDialogMessageW (hwnd, &msg))
			continue;

		TranslateMessage (&msg);
		DispatchMessageW (&msg);
	}

CleanupExit:

	UnregisterClassW (CLASS_PREVIEW, hinst);
	UnregisterClassW (CLASS_FULLSCREEN, hinst);

	return ERROR_SUCCESS;
}
