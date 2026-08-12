uniform lowp vec4 materialColor;

VARYING_ lowp vec4 varColor;
VARYING_ highp vec3 eyeVec;
VARYING_ mediump float v_relY;
VARYING_ highp float v_worldRelY; // Настоящая высота относительно игрока в мире

void main(void)
{
	gl_Position = mWorldViewProj * inVertexPosition;

	vec4 color = inVertexColor;
	color *= materialColor;
	varColor = color;

	// Позиция вершины относительно камеры в пространстве вида
	vec3 vertexEye = (mWorldView * inVertexPosition).xyz;
	eyeVec = -vertexEye;

	// mWorldView[1].xyz — это вектор МИРОВОГО ВЕРХА в пространстве камеры.
	// Скалярное произведение даёт точную высоту вершины в блоках (1 блок = 10 единиц)
	v_worldRelY = dot(vertexEye, mWorldView[1].xyz) / 10.0;

	v_relY = inTexCoord0.y;
}