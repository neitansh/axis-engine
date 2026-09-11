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
// Подпись на закрытых глазах, нарисованная в начале кадра (ScreenCaptionStep).
uniform sampler2D caption;

uniform vec2 texelSize0;

uniform ExposureParams exposureParams;
uniform lowp float bloomIntensity;
uniform lowp float saturation;
// Веки: 0 — глаза открыты, 1 — закрыты. См. TOCLIENT_EYELIDS.
uniform lowp float eyelids;
uniform mediump float eyelidsTime;
// Смыкаются (1) или раскрываются (0): раскрытие идёт с морганиями.
uniform lowp float eyelidsClosing;

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

// Мир сквозь прикрытые веки: мягче и темнее, чем шире они сомкнуты.
vec3 hazyWorld(vec2 uv, float haze)
{
	vec3 color = texture2D(rendered, uv).rgb;
	if (haze > 0.02) {
		vec2 step = texelSize0 * (1.0 + 6.0 * haze);
		vec3 sum = color;
		sum += texture2D(rendered, uv + vec2(step.x, 0.0)).rgb;
		sum += texture2D(rendered, uv - vec2(step.x, 0.0)).rgb;
		sum += texture2D(rendered, uv + vec2(0.0, step.y)).rgb;
		sum += texture2D(rendered, uv - vec2(0.0, step.y)).rgb;
		sum += texture2D(rendered, uv + step).rgb;
		sum += texture2D(rendered, uv - step).rgb;
		color = mix(color, sum / 7.0, haze);
	}
	return color;
}

// Подпись — мягкая, как всё, что видно с закрытыми глазами.
vec4 softCaption(vec2 uv)
{
	const int TAPS = 8;
	vec2 taps[TAPS];
	taps[0] = vec2(-0.7, -0.7); taps[1] = vec2( 0.7, -0.7);
	taps[2] = vec2(-0.7,  0.7); taps[3] = vec2( 0.7,  0.7);
	taps[4] = vec2(-1.0,  0.0); taps[5] = vec2( 1.0,  0.0);
	taps[6] = vec2( 0.0, -1.0); taps[7] = vec2( 0.0,  1.0);
	float radius = 0.003;
	vec4 sum = texture2D(caption, uv);
	for (int i = 0; i < TAPS; i++)
		sum += texture2D(caption, uv + taps[i] * radius);
	return sum / float(TAPS + 1);
}

// Насколько открыт глаз при раскрытии, 0..1 по ходу раскрытия: два
// коротких моргания, потом целиком.
float eyeOpening(float progress)
{
	if (progress < 0.16)
		return 0.35 * smoothstep(0.0, 0.16, progress);
	if (progress < 0.26)
		return 0.35 * (1.0 - smoothstep(0.16, 0.26, progress));
	if (progress < 0.46)
		return 0.7 * smoothstep(0.26, 0.46, progress);
	if (progress < 0.56)
		return mix(0.7, 0.08, smoothstep(0.46, 0.56, progress));
	return mix(0.08, 1.0, smoothstep(0.56, 1.0, progress));
}

void main(void)
{
	vec2 uv = varTexCoord.st;
	// Веки. Смыкаются ровно, картинка за ними мутнеет и гаснет; на чёрном
	// проявляется подпись. Раскрываются с морганиями — две прямые шторки
	// сверху и снизу с мягким краем расходятся, смыкаются, расходятся шире
	// и лишь потом целиком; подпись гаснет раньше, чем глаз откроется.
	float k = eyelids;
	if (k > 0.001) {
		float open;
		if (eyelidsClosing > 0.5)
			open = 1.0 - smoothstep(0.0, 1.0, k);
		else
			open = k > 0.85 ? 0.0 : eyeOpening((0.85 - k) / 0.85);
		float lid = 1.0 - smoothstep(0.5 * open - 0.04, 0.5 * open + 0.01, abs(uv.y - 0.5));
		// Сомкнутые веки не пропускают ничего, даже по шву.
		lid *= smoothstep(0.0, 0.06, open);
		lid = max(lid, smoothstep(0.92, 1.0, open));

		float haze = (1.0 - open) * 0.8;
		vec4 color = vec4(hazyWorld(uv, haze), 1.0);
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
		color.rgb *= lid * mix(0.6, 1.0, open);

		// Подпись живёт только на закрытых глазах.
		vec4 cap = softCaption(uv);
		cap.a *= smoothstep(0.85, 1.0, k);
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
