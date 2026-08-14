// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "animation.h"
#include "convert.h"
#include "easing.h"
#include "json.h"
#include "molang.h"
#include "log.h"

#include <algorithm>
#include <map>
#include <vector>

namespace bedrock
{

namespace
{

/*
 * Сколько раз в секунду опрашивается кривая, которую нельзя записать двумя
 * ключами. Прямых участков это не касается — они и остаются двумя ключами;
 * дробится только то, что гнётся: сглаженные переходы и выражения.
 *
 * Тридцать раз в секунду — вдвое чаще, чем анимации рисуют в Blockbench, и
 * заметно чаще, чем сервер шлёт свои шаги. Один ствол со всеми своими
 * дорожками укладывается в несколько десятков килобайт ключей, и это цена
 * один раз при загрузке, а не каждый кадр.
 */
constexpr f32 SAMPLE_RATE = 30.0f;

/// Один ключ канала, как он записан в файле.
struct Key
{
	f32 time = 0.0f;
	Molang value[3];
	Easing easing;
	bool constant = true; ///< не зависит ни от времени, ни от кривой

	v3f eval(f32 anim_time) const
	{
		return v3f(value[0].eval(anim_time), value[1].eval(anim_time),
				value[2].eval(anim_time));
	}
};

enum class Channel : u8 { POSITION, ROTATION, SCALE };

/// Разобрать тройку значений: числа или выражения Molang.
void readTriple(const Json::Value &vector, Molang out[3], bool &constant)
{
	for (u32 i = 0; i < 3; ++i) {
		const Json::Value &item = vector.size() > i ? vector[i] : Json::Value();
		if (item.isString()) {
			out[i] = Molang::parse(item.asString());
			if (!out[i].isConstant())
				constant = false;
		} else if (item.isNumeric()) {
			out[i] = Molang::constant(item.asFloat());
		} else {
			out[i] = Molang::constant(0.0f);
		}
	}
}

/*
 * Канал кости. Blockbench пишет его тремя способами, и все три встречаются в
 * настоящих модах:
 *
 *   "rotation": [0, 90, 0]                   — одно значение на всю дорожку
 *   "rotation": {"vector": [...]}            — то же, но многословнее
 *   "rotation": {"0.0": {...}, "0.5": {...}} — ключи по времени
 */
std::vector<Key> readChannel(const Json::Value &channel)
{
	std::vector<Key> keys;

	auto readStatic = [&](const Json::Value &vector) {
		Key key;
		key.time = 0.0f;
		readTriple(vector, key.value, key.constant);
		keys.push_back(key);
	};

	if (channel.isArray()) {
		readStatic(channel);
		return keys;
	}
	if (!channel.isObject())
		return keys;
	if (channel.isMember("vector")) {
		readStatic(channel["vector"]);
		return keys;
	}

	for (const std::string &time_str : channel.getMemberNames()) {
		const Json::Value &entry = channel[time_str];
		Key key;
		key.time = static_cast<f32>(atof(time_str.c_str()));

		if (entry.isArray()) {
			readTriple(entry, key.value, key.constant);
		} else if (entry.isObject()) {
			// Blockbench умеет разводить значение до и после ключа. Разрыв
			// движку не выразить одним ключом, поэтому берём значение после:
			// именно оно определяет, куда кость поедет дальше.
			const Json::Value &vector = entry.isMember("post") ? entry["post"]
					: (entry.isMember("vector") ? entry["vector"] : entry["pre"]);
			readTriple(vector.isArray() ? vector : vector["vector"],
					key.value, key.constant);
			if (entry.isMember("easing")) {
				key.easing = Easing::parse(entry["easing"].asString());
				if (!key.easing.isLinear())
					key.constant = false;
				if (entry.isMember("easingArgs")
						&& entry["easingArgs"].isArray()
						&& !entry["easingArgs"].empty()) {
					key.easing.arg = entry["easingArgs"][0].asFloat();
					key.easing.has_arg = true;
				}
			}
		}
		keys.push_back(key);
	}

	std::sort(keys.begin(), keys.end(),
			[](const Key &a, const Key &b) { return a.time < b.time; });
	return keys;
}

/// Значение канала в произвольный момент — ровно так, как его считает Bedrock:
/// кривая ключа описывает подход К НЕМУ, а не уход от него.
v3f sampleChannel(const std::vector<Key> &keys, f32 time)
{
	if (keys.empty())
		return v3f(0, 0, 0);
	if (keys.size() == 1 || time <= keys.front().time)
		return keys.front().eval(time);
	if (time >= keys.back().time)
		return keys.back().eval(time);

	size_t next = 1;
	while (next < keys.size() && keys[next].time < time)
		++next;

	const Key &from = keys[next - 1];
	const Key &to = keys[next];
	const f32 span = to.time - from.time;
	const f32 t = span > 0.0f ? (time - from.time) / span : 1.0f;
	const f32 eased = to.easing.apply(t);

	const v3f a = from.eval(time);
	const v3f b = to.eval(time);
	return a + (b - a) * eased;
}

/// Моменты, в которые канал придётся опросить. Прямые участки описываются
/// концами, гнутые — дробятся.
///
/// Повороты дробятся всегда, и это не перестраховка. Bedrock ведёт кость по
/// углам, покомпонентно, а дорожка движка хранит повороты целиком и идёт между
/// ними кратчайшей дугой. Пути расходятся всюду, где угол переваливает за
/// половину оборота: рука, которой положено развернуть оружие через −180°,
/// поворачивает его в другую сторону. Частые ключи снимают расхождение —
/// каждый отрезок становится настолько коротким, что обе дороги совпадают.
void sampleTimes(const std::vector<Key> &keys, std::vector<f32> &out,
		bool rotation)
{
	for (size_t i = 0; i < keys.size(); ++i) {
		out.push_back(keys[i].time);
		if (i + 1 >= keys.size())
			continue;

		const Key &to = keys[i + 1];
		const bool straight = !rotation && to.easing.isLinear() && to.constant
				&& keys[i].constant;
		if (straight)
			continue;

		const f32 span = to.time - keys[i].time;
		const int steps = std::max(1, static_cast<int>(span * SAMPLE_RATE));
		for (int s = 1; s < steps; ++s)
			out.push_back(keys[i].time + span * (static_cast<f32>(s) / steps));
	}
}

} // namespace

u32 loadAnimations(scene::SkinnedMesh *mesh, const std::string &json,
		const std::string &name)
{
	if (!mesh)
		return 0;

	Json::Value root;
	if (!parseJson(json, root, name))
		return 0;

	const Json::Value &animations = root["animations"];
	if (!animations.isObject()) {
		errorstream << "Bedrock: " << name << ": нет раздела animations."
				" Файл должен быть выгружен из Blockbench как"
				" Bedrock Animation." << std::endl;
		return 0;
	}

	/*
	 * Чем анимация является для кости, а чем не является.
	 *
	 * Поворот в Bedrock прибавляется к тому, что задано в модели: кость,
	 * повёрнутая в Blockbench на месте, остаётся повёрнутой и во время
	 * анимации. Складывать нужно именно углы, до перевода в поворот, —
	 * сложить два готовых поворота не то же самое.
	 *
	 * Смещение же в Bedrock отсчитывается от места самой кости, а дорожка
	 * движка задаёт положение сустава целиком, от родителя. Поэтому к
	 * смещению из файла прибавляется собственное смещение кости: без этого
	 * любая кость, которой анимация двигает, улетала бы к началу координат.
	 */
	std::map<std::string, v3f> base_rotation;
	std::map<std::string, v3f> base_position;
	std::map<std::string, u16> joint_by_name;
	for (const auto *joint : mesh->getAllJoints()) {
		if (!joint->Name)
			continue;
		joint_by_name[*joint->Name] = joint->JointID;
		v3f degrees(0, 0, 0);
		v3f offset(0, 0, 0);
		if (const auto *transform = std::get_if<core::Transform>(&joint->transform)) {
			degrees = fromEngineRotation(transform->rotation);
			offset = transform->translation;
		}
		base_rotation[*joint->Name] = degrees;
		base_position[*joint->Name] = offset;
	}

	u32 added = 0;
	for (const std::string &anim_name : animations.getMemberNames()) {
		const Json::Value &anim_json = animations[anim_name];
		if (!anim_json.isObject())
			continue;

		scene::SkinnedMesh::Animation animation;
		animation.name = anim_name;

		f32 length = anim_json.get("animation_length", 0.0f).asFloat();

		const Json::Value &bones = anim_json["bones"];
		for (const std::string &bone_name : bones.getMemberNames()) {
			auto joint_it = joint_by_name.find(bone_name);
			if (joint_it == joint_by_name.end()) {
				// Анимации нередко описывают кости, которых в этой модели
				// нет: руки игрока, прицелы, съёмные части. Это не ошибка.
				continue;
			}

			const Json::Value &bone_json = bones[bone_name];
			const std::vector<Key> position = readChannel(bone_json["position"]);
			const std::vector<Key> rotation = readChannel(bone_json["rotation"]);
			const std::vector<Key> scale = readChannel(bone_json["scale"]);
			if (position.empty() && rotation.empty() && scale.empty())
				continue;

			scene::SkinnedMesh::Animation::JointKeys joint_keys;
			joint_keys.joint_id = joint_it->second;

			const v3f base = base_rotation[bone_name];
			const v3f origin = base_position[bone_name];

			auto bake = [&](const std::vector<Key> &keys, Channel channel) {
				if (keys.empty())
					return;
				std::vector<f32> times;
				sampleTimes(keys, times, channel == Channel::ROTATION);
				// Дорожка, у которой значение одно на всю длину, всё равно
				// должна дожить до её конца: иначе кость вернётся в исходную
				// позу раньше, чем анимация кончится.
				if (length > 0.0f && (times.empty() || times.back() < length))
					times.push_back(length);
				std::sort(times.begin(), times.end());
				times.erase(std::unique(times.begin(), times.end()), times.end());

				for (const f32 time : times) {
					const v3f value = sampleChannel(keys, time);
					switch (channel) {
					case Channel::POSITION:
						joint_keys.keys.position.pushBack(time,
								origin + toEngine(value));
						break;
					case Channel::ROTATION:
						joint_keys.keys.rotation.pushBack(time,
								toEngineRotation(base + value));
						break;
					case Channel::SCALE:
						// Масштаб осей не переставляет, знаки ему не нужны.
						joint_keys.keys.scale.pushBack(time, value);
						break;
					}
				}
			};

			bake(position, Channel::POSITION);
			bake(rotation, Channel::ROTATION);
			bake(scale, Channel::SCALE);

			joint_keys.keys.cleanup();
			length = std::max(length, joint_keys.keys.getEndFrame());
			animation.joint_keys.push_back(std::move(joint_keys));
		}

		if (animation.joint_keys.empty()) {
			warningstream << "Bedrock: " << name << ": анимация " << anim_name
					<< " не задевает ни одной кости этой модели" << std::endl;
			continue;
		}

		// Дорожка без длины — не пустая: Blockbench так пишет позы, у которых
		// нет движения, одними значениями без ключей. Дорожке нулевой длины
		// движку нечего проигрывать, поэтому даём ей мгновение: поза встаёт
		// и держится, а это ровно то, для чего её и писали.
		if (length <= 0.0f)
			length = 0.05f;
		animation.end_frame = length;
		mesh->addAnimation(std::move(animation));
		++added;
	}

	infostream << "Bedrock: " << name << ": добавлено дорожек: " << added
			<< std::endl;
	return added;
}

} // namespace bedrock
