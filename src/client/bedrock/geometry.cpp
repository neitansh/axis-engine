// Copyright (C) 2026 the-axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "geometry.h"
#include "convert.h"
#include "json.h"
#include "log.h"
#include "IReadFile.h"
#include "SSkinMeshBuffer.h"
#include "S3DVertex.h"
#include "matrix4.h"
#include "irr_v2d.h"

#include <json/json.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrock
{

namespace
{

/// Грани куба. Порядок и смысл — как в Bedrock: имена сторон света, а не осей.
enum class Face : u8 { NORTH, SOUTH, EAST, WEST, UP, DOWN };

const char *faceName(Face face)
{
	switch (face) {
	case Face::NORTH: return "north";
	case Face::SOUTH: return "south";
	case Face::EAST:  return "east";
	case Face::WEST:  return "west";
	case Face::UP:    return "up";
	case Face::DOWN:  return "down";
	}
	return "";
}

struct FaceUV
{
	v2f uv;        ///< левый верхний угол в пикселях текстуры
	v2f uv_size;   ///< размер; отрицательный переворачивает
	int rotation = 0; ///< поворот картинки на грани, кратный 90°
	bool present = false;
};

struct Cube
{
	v3f origin;      ///< угол с наименьшими координатами, в пикселях
	v3f size;
	v3f pivot;       ///< вокруг чего повёрнут сам куб
	v3f rotation;    ///< градусы
	f32 inflate = 0.0f;
	bool mirror = false;
	bool has_rotation = false;
	/// Развёртка: либо шесть граней поимённо, либо один угол «коробкой».
	bool box_uv = false;
	v2f box_uv_origin;
	FaceUV faces[6];
};

struct Bone
{
	std::string name;
	std::string parent;
	v3f pivot;
	v3f rotation;
	f32 inflate = 0.0f;
	bool mirror = false;
	std::vector<Cube> cubes;

	// Заполняется при сборке
	s32 joint_id = -1;
	s32 parent_index = -1;
};

v3f readVec3(const Json::Value &value, const v3f &fallback = v3f(0, 0, 0))
{
	if (!value.isArray() || value.size() < 3)
		return fallback;
	return v3f(value[0].asFloat(), value[1].asFloat(), value[2].asFloat());
}

v2f readVec2(const Json::Value &value, const v2f &fallback = v2f(0, 0))
{
	if (!value.isArray() || value.size() < 2)
		return fallback;
	return v2f(value[0].asFloat(), value[1].asFloat());
}

void readCubeUV(const Json::Value &uv, Cube &cube)
{
	if (uv.isArray()) {
		// Развёртка коробкой: один угол, остальное раскладывается по размеру.
		cube.box_uv = true;
		cube.box_uv_origin = readVec2(uv);
		return;
	}
	if (!uv.isObject())
		return;

	for (u8 i = 0; i < 6; ++i) {
		const Json::Value &face = uv[faceName(static_cast<Face>(i))];
		if (!face.isObject())
			continue;
		FaceUV &out = cube.faces[i];
		out.uv = readVec2(face["uv"]);
		out.uv_size = readVec2(face["uv_size"]);
		if (face.isMember("uv_rotation"))
			out.rotation = ((face["uv_rotation"].asInt() % 360) + 360) % 360;
		out.present = true;
	}
}

/// Восемь углов куба в пикселях Bedrock, с раздутием.
struct Corners
{
	f32 x0, x1, y0, y1, z0, z1;

	Corners(const Cube &cube, f32 inflate)
	{
		x0 = cube.origin.X - inflate;
		y0 = cube.origin.Y - inflate;
		z0 = cube.origin.Z - inflate;
		x1 = cube.origin.X + cube.size.X + inflate;
		y1 = cube.origin.Y + cube.size.Y + inflate;
		z1 = cube.origin.Z + cube.size.Z + inflate;
	}
};

/*
 * Четвёрки вершин граней. Порядок внутри четвёрки не произволен: на него
 * ложится развёртка, и он повторён за Minecraft (GeckoLib, VertexSet) — там
 * координата X зеркальна нашей, поэтому «дальний» угол у них соответствует
 * большему X здесь. Менять порядок нельзя: развёртка съедет.
 */
void faceCorners(Face face, const Corners &c, v3f out[4])
{
	switch (face) {
	case Face::WEST:
		out[0] = v3f(c.x1, c.y1, c.z1); out[1] = v3f(c.x1, c.y1, c.z0);
		out[2] = v3f(c.x1, c.y0, c.z0); out[3] = v3f(c.x1, c.y0, c.z1);
		break;
	case Face::EAST:
		out[0] = v3f(c.x0, c.y1, c.z0); out[1] = v3f(c.x0, c.y1, c.z1);
		out[2] = v3f(c.x0, c.y0, c.z1); out[3] = v3f(c.x0, c.y0, c.z0);
		break;
	case Face::NORTH:
		out[0] = v3f(c.x1, c.y1, c.z0); out[1] = v3f(c.x0, c.y1, c.z0);
		out[2] = v3f(c.x0, c.y0, c.z0); out[3] = v3f(c.x1, c.y0, c.z0);
		break;
	case Face::SOUTH:
		out[0] = v3f(c.x0, c.y1, c.z1); out[1] = v3f(c.x1, c.y1, c.z1);
		out[2] = v3f(c.x1, c.y0, c.z1); out[3] = v3f(c.x0, c.y0, c.z1);
		break;
	case Face::UP:
		out[0] = v3f(c.x1, c.y1, c.z1); out[1] = v3f(c.x0, c.y1, c.z1);
		out[2] = v3f(c.x0, c.y1, c.z0); out[3] = v3f(c.x1, c.y1, c.z0);
		break;
	case Face::DOWN:
		out[0] = v3f(c.x1, c.y0, c.z0); out[1] = v3f(c.x0, c.y0, c.z0);
		out[2] = v3f(c.x0, c.y0, c.z1); out[3] = v3f(c.x1, c.y0, c.z1);
		break;
	}
}

/// Наружная нормаль грани — уже в осях движка.
v3f faceNormal(Face face)
{
	switch (face) {
	// Запад в файле лежит при большем X, а движок переворачивает X: значит
	// наружу — в минус. То же с севером и Z.
	case Face::WEST:  return v3f(-1, 0, 0);
	case Face::EAST:  return v3f(1, 0, 0);
	case Face::NORTH: return v3f(0, 0, 1);
	case Face::SOUTH: return v3f(0, 0, -1);
	case Face::UP:    return v3f(0, 1, 0);
	case Face::DOWN:  return v3f(0, -1, 0);
	}
	return v3f(0, 1, 0);
}

/// Развёртка коробкой: где на текстуре лежит эта грань. Числа — из Minecraft,
/// который так читает старые модели; размеры округляются вниз, как и там.
void boxUV(Face face, const Cube &cube, v2f &uv, v2f &uv_size)
{
	const v2f o = cube.box_uv_origin;
	const f32 sx = std::floor(cube.size.X);
	const f32 sy = std::floor(cube.size.Y);
	const f32 sz = std::floor(cube.size.Z);

	switch (face) {
	case Face::WEST:
		uv = v2f(o.X + sz + sx, o.Y + sz); uv_size = v2f(sz, sy); break;
	case Face::EAST:
		uv = v2f(o.X, o.Y + sz);           uv_size = v2f(sz, sy); break;
	case Face::NORTH:
		uv = v2f(o.X + sz, o.Y + sz);      uv_size = v2f(sx, sy); break;
	case Face::SOUTH:
		uv = v2f(o.X + sz + sx + sz, o.Y + sz); uv_size = v2f(sx, sy); break;
	case Face::UP:
		uv = v2f(o.X + sz, o.Y);           uv_size = v2f(sx, sz); break;
	case Face::DOWN:
		uv = v2f(o.X + sz + sx, o.Y + sz); uv_size = v2f(sx, -sz); break;
	}
}

/// Углы развёртки по вершинам грани, с учётом поворота картинки.
void faceUVs(const FaceUV &face, f32 tex_w, f32 tex_h, bool mirror, v2f out[4])
{
	f32 u0 = face.uv.X / tex_w;
	f32 v0 = face.uv.Y / tex_h;
	f32 u1 = (face.uv.X + face.uv_size.X) / tex_w;
	f32 v1 = (face.uv.Y + face.uv_size.Y) / tex_h;

	// Незеркальная грань берётся развёрнутой по горизонтали: модель Bedrock
	// зеркальна тому, как её рисуют, и развёртка это учитывает.
	if (!mirror)
		std::swap(u0, u1);

	const v2f corners[4] = {v2f(u0, v0), v2f(u1, v0), v2f(u1, v1), v2f(u0, v1)};
	const int shift = face.rotation / 90;
	for (int i = 0; i < 4; ++i)
		out[i] = corners[(i + shift) % 4];
}

/*
 * Поворот куба вокруг собственной точки — в осях движка, а не в пикселях
 * Bedrock. Разница не косметическая: перевод осей меняет знаки у поворотов
 * вокруг X и Z, поэтому куб, повёрнутый в исходных осях, ляжет зеркально
 * тому, как его кладёт Minecraft. Поворот у куба ровно тот же, что у кости,
 * и берётся тем же переводом — другого правила у формата нет.
 */
v3f rotateAroundPivot(const v3f &point, const Cube &cube)
{
	if (!cube.has_rotation)
		return point;

	const v3f pivot = toEngine(cube.pivot);
	return bedrockRotation(cube.rotation) * (point - pivot) + pivot;
}

bool readBones(const Json::Value &bones_json, std::vector<Bone> &bones,
		const std::string &name)
{
	for (const Json::Value &bone_json : bones_json) {
		if (!bone_json.isObject())
			continue;

		Bone bone;
		bone.name = bone_json["name"].asString();
		if (bone.name.empty()) {
			warningstream << "Bedrock: " << name
					<< ": кость без имени пропущена" << std::endl;
			continue;
		}
		if (bone_json.isMember("parent"))
			bone.parent = bone_json["parent"].asString();
		bone.pivot = readVec3(bone_json["pivot"]);
		bone.rotation = readVec3(bone_json["rotation"]);
		if (bone_json.isMember("inflate"))
			bone.inflate = bone_json["inflate"].asFloat();
		if (bone_json.isMember("mirror"))
			bone.mirror = bone_json["mirror"].asBool();

		for (const Json::Value &cube_json : bone_json["cubes"]) {
			if (!cube_json.isObject())
				continue;
			Cube cube;
			cube.origin = readVec3(cube_json["origin"]);
			cube.size = readVec3(cube_json["size"]);
			cube.pivot = readVec3(cube_json["pivot"]);
			cube.rotation = readVec3(cube_json["rotation"]);
			cube.has_rotation = cube_json.isMember("rotation");
			cube.inflate = cube_json.isMember("inflate")
					? cube_json["inflate"].asFloat() : bone.inflate;
			cube.mirror = cube_json.isMember("mirror")
					? cube_json["mirror"].asBool() : bone.mirror;
			readCubeUV(cube_json["uv"], cube);
			bone.cubes.push_back(std::move(cube));
		}

		bones.push_back(std::move(bone));
	}
	return !bones.empty();
}

} // namespace

bool GeometryLoader::isALoadableFileExtension(const io::path &filename) const
{
	const std::string name(filename.c_str());
	const std::string ext(GEOMETRY_EXT);
	return name.size() >= ext.size()
			&& name.compare(name.size() - ext.size(), ext.size(), ext) == 0;
}

scene::IAnimatedMesh *GeometryLoader::createMesh(io::IReadFile *file)
{
	if (!file)
		return nullptr;

	const long size = file->getSize();
	std::string data(static_cast<size_t>(std::max(0L, size)), '\0');
	if (size > 0)
		file->read(&data[0], size);

	return build(data, file->getFileName().c_str());
}

scene::SkinnedMesh *GeometryLoader::build(const std::string &json,
		const std::string &name)
{
	Json::Value root;
	if (!parseJson(json, root, name))
		return nullptr;

	const Json::Value &geometries = root["minecraft:geometry"];
	if (!geometries.isArray() || geometries.empty()) {
		errorstream << "Bedrock: " << name << ": нет minecraft:geometry."
				" Модель должна быть выгружена из Blockbench как"
				" Bedrock Geometry (format_version 1.12 и новее)." << std::endl;
		return nullptr;
	}
	// Файл может нести несколько моделей; берём первую — остальные в этом
	// формате встречаются только как варианты одной и той же вещи.
	const Json::Value &geometry = geometries[0];

	const Json::Value &desc = geometry["description"];
	const f32 tex_w = desc.get("texture_width", 16).asFloat();
	const f32 tex_h = desc.get("texture_height", 16).asFloat();
	if (tex_w <= 0 || tex_h <= 0) {
		errorstream << "Bedrock: " << name << ": нулевой размер текстуры"
				<< std::endl;
		return nullptr;
	}

	std::vector<Bone> bones;
	if (!readBones(geometry["bones"], bones, name)) {
		errorstream << "Bedrock: " << name << ": в модели нет костей"
				<< std::endl;
		return nullptr;
	}

	std::unordered_map<std::string, s32> by_name;
	for (size_t i = 0; i < bones.size(); ++i)
		by_name[bones[i].name] = static_cast<s32>(i);
	for (Bone &bone : bones) {
		if (bone.parent.empty())
			continue;
		auto it = by_name.find(bone.parent);
		if (it == by_name.end()) {
			warningstream << "Bedrock: " << name << ": у кости " << bone.name
					<< " нет родителя " << bone.parent
					<< ", считаю её корневой" << std::endl;
			continue;
		}
		bone.parent_index = it->second;
	}

	scene::SkinnedMeshBuilder builder(scene::SkinnedMesh::SourceFormat::OTHER);

	/*
	 * Скелет. Точка вращения кости в Bedrock задана в общих координатах
	 * модели, а суставу нужна своя, от родителя — отсюда вычитание.
	 *
	 * Поворот кости из файла — это поза, а не привязка: вершины лежат в
	 * координатах модели, до всякого поворота. Поэтому обратную матрицу
	 * привязки задаём здесь сами — один сдвиг в точку вращения. Не задать её
	 * значило бы привязать вершины к уже повёрнутой кости, и модель со
	 * скошенными костями сложилась бы вдвое.
	 */
	std::vector<scene::SkinnedMesh::SJoint *> joints(bones.size(), nullptr);
	for (size_t i = 0; i < bones.size(); ++i) {
		Bone &bone = bones[i];
		scene::SkinnedMesh::SJoint *parent = bone.parent_index >= 0
				? joints[bone.parent_index] : nullptr;
		auto *joint = builder.addJoint(parent);
		joints[i] = joint;
		bone.joint_id = joint->JointID;

		const v3f pivot = toEngine(bone.pivot);
		const v3f parent_pivot = bone.parent_index >= 0
				? toEngine(bones[bone.parent_index].pivot) : v3f(0, 0, 0);

		core::Transform transform;
		transform.translation = pivot - parent_pivot;
		transform.rotation = toEngineRotation(bone.rotation);
		joint->transform = transform;
		joint->Name = bone.name;

		core::matrix4 bind;
		bind.setTranslation(pivot);
		joint->GlobalInversedMatrix = bind;
		joint->GlobalInversedMatrix->makeInverse();
	}

	/*
	 * Геометрия. Вся модель кладётся в один буфер: текстура у неё одна, и
	 * дробить её по костям значило бы платить отдельным вызовом отрисовки за
	 * каждую — а костей у ствола под тридцать. Принадлежность кости несёт не
	 * буфер, а вес вершины, единственный и полный.
	 */
	scene::SSkinMeshBuffer *buffer = builder.addMeshBuffer();
	buffer->VertexType = video::EVT_STANDARD;
	auto &vertices = buffer->Vertices_Standard->Data;
	auto &indices = buffer->Indices->Data;

	size_t cube_count = 0;
	for (const Bone &bone : bones)
		cube_count += bone.cubes.size();
	vertices.reserve(cube_count * 24);
	indices.reserve(cube_count * 36);

	for (size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
		const Bone &bone = bones[bone_index];
		for (const Cube &cube : bone.cubes) {
			const Corners corners(cube, cube.inflate);

			for (u8 f = 0; f < 6; ++f) {
				const Face face = static_cast<Face>(f);

				v2f uvs[4];
				if (cube.box_uv) {
					FaceUV box;
					boxUV(face, cube, box.uv, box.uv_size);
					faceUVs(box, tex_w, tex_h, cube.mirror, uvs);
				} else {
					const FaceUV &face_uv = cube.faces[f];
					// Грань без развёртки в Bedrock не рисуется вовсе: ею
					// пользуются, чтобы не платить за невидимые стороны.
					if (!face_uv.present)
						continue;
					faceUVs(face_uv, tex_w, tex_h, cube.mirror, uvs);
				}

				// Зеркальный куб меняет местами боковые грани: развёртка у
				// него общая с незеркальным, а стороны противоположны.
				Face geom_face = face;
				if (cube.mirror) {
					if (face == Face::WEST)
						geom_face = Face::EAST;
					else if (face == Face::EAST)
						geom_face = Face::WEST;
				}

				v3f corner_pos[4];
				faceCorners(geom_face, corners, corner_pos);

				v3f normal = faceNormal(geom_face);
				if (cube.has_rotation) {
					// Нормаль поворачивается вместе с кубом — иначе свет
					// ложится так, будто куб не поворачивали. Точка вращения
					// направлению не нужна, только сам поворот.
					normal = bedrockRotation(cube.rotation) * normal;
				}

				const u16 base = static_cast<u16>(vertices.size());
				for (int i = 0; i < 4; ++i) {
					const v3f pos = rotateAroundPivot(
							toEngine(corner_pos[i]), cube);
					vertices.emplace_back(pos, normal,
							video::SColor(0xFFFFFFFF), uvs[i]);
					builder.addWeight(joints[bone_index], 0,
							base + i, 1.0f);
				}

				// Обход вершин: тот, при котором лицевая сторона смотрит
				// наружу в левой тройке осей движка.
				indices.push_back(base + 0);
				indices.push_back(base + 2);
				indices.push_back(base + 1);
				indices.push_back(base + 0);
				indices.push_back(base + 3);
				indices.push_back(base + 2);
			}
		}
	}

	if (vertices.empty()) {
		errorstream << "Bedrock: " << name << ": в модели нет ни одной грани"
				<< std::endl;
		return nullptr;
	}

	scene::SkinnedMesh *mesh = std::move(builder).finalize();
	infostream << "Bedrock: " << name << ": " << bones.size() << " костей, "
			<< cube_count << " кубов, " << vertices.size() << " вершин"
			<< std::endl;
	return mesh;
}

} // namespace bedrock
