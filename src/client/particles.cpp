// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "particles.h"
#include <cmath>
#include <array>
#include <algorithm>
#include "ICameraSceneNode.h"
#include "client.h"
#include "collision.h"
#include "client/content_cao.h"
#include "client/clientevent.h"
#include "client/renderingengine.h"
#include "client/texturesource.h"
#include "util/numeric.h"
#include "light.h"
#include "localplayer.h"
#include "clientmap.h"
#include "mapnode.h"
#include "node_visuals.h"
#include "nodedef.h"
#include "client.h"
#include "settings.h"
#include "profiler.h"

#include "CMeshBuffer.h"

using BlendMode = ParticleParamTypes::BlendMode;

ClientParticleTexture::ClientParticleTexture(const ServerParticleTexture& p, ITextureSource *tsrc)
{
	tex = p;
	// note: getTextureForMesh not needed here because we don't use texture filtering
	ref = tsrc->getTexture(p.string);

	// Try to show another texture to indicate a code issue.
	if (!ref)
		ref = tsrc->getTexture("no_texture.png");
}

static video::ITexture *extractTexture(const TileDef &def, const TileLayer &layer,
	ITextureSource *tsrc)
{
	// If animated take first frame from tile layer (so we don't have to handle
	// that manually), otherwise look up by name.
	if (!layer.empty() && (layer.material_flags & MATERIAL_FLAG_ANIMATION)) {
		auto *ret = (*layer.frames)[0].texture;
		assert(ret->getType() == video::ETT_2D);
		return ret;
	}
	if (!def.name.empty())
		return tsrc->getTexture(def.name);
	return nullptr;
}

/*
	Particle
*/

Particle::Particle(
		const ParticleParameters &p,
		const ClientParticleTexRef &texture,
		v2f texpos,
		v2f texsize,
		video::SColor color,
		ParticleSpawner *parent,
		std::unique_ptr<ClientParticleTexture> owned_texture
	) :
		m_expiration(p.expirationtime),
		m_rotation(p.rotation),
		m_rotation_speed(p.rotation_speed),

		m_base_color(color),

		m_texture(texture),
		m_texpos(texpos),
		m_texsize(texsize),
		m_pos(p.pos),
		m_velocity(p.vel),
		m_acceleration(p.acc),
		m_p(p),

		m_parent(parent),
		m_owned_texture(std::move(owned_texture))
{
}

Particle::~Particle()
{
	if (m_buffer)
		m_buffer->release(m_index);
}

bool Particle::attachToBuffer(ParticleBuffer *buffer)
{
	auto index_opt = buffer->allocate();
	if (index_opt.has_value()) {
		m_index = index_opt.value();
		m_buffer = buffer;
		return true;
	}
	return false;
}

void Particle::step(float dtime, ClientEnvironment *env)
{
	m_time += dtime;

	/*
	 * Улёгшийся мусор не считает ничего.
	 *
	 * Ни движения, ни столкновений, ни освещения, ни — главное — вершин:
	 * буфер их уже содержит и трогать его незачем. Тысяча лежащих обломков
	 * обходится в один обход списка и ничего больше.
	 *
	 * Единственное, что может их поднять, — смена смещения камеры: вершины
	 * хранятся относительно него, и после сдвига мира их нужно переписать.
	 */
	if (m_settled) {
		if (env->getCameraOffset() != m_settled_camera_offset) {
			m_settled_camera_offset = env->getCameraOffset();
			updateVertices(env, m_settled_color);
		}
		return;
	}

	if (m_rotation_speed != v3f())
		m_rotation += m_rotation_speed * dtime;

	// apply drag (not handled by collisionMoveSimple) and brownian motion
	v3f av = vecAbsolute(m_velocity);
	av -= av * (m_p.drag * dtime);
	m_velocity = av*vecSign(m_velocity) + v3f(m_p.jitter.pickWithin())*dtime;

	/*
	 * Частица, которая гибнет от первого касания, не нуждается в разборе
	 * столкновений — ей нужен ответ «да или нет».
	 *
	 * Разница не в мелочи. Полный разбор берёт коробку размером с частицу, а
	 * размер у частицы — это размер спрайта: у дождя он доходит до трёх узлов,
	 * и капля обходит десятки узлов за кадр. Тысяча капель — и кадр вырастает
	 * вдвое, причём на процессоре, где его никто не ищет. Капле же коробка не
	 * нужна вовсе: физически это точка, а спрайт растянут только для вида.
	 *
	 * Поэтому здесь путь короче: летим свободно, а отрезок пути проверяем
	 * точками, шагом не крупнее половины узла — быстрая капля не перепрыгнет
	 * преграду, а медленной хватит одной проверки. Гаснет частица там же, где
	 * гасла раньше, так что на глаз не меняется ничего.
	 */
	const bool vanishes_on_touch = m_p.collisiondetection && m_p.collision_removal
			&& !m_p.object_collision && m_p.bounce.max <= 0.0f;

	if (vanishes_on_touch) {
		const v3f start = m_pos;
		m_pos += (m_velocity + m_acceleration * 0.5f * dtime) * dtime;
		m_velocity += m_acceleration * dtime;

		const v3f delta = m_pos - start;
		const f32 length = delta.getLength();
		// Шестнадцать шагов — это восемь узлов пути за кадр. Дальше частица
		// летит быстрее, чем имеет смысл проверять: на таких скоростях её и
		// не видно.
		const int steps = std::min(16, std::max(1, (int)std::ceil(length / 0.5f)));
		const NodeDefManager *ndef = env->getPlaceDef()->ndef();

		for (int i = 1; i <= steps; i++) {
			const v3f probe = start + delta * ((f32)i / (f32)steps);
			const v3s16 np(std::floor(probe.X + 0.5f), std::floor(probe.Y + 0.5f),
					std::floor(probe.Z + 0.5f));

			bool pos_ok = false;
			const MapNode n = env->getClientMap().getNode(np, &pos_ok);
			// Незагруженный кусок карты преградой не считается: иначе дождь
			// обрывался бы по краю прогрузки.
			if (!pos_ok || n.getContent() == CONTENT_IGNORE)
				continue;

			if (ndef->get(n).walkable) {
				m_pos = probe;
				m_expiration = -1.0f;   // погасить на месте касания
				break;
			}
		}
	} else if (m_p.collisiondetection) {
		aabb3f box(v3f(-m_p.size / 2.0f), v3f(m_p.size / 2.0f));
		v3f p_pos = m_pos * BS;
		v3f p_velocity = m_velocity * BS;
		CollisionMoveResult r = collisionMoveSimple(env, env->getPlaceDef(),
			box, 0.0f, dtime, &p_pos, &p_velocity, m_acceleration * BS, nullptr,
			m_p.object_collision, StepUpMode::LEGACY);

		f32 bounciness = m_p.bounce.pickWithin();
		if (r.collides && (m_p.collision_removal || bounciness > 0)) {
			if (m_p.collision_removal) {
				// force expiration of the particle
				m_expiration = -1.0f;
			} else if (bounciness > 0) {
				/* cheap way to get a decent bounce effect is to only invert the
				 * largest component of the velocity vector, so e.g. you don't
				 * have a rock immediately bounce back in your face when you try
				 * to skip it across the water (as would happen if we simply
				 * downscaled and negated the velocity vector). this means
				 * bounciness will work properly for cubic objects, but meshes
				 * with diagonal angles and entities will not yield the correct
				 * visual. this is probably unavoidable */
				if (av.Y > av.X && av.Y > av.Z) {
					m_velocity.Y = -(m_velocity.Y * bounciness);
				} else if (av.X > av.Y && av.X > av.Z) {
					m_velocity.X = -(m_velocity.X * bounciness);
				} else if (av.Z > av.Y && av.Z > av.X) {
					m_velocity.Z = -(m_velocity.Z * bounciness);
				} else { // well now we're in a bit of a pickle
					m_velocity = -(m_velocity * bounciness);
				}
			}
		} else {
			m_velocity = p_velocity / BS;
		}
		m_pos = p_pos / BS;

		/*
		 * Мусор, которому велено улечься, на этом успокаивается.
		 *
		 * Условие — коснулся земли и почти остановился. Пока он ещё катится
		 * или сползает по склону, спать рано: улёгшийся уже не двигается
		 * никогда, и уложить его в воздухе значило бы подвесить обломок.
		 */
		if (m_p.settle_on_collision && r.collides && r.touching_ground
				&& m_velocity.getLengthSQ() < 0.35f) {
			m_settled = true;
			m_velocity = v3f();
			m_acceleration = v3f();
			m_rotation_speed = v3f();
			m_settled_camera_offset = env->getCameraOffset();
		}
	} else {
		// apply velocity and acceleration to position
		m_pos += (m_velocity + m_acceleration * 0.5f * dtime) * dtime;
		// apply acceleration to velocity
		m_velocity += m_acceleration * dtime;
	}

	if (m_p.animation.type != TAT_NONE) {
		m_animation_time += dtime;
		int frame_length_i = 0;
		m_p.animation.determineParams(
				m_texture.ref->getSize(),
				NULL, &frame_length_i, NULL);
		float frame_length = frame_length_i / 1000.0;
		while (m_animation_time > frame_length) {
			m_animation_frame++;
			m_animation_time -= frame_length;
		}
	}

	// animate particle alpha in accordance with settings
	float alpha = 1.f;
	if (m_texture.tex != nullptr)
		alpha = m_texture.tex -> alpha.blend(m_time / (m_expiration+0.1f));

	// Update lighting
	auto col = updateLight(env);
	col.setAlpha(255 * alpha);

	// Update model
	updateVertices(env, col);

	// Улёгшийся запоминает свой цвет: пересчитывать освещение ему больше не
	// придётся, а при сдвиге мира вершины переписываются именно им.
	if (m_settled)
		m_settled_color = col;
}

video::SColor Particle::updateLight(ClientEnvironment *env)
{
	u8 light = 0;
	bool pos_ok;

	v3s16 p = v3s16(
		floor(m_pos.X+0.5),
		floor(m_pos.Y+0.5),
		floor(m_pos.Z+0.5)
	);

	const u32 ratio = env->getDayNightRatio();
	if (m_light_valid && p == m_light_node && ratio == m_light_ratio)
		return m_light_color;

	MapNode n = env->getClientMap().getNode(p, &pos_ok);
	if (pos_ok)
		light = n.getLightBlend(env->getDayNightRatio(),
				env->getPlaceDef()->ndef()->getLightingFlags(n));
	else
		light = blend_light(env->getDayNightRatio(), LIGHT_SUN, 0);

	u8 m_light = decode_light(light + m_p.glow);
	m_light_color = video::SColor(255,
		m_light * m_base_color.getRed() / 255,
		m_light * m_base_color.getGreen() / 255,
		m_light * m_base_color.getBlue() / 255);
	m_light_node = p;
	m_light_ratio = ratio;
	m_light_valid = true;
	return m_light_color;
}

void Particle::updateVertices(ClientEnvironment *env, video::SColor color)
{
	f32 tx0, tx1, ty0, ty1;
	v2f scale;

	if (!m_buffer)
		return;

	video::S3DVertex *vertices = m_buffer->getVertices(m_index);

	if (m_texture.tex != nullptr)
		scale = m_texture.tex -> scale.blend(m_time / (m_expiration+0.1f));
	else
		scale = v2f(1.f, 1.f);

	if (m_p.animation.type != TAT_NONE) {
		const v2u32 texsize = m_texture.ref->getSize();
		v2f texcoord, framesize_f;
		v2u32 framesize;
		texcoord = m_p.animation.getTextureCoords(texsize, m_animation_frame);
		m_p.animation.determineParams(texsize, NULL, NULL, &framesize);
		framesize_f = v2f::from(framesize) / v2f::from(texsize);

		tx0 = m_texpos.X + texcoord.X;
		tx1 = m_texpos.X + texcoord.X + framesize_f.X * m_texsize.X;
		ty0 = m_texpos.Y + texcoord.Y;
		ty1 = m_texpos.Y + texcoord.Y + framesize_f.Y * m_texsize.Y;
	} else {
		tx0 = m_texpos.X;
		tx1 = m_texpos.X + m_texsize.X;
		ty0 = m_texpos.Y;
		ty1 = m_texpos.Y + m_texsize.Y;
	}

	auto half = m_p.size * .5f,
	     hx   = half * scale.X,
	     hy   = half * scale.Y;

	auto *player = env->getLocalPlayer();
	const v3s16 camera_offset = env->getCameraOffset();
	const v3f origin = m_pos * BS - intToFloat(camera_offset, BS);

	if (m_p.shape == ParticleShape::CUBE) {
		/*
		 * Кубик: шесть граней по четыре вершины.
		 *
		 * Развёртка у всех граней одна и та же — кусок камня со всех сторон
		 * камень, и разводить их по текстуре незачем. Нормали настоящие: по
		 * ним обломок ловит свет и перестаёт выглядеть плоской наклейкой.
		 */
		const f32 h = half * scale.X;
		static const v3f face_normal[6] = {
			v3f(0, 0, -1), v3f(0, 0, 1), v3f(-1, 0, 0),
			v3f(1, 0, 0), v3f(0, 1, 0), v3f(0, -1, 0),
		};
		// Углы граней в порядке, дающем внешнюю сторону по часовой стрелке.
		static const v3f face_corner[6][4] = {
			{ v3f(-1, -1, -1), v3f( 1, -1, -1), v3f( 1,  1, -1), v3f(-1,  1, -1) },
			{ v3f( 1, -1,  1), v3f(-1, -1,  1), v3f(-1,  1,  1), v3f( 1,  1,  1) },
			{ v3f(-1, -1,  1), v3f(-1, -1, -1), v3f(-1,  1, -1), v3f(-1,  1,  1) },
			{ v3f( 1, -1, -1), v3f( 1, -1,  1), v3f( 1,  1,  1), v3f( 1,  1, -1) },
			{ v3f(-1,  1, -1), v3f( 1,  1, -1), v3f( 1,  1,  1), v3f(-1,  1,  1) },
			{ v3f(-1, -1,  1), v3f( 1, -1,  1), v3f( 1, -1, -1), v3f(-1, -1, -1) },
		};
		const f32 u[4] = { tx0, tx1, tx1, tx0 };
		const f32 v[4] = { ty1, ty1, ty0, ty0 };

		for (u16 f = 0; f < 6; f++) {
			for (u16 i = 0; i < 4; i++) {
				v3f pos = face_corner[f][i] * h;
				v3f normal = face_normal[f];
				pos.rotateYZBy(m_rotation.X / core::DEGTORAD);
				pos.rotateXZBy(m_rotation.Y / core::DEGTORAD);
				pos.rotateXYBy(m_rotation.Z / core::DEGTORAD);
				normal.rotateYZBy(m_rotation.X / core::DEGTORAD);
				normal.rotateXZBy(m_rotation.Y / core::DEGTORAD);
				normal.rotateXYBy(m_rotation.Z / core::DEGTORAD);
				vertices[f * 4 + i] = video::S3DVertex(pos.X, pos.Y, pos.Z,
						normal.X, normal.Y, normal.Z, color, u[i], v[i]);
			}
		}
		for (u16 i = 0; i < 24; i++)
			vertices[i].Pos += origin;
		return;
	}

	vertices[0] = video::S3DVertex(-hx, -hy,
		0, 0, 0, 0, color, tx0, ty1);
	vertices[1] = video::S3DVertex(hx, -hy,
		0, 0, 0, 0, color, tx1, ty1);
	vertices[2] = video::S3DVertex(hx, hy,
		0, 0, 0, 0, color, tx1, ty0);
	vertices[3] = video::S3DVertex(-hx, hy,
		0, 0, 0, 0, color, tx0, ty0);

	// Update position -- see #10398
	for (u16 i = 0; i < 4; i++) {
		video::S3DVertex &vertex = vertices[i];
		if (m_p.shape == ParticleShape::FLAT) {
			/*
			 * Плоская накладка держится своего поворота, а не камеры.
			 *
			 * Лоскут нарисован в плоскости XY, а лежать ему обычно на земле,
			 * поэтому сперва он кладётся плашмя, и только потом на него
			 * ложится заданный поворот. Так «поворот ноль» означает «лежит
			 * горизонтально», что для следа на земле и есть естественное
			 * положение.
			 */
			vertex.Pos.rotateYZBy(-90.0f);
			vertex.Pos.rotateYZBy(m_rotation.X / core::DEGTORAD);
			vertex.Pos.rotateXZBy(m_rotation.Y / core::DEGTORAD);
			vertex.Pos.rotateXYBy(m_rotation.Z / core::DEGTORAD);
		} else if (m_p.vertical) {
			v3f ppos = player->getPosition() / BS;
			vertex.Pos.rotateXZBy(std::atan2(ppos.Z - m_pos.Z, ppos.X - m_pos.X) /
				core::DEGTORAD + 90);
		} else {
			vertex.Pos.rotateYZBy(player->getPitch());
			vertex.Pos.rotateXZBy(player->getYaw());
		}
		vertex.Pos += origin;
	}
}

/*
	ParticleSpawner
*/

ParticleSpawner::ParticleSpawner(
		LocalPlayer *player,
		const ParticleSpawnerParameters &params,
		u16 attached_id,
		std::vector<ClientParticleTexture> &&texpool,
		ParticleManager *p_manager
	) :
		m_active(0),
		m_particlemanager(p_manager),
		m_time(0.0f),
		m_player(player),
		p(params),
		m_texpool(std::move(texpool)),
		m_attached_id(attached_id)
{
	m_spawntimes.reserve(p.amount + 1);
	for (u16 i = 0; i <= p.amount; i++) {
		float spawntime = myrand_float() * p.time;
		m_spawntimes.push_back(spawntime);
	}

	size_t max_particles = 0; // maximum number of particles likely to be visible at any given time
	assert(p.time >= 0);
	if (p.time != 0) {
		auto maxGenerations = p.time / std::min(p.exptime.start.min, p.exptime.end.min);
		max_particles = p.amount / maxGenerations;
	} else {
		auto longestLife = std::max(p.exptime.start.max, p.exptime.end.max);
		max_particles = p.amount * longestLife;
	}

	p_manager->reserveParticleSpace(max_particles * 1.2);
}

namespace {
	GenericCAO *findObjectByID(ClientEnvironment *env, u16 id) {
		if (id == 0)
			return nullptr;
		return env->getGenericCAO(id);
	}
}

void ParticleSpawner::spawnParticle(ClientEnvironment *env, float radius,
	const core::matrix4 *attached_absolute_pos_rot_matrix)
{
	float fac = 0;
	if (p.time != 0) { // ensure safety from divide-by-zeroes
		fac = m_time / (p.time+0.1f);
	}

	auto r_pos    = p.pos.blend(fac);
	auto r_vel    = p.vel.blend(fac);
	auto r_acc    = p.acc.blend(fac);
	auto r_drag   = p.drag.blend(fac);
	auto r_radius = p.radius.blend(fac);
	auto r_jitter = p.jitter.blend(fac);
	auto r_bounce = p.bounce.blend(fac);
	v3f  attractor_origin    = p.attractor_origin.blend(fac);
	v3f  attractor_direction = p.attractor_direction.blend(fac);
	auto attractor_obj           = findObjectByID(env, p.attractor_attachment);
	auto attractor_direction_obj = findObjectByID(env, p.attractor_direction_attachment);

	auto r_exp     = p.exptime.blend(fac);
	auto r_size    = p.size.blend(fac);
	auto r_attract = p.attract.blend(fac);
	auto attract   = r_attract.pickWithin();

	v3f ppos = m_player->getPosition() / BS;
	v3f pos = r_pos.pickWithin();
	v3f sphere_radius = r_radius.pickWithin();

	// Need to apply this first or the following check
	// will be wrong for attached spawners
	if (attached_absolute_pos_rot_matrix) {
		pos *= BS;
		attached_absolute_pos_rot_matrix->transformVect(pos);
		pos /= BS;
		v3s16 camera_offset = m_particlemanager->m_env->getCameraOffset();
		pos.X += camera_offset.X;
		pos.Y += camera_offset.Y;
		pos.Z += camera_offset.Z;
	}

	if (pos.getDistanceFromSQ(ppos) > radius*radius)
		return;

	// Parameters for the single particle we're about to spawn
	ParticleParameters pp;
	pp.pos = pos;

	pp.vel = r_vel.pickWithin();
	pp.acc = r_acc.pickWithin();
	pp.drag = r_drag.pickWithin();
	pp.jitter = r_jitter;
	pp.bounce = r_bounce;

	if (attached_absolute_pos_rot_matrix) {
		// Apply attachment rotation
		pp.vel = attached_absolute_pos_rot_matrix->rotateAndScaleVect(pp.vel);
		pp.acc = attached_absolute_pos_rot_matrix->rotateAndScaleVect(pp.acc);
	}

	if (attractor_obj)
		attractor_origin += attractor_obj->getPosition() / BS;
	if (attractor_direction_obj) {
		auto *attractor_absolute_pos_rot_matrix = attractor_direction_obj->getAbsolutePosRotMatrix();
		if (attractor_absolute_pos_rot_matrix) {
			attractor_direction = attractor_absolute_pos_rot_matrix
					->rotateAndScaleVect(attractor_direction);
		}
	}

	pp.expirationtime = r_exp.pickWithin();

	// Поворот и вращение — со своим разбросом на каждую частицу: горсть
	// обломков, повёрнутых одинаково, читается как строй, а не как мусор.
	{
		auto r_rotation = p.rotation.blend(fac);
		auto r_rot_speed = p.rotation_speed.blend(fac);
		pp.rotation = r_rotation.pickWithin();
		pp.rotation_speed = r_rot_speed.pickWithin();
	}

	if (sphere_radius != v3f()) {
		f32 l = sphere_radius.getLength();
		v3f mag = sphere_radius;
		mag.normalize();

		v3f ofs = v3f(l,0,0);
		ofs.rotateXZBy(myrand_range(0.f,360.f));
		ofs.rotateYZBy(myrand_range(0.f,360.f));
		ofs.rotateXYBy(myrand_range(0.f,360.f));

		pp.pos += ofs * mag;
	}

	if (p.attractor_kind != ParticleParamTypes::AttractorKind::none && attract != 0) {
		v3f dir;
		f32 dist = 0; /* =0 necessary to silence warning */
		switch (p.attractor_kind) {
			case ParticleParamTypes::AttractorKind::none:
				break;

			case ParticleParamTypes::AttractorKind::point: {
				dist = pp.pos.getDistanceFrom(attractor_origin);
				dir = pp.pos - attractor_origin;
				dir.normalize();
				break;
			}

			case ParticleParamTypes::AttractorKind::line: {
				// <https://github.com/luanti-org/luanti/issues/11505#issuecomment-915612700>
				const auto& lorigin = attractor_origin;
				v3f ldir = attractor_direction;
				ldir.normalize();
				auto origin_to_point = pp.pos - lorigin;
				auto scalar_projection = origin_to_point.dotProduct(ldir);
				auto point_on_line = lorigin + (ldir * scalar_projection);

				dist = pp.pos.getDistanceFrom(point_on_line);
				dir = (point_on_line - pp.pos);
				dir.normalize();
				dir *= -1; // flip it around so strength=1 attracts, not repulses
				break;
			}

			case ParticleParamTypes::AttractorKind::plane: {
				// <https://github.com/luanti-org/luanti/issues/11505#issuecomment-915612700>
				const v3f& porigin = attractor_origin;
				v3f normal = attractor_direction;
				normal.normalize();
				v3f point_to_origin = porigin - pp.pos;
				f32 factor = normal.dotProduct(point_to_origin);
				if (numericAbsolute(factor) == 0.0f) {
					dir = normal;
				} else {
					factor = numericSign(factor);
					dir = normal * factor;
				}
				dist = numericAbsolute(normal.dotProduct(pp.pos - porigin));
				dir *= -1; // flip it around so strength=1 attracts, not repulses
				break;
			}
		}

		f32 speedTowards = numericAbsolute(attract) * dist;
		v3f avel = dir * speedTowards;
		if (attract > 0 && speedTowards > 0) {
			avel *= -1;
			if (p.attractor_kill) {
				// make sure the particle dies after crossing the attractor threshold
				f32 timeToCenter = dist / speedTowards;
				if (timeToCenter < pp.expirationtime)
					pp.expirationtime = timeToCenter;
			}
		}
		pp.vel += avel;
	}

	p.copyCommon(pp);

	ClientParticleTexRef texture;
	v2f texpos, texsize;
	video::SColor color(0xFFFFFFFF);

	if (p.node.getContent() != CONTENT_IGNORE) {
		if (!ParticleManager::getNodeParticleParams(env->getPlaceDef(), p.node,
				pp, &texture.ref, texpos, texsize, &color, p.node_tile))
			return;
	} else {
		if (m_texpool.size() == 0)
			return;
		texture = ClientParticleTexRef(m_texpool[myrand_range(0, m_texpool.size() - 1)]);
		texpos = v2f(0.0f, 0.0f);
		texsize = v2f(1.0f, 1.0f);
		if (texture.tex->animated)
			pp.animation = texture.tex->animation;
	}

	// Same guard as in `CE_SPAWN_PARTICLE`
	if (!texture.ref)
		return;

	// synchronize animation length with particle life if desired
	if (pp.animation.type != TAT_NONE) {
		// FIXME: this should be moved into a TileAnimationParams class method
		if (pp.animation.type == TAT_VERTICAL_FRAMES &&
			pp.animation.vertical_frames.length < 0) {
			auto& a = pp.animation.vertical_frames;
			// we add a tiny extra value to prevent the first frame
			// from flickering back on just before the particle dies
			a.length = (pp.expirationtime / -a.length) + 0.1;
		} else if (pp.animation.type == TAT_SHEET_2D &&
				   pp.animation.sheet_2d.frame_length < 0) {
			auto& a = pp.animation.sheet_2d;
			auto frames = a.frames_w * a.frames_h;
			auto runtime = (pp.expirationtime / -a.frame_length) + 0.1;
			pp.animation.sheet_2d.frame_length = frames / runtime;
		}
	}

	// Allow keeping default random size
	if (p.size.start.max > 0.0f || p.size.end.max > 0.0f)
		pp.size = r_size.pickWithin();

	++m_active;
	m_particlemanager->addParticle(std::make_unique<Particle>(
			pp,
			texture,
			texpos,
			texsize,
			color,
			this
		));
}

void ParticleSpawner::step(float dtime, ClientEnvironment *env)
{
	m_time += dtime;

	static thread_local const float radius =
			g_settings->getS16("max_block_send_distance") * MAP_BLOCKSIZE;

	bool unloaded = false;
	const core::matrix4 *attached_absolute_pos_rot_matrix = nullptr;
	if (m_attached_id) {
		if (GenericCAO *attached = env->getGenericCAO(m_attached_id)) {
			attached_absolute_pos_rot_matrix = attached->getAbsolutePosRotMatrix();
		} else {
			unloaded = true;
		}
	}

	if (p.time != 0) {
		// Spawner exists for a predefined timespan
		for (auto i = m_spawntimes.begin(); i != m_spawntimes.end(); ) {
			if ((*i) <= m_time && p.amount > 0) {
				--p.amount;

				// Pretend to, but don't actually spawn a particle if it is
				// attached to an unloaded object or distant from player.
				if (!unloaded)
					spawnParticle(env, radius, attached_absolute_pos_rot_matrix);

				i = m_spawntimes.erase(i);
			} else {
				++i;
			}
		}
	} else {
		// Spawner exists for an infinity timespan, spawn on a per-second base

		// Skip this step if attached to an unloaded object
		if (unloaded)
			return;

		for (int i = 0; i <= p.amount; i++) {
			if (myrand_float() < dtime)
				spawnParticle(env, radius, attached_absolute_pos_rot_matrix);
		}
	}
}

/*
	ParticleBuffer
*/

ParticleBuffer::ParticleBuffer(ClientEnvironment *env, const video::SMaterial &material,
		ParticleShape shape)
	: scene::ISceneNode(
			env->getPlaceDef()->getSceneManager()->getRootSceneNode(),
			env->getPlaceDef()->getSceneManager()),
	m_shape(shape),
	m_mesh_buffer(make_irr<scene::SMeshBuffer>())
{
	m_mesh_buffer->getMaterial() = material;
}

static constexpr u16 quad_indices[] = { 0, 1, 2, 2, 3, 0 };

/// Индексы кубика: шесть граней по четыре вершины, каждая двумя треугольниками.
static constexpr u16 cube_indices[] = {
	 0,  1,  2,  2,  3,  0,
	 4,  5,  6,  6,  7,  4,
	 8,  9, 10, 10, 11,  8,
	12, 13, 14, 14, 15, 12,
	16, 17, 18, 18, 19, 16,
	20, 21, 22, 22, 23, 20,
};

std::optional<u16> ParticleBuffer::allocate()
{
	u16 index;

	m_usage_timer = 0;

	const u16 verts = verticesPerParticle();
	const u16 inds = indicesPerParticle();
	const u16 *pattern = m_shape == ParticleShape::CUBE ? cube_indices : quad_indices;

	if (!m_free_list.empty()) {
		index = m_free_list.back();
		m_free_list.pop_back();
		auto *vertices = static_cast<video::S3DVertex*>(m_mesh_buffer->getVertices());
		u16 *indices = m_mesh_buffer->getIndices();
		// reset vertices, because it is only written in Particle::step()
		for (u16 i = 0; i < verts; i++)
			vertices[verts * index + i] = video::S3DVertex();
		for (u16 i = 0; i < inds; i++)
			indices[inds * index + i] = verts * index + pattern[i];
		m_live[index] = true;
		m_indices_dirty = true;
		return index;
	}

	if (m_count >= maxParticles())
		return std::nullopt;

	// append new vertices
	// note: Our buffer never gets smaller, but ParticleManager will delete
	//       us after a while.
	std::array<video::S3DVertex, 24> vertices {};
	m_mesh_buffer->append(&vertices.front(), verts, pattern, inds);
	index = m_count++;
	m_live.resize(m_count, false);
	m_live[index] = true;
	m_indices_dirty = true;
	return index;
}

void ParticleBuffer::release(u16 index)
{
	assert(index < m_count);
	// The index buffer is rebuilt from m_live every frame, so there is nothing
	// to erase here. Zeroing it would be wrong besides: after the depth sort a
	// slot's quad no longer lives at its own offset.
	m_live[index] = false;
	m_indices_dirty = true;
	m_free_list.push_back(index);
}

video::S3DVertex *ParticleBuffer::getVertices(u16 index)
{
	if (index >= m_count)
		return nullptr;
	m_bounding_box_dirty = true;
	return &(static_cast<video::S3DVertex *>(m_mesh_buffer->getVertices())
			[verticesPerParticle() * index]);
}

void ParticleBuffer::OnRegisterSceneNode()
{
	if (IsVisible) {
		const bool blended = m_mesh_buffer->getMaterial().MaterialType
				!= video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
		// Has to happen before registering: that is where the sort key is
		// taken and where culling reads the bounding box.
		if (blended)
			updateSortPosition();
		SceneManager->registerNodeForRendering(this,
				blended ? scene::ESNRP_TRANSPARENT_EFFECT : scene::ESNRP_SOLID);
	}
	scene::ISceneNode::OnRegisterSceneNode();
}

void ParticleBuffer::updateSortPosition()
{
	// The scene manager orders transparent nodes by the distance from the
	// camera to the node's own translation (see TransparentNodeEntry). A
	// particle buffer never had one, so every buffer reported the distance to
	// the world origin, they all compared equal, and their order came out
	// arbitrary. Since a buffer is created per material, that is exactly the
	// case of particles with one texture drawing over nearer particles with
	// another.
	//
	// Giving the node the centre of its particles makes that ordering work.
	// Vertices stay in world coordinates -- render() resets the world
	// transform -- so the position only ever serves as a sort key, and
	// getBoundingBox() hands back a box relative to it so that culling keeps
	// working.
	v3f sum;
	u32 live = 0;
	for (u16 i = 0; i < m_count; i++) {
		if (!m_live[i])
			continue;
		sum += (m_mesh_buffer->getPosition(4 * i)
				+ m_mesh_buffer->getPosition(4 * i + 2)) * 0.5f;
		live++;
	}

	const v3f centre = live > 0 ? sum / static_cast<f32>(live) : v3f();
	if (centre != getPosition()) {
		setPosition(centre);
		updateAbsolutePosition();
		m_bounding_box_dirty = true;
	}
}

const core::aabbox3df &ParticleBuffer::getBoundingBox() const
{
	if (!m_bounding_box_dirty)
		return m_mesh_buffer->BoundingBox;

	core::aabbox3df box{{0, 0, 0}};
	bool first = true;
	for (u16 i = 0; i < m_count; i++) {
		if (!m_live[i])
			continue;

		for (u16 j = 0; j < 4; j++) {
			const auto pos = m_mesh_buffer->getPosition(i * 4 + j);
			if (first)
				box.reset(pos);
			else
				box.addInternalPoint(pos);
			first = false;
		}
	}

	// The vertices are in world coordinates, but the scene manager culls with
	// the box put through the node's transformation. Hand it back relative to
	// the node so the two do not stack up.
	const v3f offset = getPosition();
	box.MinEdge -= offset;
	box.MaxEdge -= offset;

	m_mesh_buffer->BoundingBox = box;
	m_bounding_box_dirty = false;
	return m_mesh_buffer->BoundingBox;
}

void ParticleBuffer::updateIndices()
{
	// Blended particles do not write depth (see
	// ParticleManager::getMaterialForParticle), so nothing but the drawing
	// order decides which of two overlapping particles ends up on top. Left
	// unordered, a particle further away can be drawn later and paint over a
	// nearer one -- smoke shows through smoke, and a cloud looks inside out.
	//
	// Ordering them back to front makes the blend come out right. This is not
	// exact for particles that intersect each other, but for the sprites
	// particles actually are it is what a depth sort is for.
	//
	// Alpha-clipped particles do write depth and need none of this.
	const bool needs_sorting = m_mesh_buffer->getMaterial().MaterialType
			!= video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;

	// An alpha-clipped buffer only has to be rebuilt when a particle came or
	// went; rebuilding it every frame would re-upload the index buffer for
	// nothing.
	if (!needs_sorting && !m_indices_dirty)
		return;

	m_sort_scratch.clear();
	m_sort_scratch.reserve(m_count);

	const scene::ICameraSceneNode *camera = SceneManager->getActiveCamera();
	const bool sort = needs_sorting && camera != nullptr;
	const v3f eye = sort ? camera->getAbsolutePosition() : v3f();

	for (u16 i = 0; i < m_count; i++) {
		if (!m_live[i])
			continue;
		f32 key = 0.0f;
		if (sort) {
			// Centre of the quad from its two opposite corners. Cheaper than
			// averaging all four, and accurate enough to order by: a corner
			// alone would misplace the large sprites an explosion throws.
			const u16 verts = verticesPerParticle();
			const v3f centre = (m_mesh_buffer->getPosition(verts * i)
					+ m_mesh_buffer->getPosition(verts * i + 2)) * 0.5f;
			key = centre.getDistanceFromSQ(eye);
		}
		m_sort_scratch.emplace_back(key, i);
	}

	if (sort) {
		// Farthest first.
		std::sort(m_sort_scratch.begin(), m_sort_scratch.end(),
				[] (const auto &a, const auto &b) { return a.first > b.first; });
	}

	const u16 verts = verticesPerParticle();
	const u16 inds = indicesPerParticle();
	const u16 *pattern = m_shape == ParticleShape::CUBE ? cube_indices : quad_indices;

	u16 *indices = m_mesh_buffer->getIndices();
	u32 out = 0;
	for (const auto &entry : m_sort_scratch) {
		for (u16 j = 0; j < inds; j++)
			indices[out++] = verts * entry.second + pattern[j];
	}
	// Slots without a particle collapse to a degenerate triangle, which draws
	// nothing.
	const u32 total = (u32)inds * m_count;
	while (out < total)
		indices[out++] = 0;

	m_mesh_buffer->setDirty(scene::EBT_INDEX);
	m_indices_dirty = false;
}

void ParticleBuffer::render()
{
	video::IVideoDriver *driver = SceneManager->getVideoDriver();

	if (isEmpty())
		return;

	updateIndices();

	driver->setTransform(video::ETS_WORLD, core::matrix4());
	driver->setMaterial(m_mesh_buffer->getMaterial());
	driver->drawMeshBuffer(m_mesh_buffer.get());
}

/*
	ParticleManager
*/

ParticleManager::ParticleManager(ClientEnvironment *env) :
	m_env(env)
{}

ParticleManager::~ParticleManager()
{
	clearAll();
}

void ParticleManager::step(float dtime)
{
	stepParticles(dtime);
	stepSpawners(dtime);
	stepBuffers(dtime);
}

void ParticleManager::stepSpawners(float dtime)
{
	MutexAutoLock lock(m_spawner_list_lock);

	for (size_t i = 0; i < m_dying_particle_spawners.size();) {
		// the particlespawner owns the textures, so we need to make
		// sure there are no active particles before we free it
		if (!m_dying_particle_spawners[i]->hasActive()) {
			m_dying_particle_spawners[i] = std::move(m_dying_particle_spawners.back());
			m_dying_particle_spawners.pop_back();
		} else {
			++i;
		}
	}

	for (auto it = m_particle_spawners.begin(); it != m_particle_spawners.end();) {
		auto &ps = it->second;
		if (ps->getExpired()) {
			// same as above
			if (ps->hasActive())
				m_dying_particle_spawners.push_back(std::move(ps));
			it = m_particle_spawners.erase(it);
		} else {
			ps->step(dtime, m_env);
			++it;
		}
	}
}

void ParticleManager::stepParticles(float dtime)
{
	MutexAutoLock lock(m_particle_list_lock);

	for (size_t i = 0; i < m_particles.size();) {
		Particle &p = *m_particles[i];
		if (p.isExpired()) {
			ParticleSpawner *parent = p.getParent();
			if (parent) {
				assert(parent->hasActive());
				parent->decrActive();
			}
			// delete
			m_particles[i] = std::move(m_particles.back());
			m_particles.pop_back();
		} else {
			p.step(dtime, m_env);
			++i;
		}
	}
}

void ParticleManager::stepBuffers(float dtime)
{
	constexpr float INTERVAL = 0.5f;
	if (!m_buffer_gc.step(dtime, INTERVAL))
		return;

	MutexAutoLock lock(m_particle_list_lock);

	// remove buffers that have been unused for 5 seconds
	size_t alloc = 0;
	for (size_t i = 0; i < m_particle_buffers.size(); ) {
		auto &buf = m_particle_buffers[i];
		buf->m_usage_timer += INTERVAL;
		if (buf->isEmpty() && buf->m_usage_timer > 5.0f) {
			// delete and swap with last
			buf->remove();
			buf = std::move(m_particle_buffers.back());
			m_particle_buffers.pop_back();
		} else {
			i++;
			alloc += buf->m_count;
		}
	}

	g_profiler->avg("ParticleManager: particle buffer count [#]", m_particle_buffers.size());
	if (!m_particle_buffers.empty())
		g_profiler->avg("ParticleManager: buffer allocated size [#]", alloc);
}

void ParticleManager::clearAll()
{
	MutexAutoLock lock(m_spawner_list_lock);
	MutexAutoLock lock2(m_particle_list_lock);

	m_particle_spawners.clear();
	m_dying_particle_spawners.clear();

	m_particles.clear();

	// have to remove from scene first because it keeps a reference
	for (auto &it : m_particle_buffers)
		it->remove();
	m_particle_buffers.clear();
}

void ParticleManager::handleParticleEvent(ClientEvent *event, Client *client,
	LocalPlayer *player)
{
	switch (event->type) {
		case CE_DELETE_PARTICLESPAWNER: {
			deleteParticleSpawner(event->delete_particlespawner.id);
			// no allocated memory in delete event
			break;
		}
		case CE_ADD_PARTICLESPAWNER: {
			deleteParticleSpawner(event->add_particlespawner.id);

			const ParticleSpawnerParameters &p = *event->add_particlespawner.p;

			// There can be multiple textures, e.g. for time-based animations
			// Look up all required textures in `ITextureSource` to retrieve an `ITexture`.
			std::vector<ClientParticleTexture> texpool;
			if (!p.texpool.empty()) {
				size_t txpsz = p.texpool.size();
				texpool.reserve(txpsz);
				for (size_t i = 0; i < txpsz; ++i) {
					texpool.emplace_back(p.texpool[i], client->tsrc());
				}
			} else {
				// no texpool in use, use fallback texture
				texpool.emplace_back(p.texture, client->tsrc());
			}

			addParticleSpawner(event->add_particlespawner.id,
					std::make_unique<ParticleSpawner>(
						player,
						p,
						event->add_particlespawner.attached_id,
						std::move(texpool),
						this)
					);

			delete event->add_particlespawner.p;
			break;
		}
		case CE_SPAWN_PARTICLE: {
			ParticleParameters &p = *event->spawn_particle;

			ClientParticleTexRef texture;
			std::unique_ptr<ClientParticleTexture> texstore;
			v2f texpos, texsize;
			video::SColor color(0xFFFFFFFF);

			f32 oldsize = p.size;

			if (p.node.getContent() != CONTENT_IGNORE) {
				getNodeParticleParams(m_env->getPlaceDef(), p.node, p,
						&texture.ref, texpos, texsize, &color, p.node_tile);
			} else {
				/* with no particlespawner to own the texture, we need
				 * to save it on the heap. it will be freed when the
				 * particle is destroyed */
				texstore = std::make_unique<ClientParticleTexture>(p.texture, client->tsrc());

				texture = ClientParticleTexRef(*texstore);
				texpos = v2f(0.0f, 0.0f);
				texsize = v2f(1.0f, 1.0f);
			}

			// Allow keeping default random size
			if (oldsize > 0.0f)
				p.size = oldsize;

			if (texture.ref) {
				addParticle(std::make_unique<Particle>(
						p, texture, texpos, texsize, color, nullptr,
						std::move(texstore)));
			}

			delete event->spawn_particle;
			break;
		}
		default: break;
	}
}

bool ParticleManager::getNodeParticleParams(Client *client, const MapNode &n,
	ParticleParameters &p, video::ITexture **texture,
	v2f &texpos, v2f &texsize, video::SColor *color, u8 tilenum)
{
	const ContentFeatures &f = client->ndef()->get(n);

	// No particles for "airlike" nodes
	if (f.drawtype == NDT_AIRLIKE)
		return false;

	// Texture
	// Note: we ignore the overlay here, oh well
	u8 texid;
	if (tilenum > 0 && tilenum <= 6)
		texid = tilenum - 1;
	else
		texid = myrand_range(0,5);

	const TileLayer &tile = f.visuals->tiles[texid].layers[0];
	*texture = extractTexture(f.tiledef[texid], tile, client->tsrc());
	p.texture.blendmode = f.alpha == ALPHAMODE_BLEND
			? BlendMode::alpha : BlendMode::clip;
	p.animation.type = TAT_NONE;

	float size = (myrand_range(0,8)) / 64.0f;
	p.size = BS * size;
	if (tile.scale)
		size /= tile.scale;
	texsize = v2f(size * 2.0f, size * 2.0f);
	texpos.X = (myrand_range(0,64)) / 64.0f - texsize.X;
	texpos.Y = (myrand_range(0,64)) / 64.0f - texsize.Y;

	if (tile.has_color)
		*color = tile.color;
	else
		f.visuals->getColor(n.param2, color);

	return true;
}

// The final burst of particles when a node is finally dug, *not* particles
// spawned during the digging of a node.

void ParticleManager::addDiggingParticles(LocalPlayer *player, v3s16 pos, const MapNode &n)
{
	for (u16 j = 0; j < 16; j++) {
		addNodeParticle(player, pos, n);
	}
}

// During the digging of a node particles are spawned individually by this
// function, called from Game::handleDigging() in game.cpp.

void ParticleManager::addNodeParticle(LocalPlayer *player, v3s16 pos, const MapNode &n)
{
	ParticleParameters p;
	video::ITexture *ref = nullptr;
	v2f texpos, texsize;
	video::SColor color;

	if (!getNodeParticleParams(m_env->getPlaceDef(), n, p, &ref, texpos, texsize, &color))
		return;

	p.expirationtime = myrand_range(0, 100) / 100.0f;

	// Physics
	p.vel = v3f(
		myrand_range(-1.5f,1.5f),
		myrand_range(0.f,3.f),
		myrand_range(-1.5f,1.5f)
	);
	p.acc = v3f(
		0.0f,
		-player->movement_gravity * player->physics_override.gravity / BS,
		0.0f
	);
	p.pos = v3f(
		(f32)pos.X + myrand_range(0.f, .5f) - .25f,
		(f32)pos.Y + myrand_range(0.f, .5f) - .25f,
		(f32)pos.Z + myrand_range(0.f, .5f) - .25f
	);

	addParticle(std::make_unique<Particle>(
		p,
		ClientParticleTexRef(ref),
		texpos,
		texsize,
		color));
}

void ParticleManager::reserveParticleSpace(size_t max_estimate)
{
	MutexAutoLock lock(m_particle_list_lock);

	m_particles.reserve(m_particles.size() + max_estimate);
}

static void setBlendMode(video::SMaterial &material, BlendMode blendmode)
{
	video::E_BLEND_FACTOR bfsrc, bfdst;
	video::E_BLEND_OPERATION blendop;
	switch (blendmode) {
		case BlendMode::add:
			bfsrc = video::EBF_SRC_ALPHA;
			bfdst = video::EBF_DST_ALPHA;
			blendop = video::EBO_ADD;
		break;

		case BlendMode::sub:
			bfsrc = video::EBF_SRC_ALPHA;
			bfdst = video::EBF_DST_ALPHA;
			blendop = video::EBO_REVSUBTRACT;
		break;

		case BlendMode::screen:
			bfsrc = video::EBF_ONE;
			bfdst = video::EBF_ONE_MINUS_SRC_COLOR;
			blendop = video::EBO_ADD;
		break;

		default: // includes BlendMode::alpha
			bfsrc = video::EBF_SRC_ALPHA;
			bfdst = video::EBF_ONE_MINUS_SRC_ALPHA;
			blendop = video::EBO_ADD;
		break;
	}

	material.MaterialTypeParam = video::pack_textureBlendFunc(
			bfsrc, bfdst,
			video::EMFN_MODULATE_1X,
			video::EAS_TEXTURE | video::EAS_VERTEX_COLOR);
	material.BlendOperation = blendop;
}

video::SMaterial ParticleManager::getMaterialForParticle(const Particle *particle)
{
	const ClientParticleTexRef &texture = particle->getTextureRef();

	video::SMaterial material;

	// Texture
	material.BackfaceCulling = false;
	material.FogEnable = true;
	material.forEachTexture([] (auto &tex) {
		tex.MinFilter = video::ETMINF_NEAREST_MIPMAP_NEAREST;
		tex.MagFilter = video::ETMAGF_NEAREST;
	});

	const auto blendmode = particle->getBlendMode();
	if (blendmode == BlendMode::clip) {
		material.ZWriteEnable = video::EZW_ON;
		material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
		material.MaterialTypeParam = 0.5f;
	} else {
		// We don't have working transparency sorting. Disable Z-Write for
		// correct results for clipped-alpha at least.
		material.ZWriteEnable = video::EZW_OFF;
		material.MaterialType = video::EMT_ONETEXTURE_BLEND;
		setBlendMode(material, blendmode);
	}
	material.setTexture(0, texture.ref);

	return material;
}

bool ParticleManager::addParticle(std::unique_ptr<Particle> toadd)
{
	MutexAutoLock lock(m_particle_list_lock);

	auto material = getMaterialForParticle(toadd.get());

	const ParticleShape shape = toadd->getShape();

	ParticleBuffer *found = nullptr;
	// simple shortcut when multiple particles of the same type get added
	if (!m_particles.empty()) {
		auto &last = m_particles.back();
		if (last->getBuffer() && last->getBuffer()->getMaterial(0) == material
				&& last->getBuffer()->getShape() == shape)
			found = last->getBuffer();
	}
	// search fitting buffer
	if (!found) {
		for (auto &buffer : m_particle_buffers) {
			if (buffer->getMaterial(0) == material && buffer->getShape() == shape) {
				found = buffer.get();
				break;
			}
		}
	}
	// or create a new one
	if (!found) {
		auto tmp = make_irr<ParticleBuffer>(m_env, material, shape);
		found = tmp.get();
		m_particle_buffers.push_back(std::move(tmp));
	}

	if (!toadd->attachToBuffer(found)) {
		infostream << "ParticleManager: buffer full, dropping particle" << std::endl;
		return false;
	}
	m_particles.push_back(std::move(toadd));
	return true;
}

void ParticleManager::addParticleSpawner(u64 id, std::unique_ptr<ParticleSpawner> toadd)
{
	MutexAutoLock lock(m_spawner_list_lock);

	auto &slot = m_particle_spawners[id];
	if (slot) {
		// do not kill spawners here. children are still alive
		errorstream << "ParticleManager: Failed to add spawner with id " << id
				<< ". Id already in use." << std::endl;
		return;
	}
	slot = std::move(toadd);
}

void ParticleManager::deleteParticleSpawner(u64 id)
{
	MutexAutoLock lock(m_spawner_list_lock);

	auto it = m_particle_spawners.find(id);
	if (it != m_particle_spawners.end()) {
		m_dying_particle_spawners.push_back(std::move(it->second));
		m_particle_spawners.erase(it);
	}
}
