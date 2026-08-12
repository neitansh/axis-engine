uniform lowp vec4 fogColor;
uniform float fogDistance;
uniform float fogShadingParameter;

VARYING_ highp vec3 eyeVec;
VARYING_ lowp vec4 varColor;
VARYING_ mediump float v_relY;
VARYING_ highp float v_worldRelY;

void main(void)
{
	vec4 col = varColor;

	// 1. Стандартный туман (расстояние не зависит от поворота головы)
	float clarity = clamp(fogShadingParameter
		- fogShadingParameter * length(eyeVec) / fogDistance, 0.0, 1.0);
	col.rgb = mix(fogColor.rgb, col.rgb, clarity);

	// 2. Истинная мировая высота относительно игрока (в блоках)
	float playerRelY = v_worldRelY;

	// Логика затухания из MC
	float fadeDirection = (playerRelY + 5.0) / 10.0;
	float fadeIntensity = abs(playerRelY / 40.0);

	float topFadeHeight = v_relY;
	float bottomFadeHeight = 1.0 - v_relY;

	float fadeHeight = mix(topFadeHeight, bottomFadeHeight, clamp(fadeDirection, 0.0, 1.0));
	float fadeFinal = mix(1.0, fadeHeight, clamp(fadeIntensity, 0.0, 1.0));

	col.a *= fadeFinal;

	gl_FragColor = col;
}