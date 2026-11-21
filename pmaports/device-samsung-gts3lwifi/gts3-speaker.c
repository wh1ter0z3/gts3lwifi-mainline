/*
 * gts3_speaker: speaker processing for the Galaxy Tab S3, a LADSPA plugin.
 *
 * Reproduces the sound of the stock firmware's speaker processing from measurements:
 * an FFT multiband dynamics processor. Every frequency gets the gain measured on the
 * stock processing for the current level of its band (9 detection bands; below 60 Hz
 * the cut follows the whole bass region), then a peak limiter guards the speakers.
 *
 * Stereo in, stereo out, 1024 samples of latency (reported on the "latency" port).
 */
#include <ladspa.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gts3-speaker-tables.h"

#define N 1024
#define HOP 256
#define NBIN (N / 2 + 1)
#define NB 9

static const float edges[NB + 1] = { 0, 60, 200, 380, 680, 1500, 3200, 6500, 8000, 1e9f };
static const float release_s[NB] = { 0.30f, 0.25f, 0.20f, 0.15f, 0.10f, 0.10f, 0.25f, 0.25f, 0.30f };
static const float band_trim[NB] = { -9.0f, 0, 0, 0, 0, 0, 0, 0, 1.2f };
#define ATTACK_S 0.012f
#define LEVEL_OFFSET (-3.5f)	/* band power -> measured sine level, fitted */
#define TRIM (-0.8f)		/* overall, fitted on music */
#define CEILING 0.944f		/* limiter: -0.5 dBFS */
#define LIM_RELEASE_S 0.08f

enum { P_IN_L, P_IN_R, P_OUT_L, P_OUT_R, P_BOOST, P_LATENCY, P_COUNT };

typedef struct { float re, im; } cpx;

struct gts3 {
	LADSPA_Data *port[P_COUNT];
	float rate;
	float win[N], wnorm, wsum;
	cpx tw[N / 2];
	int rev[N];
	float curve[2][NBIN][TAB_NLEV];
	int band[NBIN];
	float att, rel[NB];
	float env[2][NB];
	float in[2][N];		/* the last N input samples */
	float acc[2][N];	/* overlap-add accumulator */
	int fill;		/* new samples since the last frame */
	float lim_gain, lim_rel;
};

static void fft(const struct gts3 *s, cpx *x, int inverse)
{
	for (int i = 0; i < N; i++)
		if (i < s->rev[i]) {
			cpx t = x[i];
			x[i] = x[s->rev[i]];
			x[s->rev[i]] = t;
		}
	for (int len = 2; len <= N; len <<= 1) {
		int step = N / len;
		for (int i = 0; i < N; i += len)
			for (int k = 0; k < len / 2; k++) {
				cpx w = s->tw[k * step];
				if (inverse)
					w.im = -w.im;
				cpx a = x[i + k], b = x[i + k + len / 2];
				cpx t = { b.re * w.re - b.im * w.im, b.re * w.im + b.im * w.re };
				x[i + k] = (cpx){ a.re + t.re, a.im + t.im };
				x[i + k + len / 2] = (cpx){ a.re - t.re, a.im - t.im };
			}
	}
}

static float interp_log(const float *xs, const float *ys, int n, float x)
{
	if (x <= xs[0])
		return ys[0];
	for (int i = 1; i < n; i++)
		if (x <= xs[i]) {
			float t = (log2f(x) - log2f(xs[i - 1])) / (log2f(xs[i]) - log2f(xs[i - 1]));
			return ys[i - 1] + t * (ys[i] - ys[i - 1]);
		}
	return ys[n - 1];
}

static LADSPA_Handle instantiate(const LADSPA_Descriptor *d, unsigned long rate)
{
	struct gts3 *s = calloc(1, sizeof(*s));
	(void)d;
	if (!s)
		return NULL;
	s->rate = rate;
	for (int i = 0; i < N; i++) {
		s->win[i] = sqrtf(0.5f - 0.5f * cosf(2 * (float)M_PI * i / N));
		s->wnorm += s->win[i] * s->win[i];
		s->wsum += s->win[i];
	}
	s->wnorm /= HOP;
	for (int i = 0; i < N / 2; i++)
		s->tw[i] = (cpx){ cosf(2 * (float)M_PI * i / N), -sinf(2 * (float)M_PI * i / N) };
	for (int i = 0, bits = 10; i < N; i++) {
		int r = 0;
		for (int b = 0; b < bits; b++)
			r |= ((i >> b) & 1) << (bits - 1 - b);
		s->rev[i] = r;
	}
	for (int c = 0; c < 2; c++) {
		float col[TAB_NFREQ];
		for (int k = 0; k < NBIN; k++) {
			float f = (float)k * rate / N;
			for (int l = 0; l < TAB_NLEV; l++) {
				for (int i = 0; i < TAB_NFREQ; i++) {
					col[i] = tab_gain[c][i][l];
					if (l == 0 && tab_gain[c][i][1] > col[i])
						col[i] = tab_gain[c][i][1];	/* noisy bass readings at -60 dBFS */
				}
				s->curve[c][k][l] = interp_log(tab_freq, col, TAB_NFREQ, f);
			}
		}
	}
	for (int k = 0; k < NBIN; k++) {
		float f = (float)k * rate / N;
		int b = 0;
		while (b < NB - 1 && f >= edges[b + 1])
			b++;
		s->band[k] = b;
	}
	s->att = expf(-HOP / (rate * ATTACK_S));
	for (int b = 0; b < NB; b++)
		s->rel[b] = expf(-HOP / (rate * release_s[b]));
	for (int c = 0; c < 2; c++)
		for (int b = 0; b < NB; b++)
			s->env[c][b] = -90;
	s->lim_gain = 1;
	s->lim_rel = expf(-1 / (rate * LIM_RELEASE_S));
	return s;
}

static void connect(LADSPA_Handle h, unsigned long port, LADSPA_Data *data)
{
	((struct gts3 *)h)->port[port] = data;
}

static float gain_db(const struct gts3 *s, int c, int k, float level)
{
	const float *lv = tab_level;
	if (level <= lv[0])
		return s->curve[c][k][0];
	if (level >= lv[TAB_NLEV - 1])
		return s->curve[c][k][TAB_NLEV - 1];
	float pos = (level - lv[0]) / (lv[1] - lv[0]);
	int i = (int)pos;
	float t = pos - i;
	return s->curve[c][k][i] * (1 - t) + s->curve[c][k][i + 1] * t;
}

static void frame(struct gts3 *s)
{
	cpx z[N];
	cpx sp[2][NBIN];

	for (int i = 0; i < N; i++)
		z[i] = (cpx){ s->in[0][i] * s->win[i], s->in[1][i] * s->win[i] };
	fft(s, z, 0);
	for (int k = 0; k < NBIN; k++) {	/* split the two real spectra */
		cpx a = z[k], b = z[(N - k) % N];
		sp[0][k] = (cpx){ (a.re + b.re) / 2, (a.im - b.im) / 2 };
		sp[1][k] = (cpx){ (a.im + b.im) / 2, (b.re - a.re) / 2 };
	}
	float ref = s->wsum * s->wsum / 4;
	for (int c = 0; c < 2; c++) {
		float bp[NB] = { 0 }, lvl[NB];
		for (int k = 0; k < NBIN; k++)
			bp[s->band[k]] += sp[c][k].re * sp[c][k].re + sp[c][k].im * sp[c][k].im;
		for (int b = 0; b < NB; b++) {
			lvl[b] = 10 * log10f(bp[b] * 2 / ref + 1e-12f) + LEVEL_OFFSET;
			float a = lvl[b] > s->env[c][b] ? s->att : s->rel[b];
			s->env[c][b] = a * s->env[c][b] + (1 - a) * lvl[b];
		}
		float bass = 10 * log10f(powf(10, s->env[c][0] / 10) + powf(10, s->env[c][1] / 10) +
					 powf(10, s->env[c][2] / 10));
		for (int k = 0; k < NBIN; k++) {
			int b = s->band[k];
			float g = gain_db(s, c, k, b == 0 ? bass : s->env[c][b]) + TRIM + band_trim[b];
			float m = powf(10, g / 20);
			sp[c][k].re *= m;
			sp[c][k].im *= m;
		}
	}
	for (int k = 0; k < N; k++) {	/* back into one complex spectrum: left + j right */
		cpx l, r;
		if (k < NBIN) {
			l = sp[0][k];
			r = sp[1][k];
		} else {
			l = (cpx){ sp[0][N - k].re, -sp[0][N - k].im };
			r = (cpx){ sp[1][N - k].re, -sp[1][N - k].im };
		}
		z[k] = (cpx){ l.re - r.im, l.im + r.re };
	}
	fft(s, z, 1);
	for (int i = 0; i < N; i++) {
		float w = s->win[i] / (N * s->wnorm);
		s->acc[0][i] += z[i].re * w;
		s->acc[1][i] += z[i].im * w;
	}
}

static void run(LADSPA_Handle h, unsigned long n)
{
	struct gts3 *s = h;
	float boost = powf(10, (s->port[P_BOOST] ? *s->port[P_BOOST] : 0) / 20);

	for (unsigned long i = 0; i < n; i++) {
		/* the input goes into the last HOP slots; the output comes from the front of the accumulator */
		s->in[0][N - HOP + s->fill] = s->port[P_IN_L][i];
		s->in[1][N - HOP + s->fill] = s->port[P_IN_R][i];
		float l = s->acc[0][s->fill] * boost, r = s->acc[1][s->fill] * boost;
		float peak = fmaxf(fabsf(l), fabsf(r)) * s->lim_gain;
		if (peak > CEILING)
			s->lim_gain *= CEILING / peak;
		else
			s->lim_gain = 1 - s->lim_rel * (1 - s->lim_gain);
		s->port[P_OUT_L][i] = l * s->lim_gain;
		s->port[P_OUT_R][i] = r * s->lim_gain;
		if (++s->fill == HOP) {
			s->fill = 0;
			for (int c = 0; c < 2; c++) {
				memmove(s->acc[c], s->acc[c] + HOP, (N - HOP) * sizeof(float));
				memset(s->acc[c] + N - HOP, 0, HOP * sizeof(float));
			}
			frame(s);
			for (int c = 0; c < 2; c++)
				memmove(s->in[c], s->in[c] + HOP, (N - HOP) * sizeof(float));
		}
	}
	if (s->port[P_LATENCY])
		*s->port[P_LATENCY] = N;
}

static void cleanup(LADSPA_Handle h)
{
	free(h);
}

static const LADSPA_PortDescriptor port_desc[P_COUNT] = {
	LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO, LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO, LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
	LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL, LADSPA_PORT_OUTPUT | LADSPA_PORT_CONTROL,
};
static const char *const port_names[P_COUNT] = { "In L", "In R", "Out L", "Out R", "Boost", "latency" };
static const LADSPA_PortRangeHint port_hints[P_COUNT] = {
	{ 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
	{ LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0, -12, 12 },
	{ 0, 0, 0 },
};

static const LADSPA_Descriptor desc = {
	.UniqueID = 0x67733301,
	.Label = "gts3_speaker",
	.Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE,
	.Name = "Galaxy Tab S3 speaker processing",
	.Maker = "gts3lwifi-mainline",
	.Copyright = "GPL-2.0-or-later",
	.PortCount = P_COUNT,
	.PortDescriptors = port_desc,
	.PortNames = port_names,
	.PortRangeHints = port_hints,
	.instantiate = instantiate,
	.connect_port = connect,
	.run = run,
	.cleanup = cleanup,
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long i)
{
	return i == 0 ? &desc : NULL;
}
