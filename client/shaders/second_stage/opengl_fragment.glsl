#define rendered texture0
#define bloom texture1
#define caption texture3

#ifdef GL_ES
// Dithering requires sufficient floating-point precision
#ifndef GL_FRAGMENT_PRECISION_HIGH
#undef ENABLE_DITHERING
#endif
#endif

struct ExposureParams {
	float compensationFactor;
};

uniform sampler2D rendered;
uniform sampler2D bloom;
// Подпись экрана помех, нарисованная в начале кадра (ScreenCaptionStep).
uniform sampler2D caption;

uniform vec2 texelSize0;

uniform ExposureParams exposureParams;
uniform lowp float bloomIntensity;
uniform lowp float saturation;
// Помехи потерянного сигнала: 0 — чисто, 1 — сплошь. См. TOCLIENT_SCREEN_STATIC.
uniform lowp float screenStatic;
uniform mediump float screenStaticTime;

CENTROID_ VARYING_ mediump vec2 varTexCoord;

#ifdef ENABLE_AUTO_EXPOSURE
VARYING_ float exposure; // linear exposure factor, see vertex shader
#endif

#ifdef ENABLE_BLOOM

vec4 applyBloom(vec4 color, vec2 uv)
{
	vec3 light = texture2D(bloom, uv).rgb;
#ifdef ENABLE_BLOOM_DEBUG
	if (uv.x > 0.5 && uv.y < 0.5)
		return vec4(light, color.a);
	if (uv.x < 0.5)
		return color;
#endif
	color.rgb = mix(color.rgb, light, bloomIntensity);
	return color;
}

#endif

#if ENABLE_TONE_MAPPING

/* Hable's UC2 Tone mapping parameters
	A = 0.22;
	B = 0.30;
	C = 0.10;
	D = 0.20;
	E = 0.01;
	F = 0.30;
	W = 11.2;
	equation used:  ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F
*/

// highp for GLES, see <https://github.com/luanti-org/luanti/pull/14688>
highp vec3 uncharted2Tonemap(highp vec3 x)
{
	return ((x * (0.22 * x + 0.03) + 0.002) / (x * (0.22 * x + 0.3) + 0.06)) - 0.03333;
}

// Принимает цвет в линейном пространстве: перевод в sRGB и обратно, стоявший
// здесь и строкой выше по ходу кадра, взаимно отменялся, а стоил шести
// возведений в степень на каждый пиксель экрана.
vec4 applyToneMapping(vec4 color)
{
	const float gamma = 1.6;
	const float exposureBias = 5.5;
	color.rgb = uncharted2Tonemap(exposureBias * color.rgb);
	// Precalculated white_scale from
	//vec3 whiteScale = 1.0 / uncharted2Tonemap(vec3(W));
	vec3 whiteScale = vec3(1.036015346);
	color.rgb *= whiteScale;
	return vec4(pow(color.rgb, vec3(1.0 / gamma)), color.a);
}
#endif

vec3 applySaturation(vec3 color, float factor)
{
	// Calculate the perceived luminosity from the RGB color.
	// See also: https://www.w3.org/WAI/GL/wiki/Relative_luminance
	float brightness = dot(color, vec3(0.2125, 0.7154, 0.0721));
	return mix(vec3(brightness), color, factor);
}

#ifdef ENABLE_DITHERING
// From http://alex.vlachos.com/graphics/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf
// and https://www.shadertoy.com/view/MslGR8 (5th one starting from the bottom)
// NOTE: `frag_coord` is in pixels (i.e. not normalized UV).
vec3 screen_space_dither(highp vec2 frag_coord) {
	// Iestyn's RGB dither (7 asm instructions) from Portal 2 X360, slightly modified for VR.
	highp vec3 dither = vec3(dot(vec2(171.0, 231.0), frag_coord));
	dither.rgb = fract(dither.rgb / vec3(103.0, 71.0, 97.0));

	// Subtract 0.5 to avoid slightly brightening the whole viewport.
	return (dither.rgb - 0.5) / 255.0;
}
#endif

float staticHash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Чистые помехи: контрастное чёрно-белое зерно, размазанное вдоль строк,
// тёмная строчная развёртка через каждые три точки, яркая середина и тёмные
// края трубки, поверх — бегущая полоса.
vec3 snowColor(vec2 uv)
{
	float frame = floor(screenStaticTime * 30.0);
	vec2 px = floor(gl_FragCoord.xy / 2.0);
	float g = staticHash(px + vec2(frame * 13.0, frame * 7.0));
	float row = staticHash(vec2(px.y, frame * 3.0));
	g = mix(g, row, 0.35);
	g = smoothstep(0.15, 0.85, g);

	float lines = 0.3 + 0.7 * (0.5 + 0.5 * cos(gl_FragCoord.y * 2.094));
	float mid = 1.0 - abs(uv.y - 0.5) * 2.0;
	float edge = length((uv - 0.5) * vec2(1.2, 1.0));
	float vignette = 1.0 - smoothstep(0.3, 0.95, edge);
	float band = fract(uv.y * 0.6 - screenStaticTime * 0.13);
	float roll = smoothstep(0.42, 0.5, band) * (1.0 - smoothstep(0.5, 0.62, band));
	float flicker = 0.92 + 0.08 * staticHash(vec2(frame, 1.0));

	float v = g * lines * mix(0.5, 1.0, mid) * mix(0.3, 1.0, vignette) * flicker;
	v += roll * 0.12;
	return vec3(v);
}

// Гладкий шум: пятна помех с мягкими краями, а не клетки.
float smoothNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = staticHash(i);
	float b = staticHash(i + vec2(1.0, 0.0));
	float c = staticHash(i + vec2(0.0, 1.0));
	float d = staticHash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Картинка мира там, где сигнал уже пойман: чем сильнее помехи вокруг, тем
// она серее и размытее — возвращается не сразу в полном качестве.
// Картинка плывёт: медленная волна по всему кадру и мелкая построчная дрожь,
// как у плёнки, которую тянет неровно.
vec2 swim(vec2 uv, float k, float amount)
{
	float t = screenStaticTime;
	uv.x += sin(uv.y * 6.3 + t * 1.7) * 0.012 * amount * k;
	uv.y += sin(uv.x * 4.7 + t * 1.1) * 0.008 * amount * k;
	uv.x += sin(uv.y * 90.0 + t * 9.0) * 0.0025 * amount * k;
	uv.y += sin(t * 0.7) * 0.01 * amount * k;
	return uv;
}

vec3 worldColor(vec2 uv, float k)
{
	uv = swim(uv, k, 1.0);
	float row = floor(uv.y * 48.0);
	float tick = floor(screenStaticTime * 12.0);
	float tear = staticHash(vec2(row, tick));
	if (tear > 0.93)
		uv.x += (tear - 0.93) * 0.6 * k;

	vec3 color = texture2D(rendered, uv).rgb;
	if (k > 0.02) {
		vec2 step = texelSize0 * (1.0 + 5.0 * k);
		vec3 sum = color;
		sum += texture2D(rendered, uv + vec2(step.x, 0.0)).rgb;
		sum += texture2D(rendered, uv - vec2(step.x, 0.0)).rgb;
		sum += texture2D(rendered, uv + vec2(0.0, step.y)).rgb;
		sum += texture2D(rendered, uv - vec2(0.0, step.y)).rgb;
		sum += texture2D(rendered, uv + step).rgb;
		sum += texture2D(rendered, uv - step).rgb;
		color = mix(color, sum / 7.0, k);
	}
	return color;
}

// Подпись поверх помех: мягкая, с зерном, но не серая — читаться она
// обязана, это единственное слово на экране.
vec4 blurredCaption(vec2 uv, float k)
{
	const int TAPS = 8;
	vec2 taps[TAPS];
	taps[0] = vec2(-0.7, -0.7); taps[1] = vec2( 0.7, -0.7);
	taps[2] = vec2(-0.7,  0.7); taps[3] = vec2( 0.7,  0.7);
	taps[4] = vec2(-1.0,  0.0); taps[5] = vec2( 1.0,  0.0);
	taps[6] = vec2( 0.0, -1.0); taps[7] = vec2( 0.0,  1.0);
	uv = swim(uv, k, 0.35);
	float radius = 0.004;
	vec4 sum = texture2D(caption, uv);
	for (int i = 0; i < TAPS; i++)
		sum += texture2D(caption, uv + taps[i] * radius);
	return sum / float(TAPS + 1);
}

void main(void)
{
	vec2 uv = varTexCoord.st;
	// Сигнал не уходит и не возвращается разом: по экрану лежит гладкое поле
	// шума, и у каждого места свой порог силы помех, ниже которого оно ловит
	// картинку. Так при уходе помех мир проступает мягкими пятнами, которые
	// растут и сливаются, а на полной силе не проступает нигде.
	float k = screenStatic;
	if (k > 0.001) {
		float t = screenStaticTime;
		float field = 0.6 * smoothNoise(uv * vec2(5.0, 3.0) + vec2(t * 0.12, -t * 0.08))
				+ 0.4 * smoothNoise(uv * vec2(11.0, 7.0) + vec2(-t * 0.2, t * 0.15));
		float threshold = 0.1 + 0.85 * field;
		float snow = smoothstep(threshold - 0.35, threshold, k);

		vec4 color = vec4(worldColor(uv, k), 1.0);
		color.rgb = pow(color.rgb, vec3(2.2));
		color.rgb *= exposureParams.compensationFactor;
#ifdef ENABLE_AUTO_EXPOSURE
		color.rgb *= exposure;
#endif
#ifdef ENABLE_BLOOM
		color = applyBloom(color, uv);
#endif
		color.rgb = clamp(color.rgb, vec3(0.), vec3(1.));
#if ENABLE_TONE_MAPPING
		color = applyToneMapping(color);
#else
		color.rgb = pow(color.rgb, vec3(1.0 / 2.2));
#endif
		color.rgb = applySaturation(color.rgb, saturation);
		float luma = dot(color.rgb, vec3(0.2125, 0.7154, 0.0721));
		color.rgb = mix(color.rgb, vec3(luma), 0.8 * k);

		color.rgb = mix(color.rgb, snowColor(uv), snow);

		vec4 cap = blurredCaption(uv, k);
		cap.a *= smoothstep(0.55, 0.85, k);
		color.rgb = mix(color.rgb, cap.rgb, cap.a);
		gl_FragColor = vec4(clamp(color.rgb, vec3(0.), vec3(1.)), 1.0);
		return;
	}
#ifdef ENABLE_SSAA
	vec4 color = vec4(0.);
	for (float dx = 1.; dx < SSAA_SCALE; dx += 2.)
	for (float dy = 1.; dy < SSAA_SCALE; dy += 2.)
		color += texture2D(rendered, uv + texelSize0 * vec2(dx, dy)).rgba;
	color /= SSAA_SCALE * SSAA_SCALE / 4.;
#else
	vec4 color = texture2D(rendered, uv).rgba;
#endif

	// translate to linear colorspace (approximate)
	color.rgb = pow(color.rgb, vec3(2.2));

#ifdef ENABLE_BLOOM_DEBUG
	if (uv.x > 0.5 || uv.y > 0.5)
#endif
	{
		color.rgb *= exposureParams.compensationFactor;
#ifdef ENABLE_AUTO_EXPOSURE
		color.rgb *= exposure;
#endif
	}

#ifdef ENABLE_BLOOM
	color = applyBloom(color, uv);
#endif


	color.rgb = clamp(color.rgb, vec3(0.), vec3(1.));

#if !ENABLE_TONE_MAPPING
	// return to sRGB colorspace (approximate)
	color.rgb = pow(color.rgb, vec3(1.0 / 2.2));
#endif

#ifdef ENABLE_BLOOM_DEBUG
	if (uv.x > 0.5 || uv.y > 0.5)
#endif
	{
#if ENABLE_TONE_MAPPING
		color = applyToneMapping(color);
#endif

		color.rgb = applySaturation(color.rgb, saturation);
	}

#ifdef ENABLE_DITHERING
	// Apply dithering just before quantisation
	color.rgb += screen_space_dither(gl_FragCoord.xy);
#endif

	gl_FragColor = vec4(color.rgb, 1.0); // force full alpha to avoid holes in the image.
}
