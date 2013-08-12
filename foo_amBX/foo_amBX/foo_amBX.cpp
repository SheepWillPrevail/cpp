// Copyright (c) 2010, Robert Meijer <robert@grazz.com>

// Please read and understand the License.txt file before using this software.

#include "stdafx.h"

static char *sPluginName = "foo_amBX hardware support";
static char *sPluginVersion = "0.8";
static char *sPluginDescription = 
"Copyright (c) 2010, Robert Meijer <robert@grazz.com>\n\
\n\
All rights reserved. The amBX referred to in this software is a\n\
registered trademark and property of amBX UK Ltd. This module is\n\
developed by a 3rd party and therefore not supported by amBX UK Ltd.\n\
\n\
Redistribution and use in source and binary forms, with or without\n\
modification, are permitted provided that the following conditions\n\
are met:\n\
\n\
- Redistributions of source code must retain the above copyright notice,\n\
  this list of conditions and the following disclaimer.\n\
- Redistributions in binary form must reproduce the above copyright notice,\n\
  this list of conditions and the following disclaimer in the documentation\n\
  and/or other materials provided with the distribution.\n\
\n\
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n\
\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n\
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n\
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n\
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n\
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n\
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n\
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n\
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n\
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n\
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n";

// {3AF873DE-528E-4f7f-9A5C-0D9A15B43FD8}
static const GUID gDSP = 
{ 0x3af873de, 0x528e, 0x4f7f, { 0x9a, 0x5c, 0xd, 0x9a, 0x15, 0xb4, 0x3f, 0xd8 } };

static const int iColorPresetCount = 3;
static const int iSpectrumPresetCount = 5;

static const std::wstring sColorPresetNames[iColorPresetCount] = {
	L"Elemental",
	L"Toasty",
	L"Bad Disco"
};

static const std::wstring sSpectrumPresetNames[iSpectrumPresetCount] = {
	L"Classic",
	L"Extremes",
	L"Dance",
	L"Pop",
	L"Rock"
};

// HR HG HB MR MG MB LR LG LB KR KG KB
//(float)0/255, (float)0/255, (float)0/255, (float)0/255, (float)0/255, (float)0/254, (float)0/255, (float)0/255, (float)0/255, (float)0/255, (float)0/255, (float)0/255
static const float fColorPresetValues[iColorPresetCount * 12] = {
	// Elemental
	1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1,	
	// Toasty
	(float)255/255, (float)233/255, (float)0/255, 
	(float)255/255, (float)91/255, (float)10/254, 
	(float)255/255, (float)0/255, (float)46/255, 
	(float)255/255, (float)215/255, (float)86/255,	
	// Bad Disco
	(float)255/254, (float)108/255, (float)35/255, 
	(float)127/255, (float)255/255, (float)199/255, 
	(float)212/255, (float)255/255, (float)0/255, 
	(float)255/255, (float)96/255, (float)249/255
};

// LS LW MS MW HS HW
static const int iSpectrumPresetValues[iSpectrumPresetCount * 6] = {
	// LLLLMMMMMMHHHHHH classic
	0, 4, 4, 6, 10, 6,
	// LLLLLLLMMHHHHHHH extremes
	0, 7, 7, 2, 9, 7,
	// LLMMMMMMMMMMHHHH dance
	0, 2, 2, 10, 12, 4,
	// LLLMMMMMMMHHHHHH pop
	0, 3, 3, 7, 10, 6,
	// LLLLLLMMMMMMHHHH rock
	0, 6, 6, 6, 12, 4
};

static void RunDSPConfigPopup(const dsp_preset &p_data, HWND p_parent, dsp_preset_edit_callback &p_callback);

using namespace pfc;

#define SAFEDELETEARRAY(a) if (a != NULL) delete[] a;

class foo_amBX_dsp : public dsp_impl_base {
private:
	// foobar2000
	service_ptr_t<visualisation_stream> pVisStream;
	// amBX
	IamBX *pAXEngine;
	IamBX_Light *pAXLightCenter, *pAXLightLeft, *pAXLightRight;

	// runtime
	double dPT, dLatency, dFadeDelay;
	int iUpdateMS, iTemporalBuffer;
	float *fLH, *fLM, *fLL, *fRH, *fRM, *fRL, *fPK, fCP[12], fKickAttn;
	// settings
	int iUPS, iKickWindow, iHighAmp, iMidAmp, iLowAmp, iColorPreset, iKickAttn, iSpectrumPreset, iSM[6];

public:
	void startTiming() {
		pVisStream->get_absolute_time(dPT);
	}

	void stopTiming() {
		double dCT;
		pVisStream->get_absolute_time(dCT);
		dLatency = dCT - dPT;
	}

	float *initTemporalBuffer() {
		float *buf = new float[iTemporalBuffer];
		for (int i = 0; i < iTemporalBuffer; i++)
			buf[i] = 0;
		return buf;
	}

	foo_amBX_dsp(dsp_preset const &in) {
		static_api_ptr_t<visualisation_manager>()->create_stream(pVisStream, visualisation_manager::KStreamFlagNewFFT);		
		startTiming();
		
		parse_preset(iUPS, iKickWindow, iHighAmp, iMidAmp, iLowAmp, iColorPreset, iKickAttn, iSpectrumPreset, iTemporalBuffer, in);
		
		dLatency = 0;
		iUpdateMS = 1000/iUPS;
		dFadeDelay = 1/iUPS;
		fKickAttn = 0.10f * iKickAttn;

		fLH = initTemporalBuffer();
		fLM = initTemporalBuffer();
		fLL = initTemporalBuffer();
		fRH = initTemporalBuffer();
		fRM = initTemporalBuffer();
		fRL = initTemporalBuffer();
		fPK = initTemporalBuffer();

		int c = 0;
		for (int i = iColorPreset * 12; i < ((iColorPreset + 1) * 12); i++)
			fCP[c++] = fColorPresetValues[i];
		c = 0;
		for (int i = iSpectrumPreset * 6; i < ((iSpectrumPreset + 1) * 6); i++)
			iSM[c++] = iSpectrumPresetValues[i];

		try {
			if (amBXCreateInterface(&pAXEngine, 1, 0, sPluginName, sPluginVersion, NULL, FALSE) != amBX_OK)
				throw new exception("Unable to create interface to amBX");
			
			pAXEngine->createLight(pAXEngine, amBX_NW | amBX_W | amBX_SW, amBX_EVERYHEIGHT, &pAXLightLeft);
			pAXLightLeft->setFadeTime(pAXLightLeft, iUpdateMS);

			pAXEngine->createLight(pAXEngine, amBX_NE | amBX_E | amBX_SE, amBX_EVERYHEIGHT, &pAXLightRight);
			pAXLightRight->setFadeTime(pAXLightRight, iUpdateMS);
			
			pAXEngine->createLight(pAXEngine, amBX_N | amBX_C | amBX_S, amBX_EVERYHEIGHT, &pAXLightCenter);
			pAXLightCenter->setFadeTime(pAXLightCenter, iUpdateMS);
		}
		catch (const exception &exc) {
			console::error(string8() << exc);
		}

		stopTiming();
	}

	~foo_amBX_dsp() {
		if (pAXEngine != NULL)
			pAXEngine->release(pAXEngine);
		
		SAFEDELETEARRAY(fLH)
		SAFEDELETEARRAY(fLM)
		SAFEDELETEARRAY(fLL)
		SAFEDELETEARRAY(fRH)
		SAFEDELETEARRAY(fRM)
		SAFEDELETEARRAY(fRL)
		SAFEDELETEARRAY(fPK)
	}

	float combineChannels(float a, float b, float c) {
		float val[3] = { a, b, c };
		float max = 0;
		for (int i = 0; i < 3; i++)
			if (val[i] > max)
				max = val[i];
		return max;
	}

	float foldTemporalBuffer(const float *buf) {
		float total = 0;
		for (int i = 0; i < iTemporalBuffer; i++)
			total += buf[i];
		return total / iTemporalBuffer;
	}

	void addToTemporalBuffer(float *buf, float val) {
		float previous = val;
		for (int i=0; i < iTemporalBuffer; i++) {
			float current = buf[i];
			buf[i] = previous;
			previous = current;
		}
	}

	float calcBandAvg(float *prev, audio_sample *input, int bandsize, int band, int bandwidth, float amp) {
		audio_sample *read = input + (band * bandsize);
		int iReadCount = bandsize * bandwidth;

		float total = 0;
		for (int i = 0; i < iReadCount; i++) {
			total += *read++ * bandwidth;
		}

		float calc = min(1, (total * amp) / iReadCount);
		float val = (foldTemporalBuffer(prev) + calc) / 2;
		addToTemporalBuffer(prev, calc);

		return val;
	}
	
	void on_endoftrack(abort_callback &p_abort) {}
	
	void on_endofplayback(abort_callback &p_abort) {}

	bool on_chunk(audio_chunk * p_chunk,abort_callback &p_abort) {
		startTiming();
		if (pAXEngine != NULL) {
			audio_chunk_impl c;
			// 1 sec FFT buffer
			double offset = min(0.99f, dLatency + dFadeDelay);
			if (pVisStream->get_spectrum_absolute(c, dPT + offset, 256) && p_chunk->get_channel_count() == 2) {
				audio_sample *s = c.get_data();

				float kick = (foldTemporalBuffer(fPK) + audio_math::calculate_peak(s, iKickWindow)) / 2;
				addToTemporalBuffer(fPK, kick);

				float target_kick_r = fCP[9] * kick * fKickAttn;
				float target_kick_g = fCP[10] * kick * fKickAttn;
				float target_kick_b = fCP[11] * kick * fKickAttn;
				
				int c = 0;
				audio_sample left[128], right[128];
				for (int i = 0; i < 256; i+=2) {
					left[c] = s[i];
					right[c] = s[i+1];
					c++;
				}
				
				float left_high = calcBandAvg(fLH, left, 8, iSM[4], iSM[5], (float)iHighAmp);
				float left_mid = calcBandAvg(fLM, left, 8, iSM[2], iSM[3], (float)iMidAmp);
				float left_low = calcBandAvg(fLL, left, 8, iSM[0], iSM[1], (float)iLowAmp);
				float right_high = calcBandAvg(fRH, right, 8, iSM[4], iSM[5], (float)iHighAmp);
				float right_mid = calcBandAvg(fRM, right, 8, iSM[2], iSM[3], (float)iMidAmp);
				float right_low = calcBandAvg(fRL, right, 8, iSM[0], iSM[1], (float)iLowAmp);

				float target_left_r = combineChannels(left_high * fCP[0], left_mid * fCP[3], left_low * fCP[6]);
				float target_left_g = combineChannels(left_high * fCP[1], left_mid * fCP[4], left_low * fCP[7]);
				float target_left_b = combineChannels(left_high * fCP[2], left_mid * fCP[5], left_low * fCP[8]);
				float target_right_r = combineChannels(right_high * fCP[0], right_mid * fCP[3], right_low * fCP[6]);
				float target_right_g = combineChannels(right_high * fCP[1], right_mid * fCP[4], right_low * fCP[7]);
				float target_right_b = combineChannels(right_high * fCP[2], right_mid * fCP[5], right_low * fCP[8]);
				float target_center_r = combineChannels(target_left_r, target_kick_r, target_right_r);
				float target_center_g = combineChannels(target_left_g, target_kick_g, target_right_g);
				float target_center_b = combineChannels(target_left_b, target_kick_b, target_right_b);

				pAXLightLeft->setCol(pAXLightLeft, min(1, target_left_r), min(1, target_left_g), min(1, target_left_b));
				pAXLightRight->setCol(pAXLightRight, min(1, target_right_r), min(1, target_right_g), min(1, target_right_b));
				pAXLightCenter->setCol(pAXLightCenter, min(1, target_center_r), min(1, target_center_g), min(1, target_center_b));

				pAXEngine->update(pAXEngine, 0);
			}
		}

		stopTiming();
		return TRUE;
	}

	void flush() {}

	double get_latency() { return dLatency; }

	bool need_track_change_mark() { return FALSE; }

	static void g_get_name(pfc::string_base &n) {
		n.set_string_(sPluginName);
	}
	
	static bool g_get_default_preset(dsp_preset &out) {
		make_preset(5, 64, 4, 2, 1, 0, 10, 0, 3, out);
		return TRUE;
	}

	static GUID g_get_guid() { return gDSP; }

	static bool g_have_config_popup() { return TRUE; }

	static void g_show_config_popup(const dsp_preset &p_data, HWND p_parent, dsp_preset_edit_callback &p_callback) {
		RunDSPConfigPopup(p_data, p_parent, p_callback);
	}

	static void make_preset(int iUPS, int iKickWindow, int iHighAmp, int iMidAmp, int iLowAmp, int iColorPreset, int iKickAttn, int iSpectrumPreset, int iTemporalBuffer, dsp_preset &out) {
		dsp_preset_builder builder; 
		builder << iUPS;
		builder << iKickWindow;
		builder << iHighAmp;
		builder << iMidAmp;
		builder << iLowAmp;
		builder << iColorPreset;
		builder << iKickAttn;
		builder << iSpectrumPreset;
		builder << iTemporalBuffer;
		builder.finish(g_get_guid(), out);
	}
	
	static void parse_preset(int &iUPS, int &iKickWindow, int &iHighAmp, int &iMidAmp, int &iLowAmp, int &iColorPreset, int &iKickAttn, int &iSpectrumPreset, int &iTemporalBuffer, const dsp_preset &in) {
		try {
			dsp_preset_parser parser(in);
			parser >> iUPS;
			parser >> iKickWindow;
			parser >> iHighAmp;
			parser >> iMidAmp;
			parser >> iLowAmp;
			parser >> iColorPreset;
			parser >> iKickAttn;
			parser >> iSpectrumPreset;
			parser >> iTemporalBuffer;
		} catch(exception_io_data) {
			dsp_preset_impl def;
			g_get_default_preset(def);
			parse_preset(iUPS, iKickWindow, iHighAmp, iMidAmp, iLowAmp, iColorPreset, iKickAttn, iSpectrumPreset, iTemporalBuffer, def);
		}
	}
};

class CfooamBXPreferences : public CDialogImpl<CfooamBXPreferences> {
public:
	enum { IDD = IDD_PREFSDIALOG };

	CfooamBXPreferences(const dsp_preset &initData, dsp_preset_edit_callback &callback) : m_callback(callback) {
		parsePreset(initData);
	}

	BEGIN_MSG_MAP(CfooamBXPreferences)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnButton)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnButton)
		COMMAND_HANDLER(IDC_UPS, CBN_SELCHANGE, OnComboChange)
		COMMAND_HANDLER(IDC_COLORPRESET, CBN_SELCHANGE, OnComboChange)
		COMMAND_HANDLER(IDC_SPECTRUMPRESET, CBN_SELCHANGE, OnComboChange)
		NOTIFY_HANDLER(IDC_TBUFSIZE, NM_RELEASEDCAPTURE, OnSliderChange)
		NOTIFY_HANDLER(IDC_KWINDOW, NM_RELEASEDCAPTURE, OnSliderChange)
		NOTIFY_HANDLER(IDC_KICKATTN, NM_RELEASEDCAPTURE, OnSliderChange)
		NOTIFY_HANDLER(IDC_HIGHAMP, NM_RELEASEDCAPTURE, OnSliderChange)
		NOTIFY_HANDLER(IDC_MIDAMP, NM_RELEASEDCAPTURE, OnSliderChange)
		NOTIFY_HANDLER(IDC_LOWAMP, NM_RELEASEDCAPTURE, OnSliderChange)
	END_MSG_MAP()

private:
	// callbacks
	dsp_preset_edit_callback &m_callback;
	// settings
	int iUPS, iKickWindow, iHighAmp, iMidAmp, iLowAmp, iColorPreset, iKickAttn, iSpectrumPreset, iTemporalBuffer;
	// window stuff
	CComboBox comboUPS, comboColorPreset, comboSpectrumPreset;
	CTrackBarCtrl trackKWindow, trackRedAmp, trackGreenAmp, trackBlueAmp, trackKAttn, trackTBuffer;

	void parsePreset(const dsp_preset &initData) {
		try {
			dsp_preset_parser parser(initData);
			parser >> iUPS;
			parser >> iKickWindow;
			parser >> iHighAmp;
			parser >> iMidAmp;
			parser >> iLowAmp;
			parser >> iColorPreset;
			parser >> iKickAttn;
			parser >> iSpectrumPreset;
			parser >> iTemporalBuffer;
		} catch(exception_io_data) {
			dsp_preset_impl def;
			foo_amBX_dsp::g_get_default_preset(def);
			parsePreset(def);
		}
	}

	BOOL OnInitDialog(CWindow, LPARAM) {
		comboUPS = GetDlgItem(IDC_UPS);
		trackKWindow = GetDlgItem(IDC_KWINDOW);
		trackRedAmp = GetDlgItem(IDC_HIGHAMP);
		trackGreenAmp = GetDlgItem(IDC_MIDAMP);
		trackBlueAmp = GetDlgItem(IDC_LOWAMP);
		comboColorPreset = GetDlgItem(IDC_COLORPRESET);
		trackKAttn = GetDlgItem(IDC_KICKATTN);
		comboSpectrumPreset = GetDlgItem(IDC_SPECTRUMPRESET);
		trackTBuffer = GetDlgItem(IDC_TBUFSIZE);

		for (int i = 0; i < 20; i++) {
			std::wstringstream itemStream;
			std::wstring itemStringW;
			itemStream << (i + 1);
			itemStringW = itemStream.str();
			comboUPS.AddString(itemStringW.c_str());
		}
		comboUPS.SetCurSel(iUPS - 1);

		for (int i = 0; i < iColorPresetCount; i++) {
			comboColorPreset.AddString(sColorPresetNames[i].c_str());
		}
		comboColorPreset.SetCurSel(iColorPreset);

		for (int i = 0; i < iSpectrumPresetCount; i++) {
			comboSpectrumPreset.AddString(sSpectrumPresetNames[i].c_str());
		}
		comboSpectrumPreset.SetCurSel(iSpectrumPreset);

		trackTBuffer.SetRange(1, 25, TRUE);
		trackTBuffer.SetPos(iTemporalBuffer > 25 ? 25 : iTemporalBuffer);

		trackKWindow.SetRange(0, 256, TRUE);
		trackKWindow.SetPos(iKickWindow > 256 ? 256 : iKickWindow);
		trackKAttn.SetRange(0, 10);
		trackKAttn.SetPos(iKickAttn > 10 ? 10 : iKickAttn);

		trackRedAmp.SetRange(0, 10, TRUE);
		trackRedAmp.SetPos(iHighAmp > 10 ? 10 : iHighAmp);
		trackGreenAmp.SetRange(0, 10, TRUE);
		trackGreenAmp.SetPos(iMidAmp > 10 ? 10 : iMidAmp);
		trackBlueAmp.SetRange(0, 10, TRUE);
		trackBlueAmp.SetPos(iLowAmp > 10 ? 10 : iLowAmp);

		return TRUE;
	}

	void OnButton(UINT, int id, CWindow) {
		EndDialog(id);
	}

	LRESULT OnComboChange(WORD, WORD, HWND, BOOL) {
		iUPS = comboUPS.GetCurSel() + 1;
		iColorPreset = comboColorPreset.GetCurSel();
		iSpectrumPreset = comboSpectrumPreset.GetCurSel();

		OnChanged();
		return 0;
	}

	LRESULT OnSliderChange(int, LPNMHDR, BOOL)
	{
		iTemporalBuffer = trackTBuffer.GetPos();

		iKickWindow = trackKWindow.GetPos();
		iKickAttn = trackKAttn.GetPos();
		iHighAmp = trackRedAmp.GetPos();
		iMidAmp = trackGreenAmp.GetPos();
		iLowAmp = trackBlueAmp.GetPos();

		OnChanged();
		return 0;
	}

	void OnChanged() {
		dsp_preset_impl preset;
		foo_amBX_dsp::make_preset(iUPS, iKickWindow, iHighAmp, iMidAmp, iLowAmp, iColorPreset, iKickAttn, iSpectrumPreset, iTemporalBuffer, preset);
		m_callback.on_preset_changed(preset);
	}
};

static void RunDSPConfigPopup(const dsp_preset &p_data, HWND p_parent, dsp_preset_edit_callback &p_callback) {
	CfooamBXPreferences popup(p_data, p_callback);
	if (popup.DoModal(p_parent) != IDOK) p_callback.on_preset_changed(p_data);
};

// exports for foobar2000
static dsp_factory_t<foo_amBX_dsp> g_foo_amBX_factory;
DECLARE_COMPONENT_VERSION(sPluginName, sPluginVersion, sPluginDescription);
VALIDATE_COMPONENT_FILENAME("foo_amBX.dll");
