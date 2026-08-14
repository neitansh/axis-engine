// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "test.h"

#include "client/bedrock/geometry.h"
#include "client/bedrock/animation.h"
#include "constants.h"
#include "SkinnedMesh.h"
#include "IMeshBuffer.h"
#include "Transform.h"
#include "matrix4.h"
#include "quaternion.h"

#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

/*
 * Перенос моделей Blockbench/GeckoLib в движок: проверка по матрицам.
 *
 * Эталон здесь не «как было вчера», а GeckoLib — код, которым эти самые файлы
 * читает Minecraft. Он переписан в этом файле дословно с декомпилированных
 * RenderUtils.prepMatrixForBone и BakedModelFactory.constructBone/constructCube,
 * своей арифметикой: столбцовые векторы, M·v, никаких соглашений движка.
 * Поэтому тест ловит не только нашу ошибку, но и молчаливую смену соглашений
 * в самом Irrlicht — а таких соглашений там два, и оба неочевидны:
 *
 *   1. core::Transform хранит поворот ОБРАТНЫМ: buildMatrix берёт у
 *      кватерниона транспонированную матрицу;
 *   2. произведение кватернионов записано задом наперёд: a * b — это b·a.
 *
 * Оба закреплены отдельным случаем ниже: если упадёт он, значит поменялся
 * движок, а не наш загрузчик, и чинить надо перевод в bedrock/convert.h.
 *
 * Связь пространств. Точка файла p (пиксели) попадает:
 *   в Minecraft  — как Φ(p)/16,  Φ = diag(−1, 1, 1)   (зеркало по X)
 *   в движок     — как Ψ(p)·BS/16, Ψ = diag(−1, 1, −1)
 * Отсюда v_движка = K · v_minecraft при K = diag(BS, BS, −BS), а матрица
 * сустава — K · M_minecraft · K⁻¹. Это и сравнивается.
 */

namespace
{

/// Своя матрица 4×4: m[строка][столбец], столбцовые векторы, M·v.
struct Ref
{
	double m[4][4];

	static Ref id()
	{
		Ref r{};
		for (int i = 0; i < 4; ++i)
			r.m[i][i] = 1;
		return r;
	}
};

Ref operator*(const Ref &a, const Ref &b)
{
	Ref r{};
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j) {
			double s = 0;
			for (int k = 0; k < 4; ++k)
				s += a.m[i][k] * b.m[k][j];
			r.m[i][j] = s;
		}
	return r;
}

Ref refTranslate(double x, double y, double z)
{
	Ref r = Ref::id();
	r.m[0][3] = x; r.m[1][3] = y; r.m[2][3] = z;
	return r;
}

Ref refScale(double x, double y, double z)
{
	Ref r = Ref::id();
	r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z;
	return r;
}

Ref refRotX(double a)
{
	Ref r = Ref::id();
	const double c = std::cos(a), s = std::sin(a);
	r.m[1][1] = c; r.m[1][2] = -s; r.m[2][1] = s; r.m[2][2] = c;
	return r;
}

Ref refRotY(double a)
{
	Ref r = Ref::id();
	const double c = std::cos(a), s = std::sin(a);
	r.m[0][0] = c; r.m[0][2] = s; r.m[2][0] = -s; r.m[2][2] = c;
	return r;
}

Ref refRotZ(double a)
{
	Ref r = Ref::id();
	const double c = std::cos(a), s = std::sin(a);
	r.m[0][0] = c; r.m[0][1] = -s; r.m[1][0] = s; r.m[1][1] = c;
	return r;
}

struct Vec { double x = 0, y = 0, z = 0; };

Vec refApply(const Ref &m, const Vec &v)
{
	return {m.m[0][0]*v.x + m.m[0][1]*v.y + m.m[0][2]*v.z + m.m[0][3],
			m.m[1][0]*v.x + m.m[1][1]*v.y + m.m[1][2]*v.z + m.m[1][3],
			m.m[2][0]*v.x + m.m[2][1]*v.y + m.m[2][2]*v.z + m.m[2][3]};
}

constexpr double DEG = 3.14159265358979323846 / 180.0;

/// Матрица кости по GeckoLib, в model space Minecraft (блоки, правая тройка).
/// RenderUtils.prepMatrixForBone поверх BakedModelFactory.constructBone:
/// точка вращения зеркалится по X, углы вокруг X и Y меняют знак, порядок —
/// Rz·Ry·Rx, и всё это между сдвигом в точку вращения и обратно.
Ref geckoBone(Vec pivot_px, Vec rot_deg, Vec pos_px, Vec scale)
{
	const Vec p{-pivot_px.x / 16.0, pivot_px.y / 16.0, pivot_px.z / 16.0};
	Ref m = refTranslate(-pos_px.x / 16.0, pos_px.y / 16.0, pos_px.z / 16.0);
	m = m * refTranslate(p.x, p.y, p.z);
	m = m * refRotZ(rot_deg.z * DEG);
	m = m * refRotY(-rot_deg.y * DEG);
	m = m * refRotX(-rot_deg.x * DEG);
	m = m * refScale(scale.x, scale.y, scale.z);
	m = m * refTranslate(-p.x, -p.y, -p.z);
	return m;
}

/// Из осей Minecraft в оси движка: K · M · K⁻¹.
Ref toEngineSpace(const Ref &mc)
{
	return refScale(BS, BS, -BS) * mc * refScale(1.0 / BS, 1.0 / BS, -1.0 / BS);
}

/// Матрицу движка читаем через образы базисных векторов: так тест не зависит
/// от того, как именно уложены числа внутри matrix4.
Ref fromEngine(const core::matrix4 &m)
{
	Ref r = Ref::id();
	core::vector3df o, e[3];
	m.transformVect(o, core::vector3df(0, 0, 0));
	m.transformVect(e[0], core::vector3df(1, 0, 0));
	m.transformVect(e[1], core::vector3df(0, 1, 0));
	m.transformVect(e[2], core::vector3df(0, 0, 1));
	for (int j = 0; j < 3; ++j) {
		r.m[0][j] = e[j].X - o.X;
		r.m[1][j] = e[j].Y - o.Y;
		r.m[2][j] = e[j].Z - o.Z;
	}
	r.m[0][3] = o.X; r.m[1][3] = o.Y; r.m[2][3] = o.Z;
	return r;
}

double maxDiff(const Ref &a, const Ref &b)
{
	double d = 0;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 4; ++j)
			d = std::max(d, std::abs(a.m[i][j] - b.m[i][j]));
	return d;
}

/// Кость модели, как её описывают в тесте.
struct Bone
{
	std::string name, parent;
	Vec pivot, rotation;
};

std::string geoJson(const std::vector<Bone> &bones)
{
	std::ostringstream o;
	o << R"({"format_version":"1.12.0","minecraft:geometry":[{"description":)"
	  << R"({"identifier":"geometry.test","texture_width":16,"texture_height":16},)"
	  << R"("bones":[)";
	for (size_t i = 0; i < bones.size(); ++i) {
		const Bone &b = bones[i];
		if (i)
			o << ",";
		o << R"({"name":")" << b.name << '"';
		if (!b.parent.empty())
			o << R"(,"parent":")" << b.parent << '"';
		o << R"(,"pivot":[)" << b.pivot.x << ',' << b.pivot.y << ',' << b.pivot.z << ']'
		  << R"(,"rotation":[)" << b.rotation.x << ',' << b.rotation.y << ','
		  << b.rotation.z << ']'
		  << R"(,"cubes":[{"origin":[0,0,0],"size":[1,1,1],"uv":[0,0]}]})";
	}
	o << "]}]}";
	return o.str();
}

/// Матрицы суставов, какими их получит отрисовка: поза либо кадр дорожки.
std::vector<core::matrix4> skinMatrices(scene::SkinnedMesh *mesh,
		const std::string &track = "", f32 time = 0.0f)
{
	std::vector<core::matrix4> mats;
	if (track.empty()) {
		for (const auto *joint : mesh->getAllJoints()) {
			if (const auto *t = std::get_if<core::Transform>(&joint->transform))
				mats.push_back(t->buildMatrix());
			else
				mats.push_back(std::get<core::matrix4>(joint->transform));
		}
	} else {
		const auto nr = mesh->getTrackNumber(track);
		const std::vector<std::optional<core::Transform>> old(
				mesh->getAllJoints().size());
		for (const auto &t : mesh->animateMesh({{*nr, time, 1.0f}}, old)) {
			if (const auto *tr = std::get_if<core::Transform>(&t))
				mats.push_back(tr->buildMatrix());
			else
				mats.push_back(std::get<core::matrix4>(t));
		}
	}
	mesh->calculateGlobalMatrices(mats);
	return mesh->calculateSkinMatrices(mats);
}

} // namespace

class TestBedrock : public TestBase
{
public:
	TestBedrock() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestBedrock"; }

	void runTests(IGameDef *gamedef);

	void testEngineConventions();
	void testRestPose();
	void testSingleAxisRotations();
	void testCombinedRotations();
	void testPivotAndHierarchy();
	void testMirroredArms();
	void testCubeRotation();
	void testAnimationFromMod();
	void testAnimationPositionAndScale();
};

static TestBedrock g_test_instance;

void TestBedrock::runTests(IGameDef *gamedef)
{
	TEST(testEngineConventions);
	TEST(testRestPose);
	TEST(testSingleAxisRotations);
	TEST(testCombinedRotations);
	TEST(testPivotAndHierarchy);
	TEST(testMirroredArms);
	TEST(testCubeRotation);
	TEST(testAnimationFromMod);
	TEST(testAnimationPositionAndScale);
}

////////////////////////////////////////////////////////////////////////////////

namespace
{

/// Проверить одну модель в покое против GeckoLib.
void checkPose(const std::vector<Bone> &bones, double tolerance = 1e-4)
{
	scene::SkinnedMesh *mesh = bedrock::GeometryLoader::build(geoJson(bones), "test");
	UASSERT(mesh);
	const auto skin = skinMatrices(mesh);

	std::map<std::string, Ref> want;
	for (const Bone &b : bones) {
		Ref m = geckoBone(b.pivot, b.rotation, {}, {1, 1, 1});
		want[b.name] = b.parent.empty() ? m : want[b.parent] * m;
	}

	for (size_t i = 0; i < mesh->getAllJoints().size(); ++i) {
		// Имя копируем: сообщение об ошибке переживёт mesh->drop().
		const std::string name = *mesh->getAllJoints()[i]->Name;
		const double d = maxDiff(toEngineSpace(want[name]), fromEngine(skin[i]));
		if (d >= tolerance) {
			mesh->drop();
			UTEST(false, "кость %s разошлась с GeckoLib на %f", name.c_str(), d);
		}
	}
	mesh->drop();
}

} // namespace

// Два соглашения Irrlicht, на которых держится перевод поворотов. Если этот
// случай упал — поменялся движок, и чинить надо bedrock/convert.h.
void TestBedrock::testEngineConventions()
{
	const core::quaternion qy(0, 90 * core::DEGTORAD, 0);

	// 1. q * v — обычный поворот: вокруг Y на +90° уводит X в −Z.
	const core::vector3df turned = qy * core::vector3df(1, 0, 0);
	UASSERT(std::abs(turned.X) < 1e-5);
	UASSERT(std::abs(turned.Z + 1) < 1e-5);

	// 2. А Transform::buildMatrix применяет ОБРАТНЫЙ поворот: тот же X уходит
	// в +Z. Ради этого loader и кладёт в сустав makeInverse().
	core::Transform t;
	t.rotation = qy;
	core::vector3df built;
	t.buildMatrix().transformVect(built, core::vector3df(1, 0, 0));
	UASSERT(std::abs(built.X) < 1e-5);
	UASSERT(std::abs(built.Z - 1) < 1e-5);

	// 3. Произведение кватернионов записано задом наперёд: a * b — это b·a.
	const core::quaternion qx(90 * core::DEGTORAD, 0, 0);
	const core::vector3df by_product = (qx * qy) * core::vector3df(0, 0, 1);
	const core::vector3df by_hand = qy * (qx * core::vector3df(0, 0, 1));
	UASSERT((by_product - by_hand).getLength() < 1e-5);

	// 4. Матрицы, наоборот, умножаются как принято: A * B — сперва B.
	core::matrix4 shift, spin;
	shift.setTranslation(core::vector3df(1, 0, 0));
	spin.setRotationDegrees(core::vector3df(0, 90, 0));
	core::vector3df out;
	(shift * spin).transformVect(out, core::vector3df(0, 0, 1));
	UASSERT((out - core::vector3df(2, 0, 0)).getLength() < 1e-5);
}

void TestBedrock::testRestPose()
{
	// Кость без поворота: сустав обязан дать тождественную матрицу, иначе
	// модель поедет ещё до всякой анимации.
	checkPose({{"root", "", {0, 0, 0}, {0, 0, 0}}});
	checkPose({{"root", "", {7, -3, 11}, {0, 0, 0}}});
}

void TestBedrock::testSingleAxisRotations()
{
	for (const double a : {90.0, -90.0, 45.0, -45.0, 180.0}) {
		checkPose({{"b", "", {0, 0, 0}, {a, 0, 0}}});
		checkPose({{"b", "", {0, 0, 0}, {0, a, 0}}});
		checkPose({{"b", "", {0, 0, 0}, {0, 0, a}}});
	}
}

void TestBedrock::testCombinedRotations()
{
	// Порядок осей виден только там, где повёрнуто больше чем по одной.
	checkPose({{"b", "", {0, 0, 0}, {30, 40, 0}}});
	checkPose({{"b", "", {0, 0, 0}, {30, 0, 50}}});
	checkPose({{"b", "", {0, 0, 0}, {0, 40, 50}}});
	checkPose({{"b", "", {0, 0, 0}, {30, 40, 50}}});
	checkPose({{"b", "", {0, 0, 0}, {-107.8, 25.1, -199.9}}});
}

void TestBedrock::testPivotAndHierarchy()
{
	// Поворот вокруг собственной точки, а не вокруг начала координат.
	checkPose({{"b", "", {3, 5, -2}, {0, 0, 45}}});
	checkPose({{"b", "", {3, 5, -2}, {20, -35, 45}}});

	// Наследование: точка потомка задана в общих координатах модели, и
	// поворот родителя должен её уносить.
	checkPose({
		{"root", "", {0, 0, 0}, {0, 30, 0}},
		{"child", "root", {4, 6, 2}, {20, 0, 10}}});
	checkPose({
		{"root", "", {1, 2, 3}, {15, 30, -45}},
		{"child", "root", {4, 6, 2}, {20, 0, 10}},
		{"grandchild", "child", {-2, 9, 5}, {0, -60, 25}}});
}

void TestBedrock::testMirroredArms()
{
	// Зеркальные кости с зеркальными поворотами. Ошибка в знаке оси развела
	// бы их в одну сторону, а не в разные.
	checkPose({
		{"body", "", {0, 0, 0}, {0, 0, 0}},
		{"left_arm", "body", {6, 5.9068, 6.00305}, {0, 45, 0}},
		{"right_arm", "body", {-6, 3.9068, 6.00305}, {0, -45, 0}}});
	checkPose({
		{"body", "", {0, 0, 0}, {0, 0, 0}},
		{"left_arm", "body", {6, 0, 0}, {30, 45, 60}},
		{"right_arm", "body", {-6, 0, 0}, {30, -45, -60}}});
}

void TestBedrock::testCubeRotation()
{
	// У куба свой поворот, помимо поворота кости, и переводится он тем же
	// правилом. Сверяем восемь углов.
	struct Case { Vec origin, size, pivot, rotation; };
	const Case cases[] = {
		{{0, 0, 0}, {2, 3, 4}, {1, 1, 1}, {0, 0, 0}},
		{{-1, 2, 3}, {2, 3, 4}, {0, 3, 5}, {0, 0, -45}},
		{{-1, 2, 3}, {2, 3, 4}, {0, 3, 5}, {-45, 0, 0}},
		{{-1, 2, 3}, {2, 3, 4}, {0, 3, 5}, {0, -45, 0}},
		{{-1, 2, 3}, {2, 3, 4}, {0, 3, 5}, {-12.5, 30, 22.5}},
	};

	for (const Case &c : cases) {
		std::ostringstream o;
		o << R"({"format_version":"1.12.0","minecraft:geometry":[{"description":)"
		  << R"({"identifier":"geometry.test","texture_width":16,"texture_height":16},)"
		  << R"("bones":[{"name":"b","pivot":[0,0,0],"cubes":[{"origin":[)"
		  << c.origin.x << ',' << c.origin.y << ',' << c.origin.z << R"(],"size":[)"
		  << c.size.x << ',' << c.size.y << ',' << c.size.z << R"(],"pivot":[)"
		  << c.pivot.x << ',' << c.pivot.y << ',' << c.pivot.z << R"(],"rotation":[)"
		  << c.rotation.x << ',' << c.rotation.y << ',' << c.rotation.z
		  << R"(],"uv":[0,0]}]}]}]})";

		scene::SkinnedMesh *mesh = bedrock::GeometryLoader::build(o.str(), "test");
		UASSERT(mesh);

		// Эталон: BakedModelFactory.constructCube + rotateMatrixAroundCube.
		const Vec pivot_mc{-c.pivot.x / 16.0, c.pivot.y / 16.0, c.pivot.z / 16.0};
		Ref turn = refTranslate(pivot_mc.x, pivot_mc.y, pivot_mc.z);
		turn = turn * refRotZ(c.rotation.z * DEG);
		turn = turn * refRotY(-c.rotation.y * DEG);
		turn = turn * refRotX(-c.rotation.x * DEG);
		turn = turn * refTranslate(-pivot_mc.x, -pivot_mc.y, -pivot_mc.z);

		std::vector<Vec> want;
		for (int i = 0; i < 8; ++i) {
			const Vec corner{
				(c.origin.x + ((i & 1) ? c.size.x : 0)) / -16.0,
				(c.origin.y + ((i & 2) ? c.size.y : 0)) / 16.0,
				(c.origin.z + ((i & 4) ? c.size.z : 0)) / 16.0};
			const Vec mc = refApply(turn, corner);
			want.push_back({mc.x * BS, mc.y * BS, -mc.z * BS});
		}

		const scene::IMeshBuffer *buffer = mesh->getMeshBuffer(0u);
		for (u32 v = 0; v < buffer->getVertexCount(); ++v) {
			const core::vector3df p = buffer->getPosition(v);
			double best = 1e9;
			for (const Vec &w : want)
				best = std::min(best, std::max({std::abs(w.x - p.X),
						std::abs(w.y - p.Y), std::abs(w.z - p.Z)}));
			if (best >= 1e-4) {
				mesh->drop();
				UTEST(false, "угол куба разошёлся с GeckoLib на %f", best);
			}
		}
		mesh->drop();
	}
}

void TestBedrock::testAnimationFromMod()
{
	// Числа настоящие: штурмовая винтовка Just Enough Guns. Поза рук из idle
	// и самый развёрнутый ключ ствола из draw — те два места, на которых
	// перенос поворотов и ломался.
	const std::vector<Bone> bones = {
		{"gun_body", "", {0, 2.5, 0}, {0, 0, 0}},
		{"left_arm", "gun_body", {6, 5.9068, 6.00305}, {0, 0, 0}},
		{"right_arm", "gun_body", {-6, 3.9068, 6.00305}, {0, 0, 0}},
	};
	const char *anim = R"({"format_version":"1.8.0","animations":{
		"idle":{"loop":true,"bones":{
			"left_arm":{"rotation":[73.95054,-26.20116,-129.71317],
			            "position":[-2.5,-4.2,-0.85]},
			"right_arm":{"rotation":[90,0,-180],"position":[5.65,-2.2,5]}}},
		"draw":{"animation_length":1.5,"bones":{
			"gun_body":{"rotation":{"0.75":{"vector":[0.3744,33.4131,99.0833]},
			                        "1.5":{"vector":[0,0,0]}},
			            "position":{"0.75":{"vector":[3,3.6,2.4]}}}}}}})";

	struct Case
	{
		const char *track;
		f32 time;
		std::map<std::string, std::pair<Vec, Vec>> pose; ///< поворот и сдвиг
	};
	const Case cases[] = {
		{"idle", 0.02f, {
			{"left_arm", {{73.95054, -26.20116, -129.71317}, {-2.5, -4.2, -0.85}}},
			{"right_arm", {{90, 0, -180}, {5.65, -2.2, 5}}}}},
		{"draw", 0.75f, {
			{"gun_body", {{0.3744, 33.4131, 99.0833}, {3, 3.6, 2.4}}}}},
		{"draw", 1.5f, {
			{"gun_body", {{0, 0, 0}, {3, 3.6, 2.4}}}}},
	};

	for (const Case &c : cases) {
		scene::SkinnedMesh *mesh = bedrock::GeometryLoader::build(geoJson(bones), "test");
		UASSERT(mesh);
		UASSERT(bedrock::loadAnimations(mesh, anim, "test") == 2);
		const auto skin = skinMatrices(mesh, c.track, c.time);

		std::map<std::string, Ref> want;
		for (const Bone &b : bones) {
			Vec rotation = b.rotation, offset{};
			const auto it = c.pose.find(b.name);
			if (it != c.pose.end()) {
				rotation = {rotation.x + it->second.first.x,
						rotation.y + it->second.first.y,
						rotation.z + it->second.first.z};
				offset = it->second.second;
			}
			Ref m = geckoBone(b.pivot, rotation, offset, {1, 1, 1});
			want[b.name] = b.parent.empty() ? m : want[b.parent] * m;
		}

		for (size_t i = 0; i < mesh->getAllJoints().size(); ++i) {
			const std::string name = *mesh->getAllJoints()[i]->Name;
			const double d = maxDiff(toEngineSpace(want[name]), fromEngine(skin[i]));
			if (d >= 1e-3) {
				mesh->drop();
				UTEST(false, "%s: кость %s разошлась с GeckoLib на %f",
						c.track, name.c_str(), d);
			}
		}
		mesh->drop();
	}
}

void TestBedrock::testAnimationPositionAndScale()
{
	// Сдвиг в Bedrock отсчитывается от места самой кости, масштаб осей не
	// переставляет. И то и другое должно доживать до конца дорожки.
	const std::vector<Bone> bones = {
		{"root", "", {2, 3, 4}, {0, 0, 0}},
		{"part", "root", {5, -1, 7}, {10, 20, 30}},
	};
	const char *anim = R"({"format_version":"1.8.0","animations":{"move":{
		"animation_length":1.0,"bones":{
			"part":{"position":{"0.0":{"vector":[0,0,0]},
			                    "1.0":{"vector":[6,-2,9]}},
			        "scale":{"0.0":{"vector":[1,1,1]},
			                 "1.0":{"vector":[2,0.5,3]}}}}}}})";

	for (const f32 time : {0.0f, 0.5f, 1.0f}) {
		scene::SkinnedMesh *mesh = bedrock::GeometryLoader::build(geoJson(bones), "test");
		UASSERT(mesh);
		UASSERT(bedrock::loadAnimations(mesh, anim, "test") == 1);
		const auto skin = skinMatrices(mesh, "move", time);

		const double t = time;
		std::map<std::string, Ref> want;
		want["root"] = geckoBone(bones[0].pivot, bones[0].rotation, {}, {1, 1, 1});
		want["part"] = want["root"] * geckoBone(bones[1].pivot, bones[1].rotation,
				{6 * t, -2 * t, 9 * t},
				{1 + t, 1 - 0.5 * t, 1 + 2 * t});

		for (size_t i = 0; i < mesh->getAllJoints().size(); ++i) {
			const std::string name = *mesh->getAllJoints()[i]->Name;
			const double d = maxDiff(toEngineSpace(want[name]), fromEngine(skin[i]));
			if (d >= 1e-3) {
				mesh->drop();
				UTEST(false, "t=%f: кость %s разошлась с GeckoLib на %f",
						(double)time, name.c_str(), d);
			}
		}
		mesh->drop();
	}
}
