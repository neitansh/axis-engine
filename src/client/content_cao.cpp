// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "content_cao.h"
#include "net_diagnostics.h"
#include <IBillboardSceneNode.h>
#include <ICameraSceneNode.h>
#include <IMeshManipulator.h>
#include <ISceneNode.h>
#include <AnimatedMeshSceneNode.h>
#include "client/client.h"
#include "client/renderingengine.h"
#include "client/sound.h"
#include "client/texturesource.h"
#include "client/mapblock_mesh.h"
#include "client/content_mapblock.h"
#include "client/meshgen/collector.h"
#include "log.h"
#include "util/basic_macros.h"
#include "util/numeric.h"
#include "util/serialize.h"
#include "camera.h" // CameraModes
#include "collision.h"
#include "content_cso.h"
#include "clientobject.h"
#include "environment.h"
#include "itemdef.h"
#include "localplayer.h"
#include "map.h"
#include "mesh.h"
#include "nodedef.h"
#include "settings.h"
#include "porting.h"
#include <limits>
#include "tool.h"
#include "wieldmesh.h"

#include "client/shader.h"
#include "client/minimap.h"
#include <quaternion.h>
#include <SMesh.h>
#include <IMeshBuffer.h>
#include <CMeshBuffer.h>

#include <algorithm>
#include <cmath>
#include <variant>
#include <optional>

struct ToolCapabilities;

std::unordered_map<u16, ClientActiveObject::Factory> ClientActiveObject::m_types;

template<typename T>
void SmoothTranslator<T>::init(T current)
{
	val_old = current;
	val_current = current;
	val_target = current;
	// A teleport is not movement: start from a standstill
	val_rate = T();
	anim_time = 0;
	anim_time_counter = 0;
	aim_is_end = true;
}

template<typename T>
void SmoothTranslator<T>::update(T new_target, bool is_end_position, float update_interval)
{
	aim_is_end = is_end_position;
	val_old = val_current;
	val_target = new_target;
	if (update_interval > 0) {
		anim_time = update_interval;
	} else {
		if (anim_time < 0.001 || anim_time > 1.0)
			anim_time = anim_time_counter;
		else
			anim_time = anim_time * 0.9 + anim_time_counter * 0.1;
	}
	anim_time_counter = 0;
}

template<typename T>
void SmoothTranslator<T>::translate(f32 dtime)
{
	anim_time_counter = anim_time_counter + dtime;

	// Critically damped approach to the target: both the position and the
	// rate it moves at stay continuous, with no overshoot.
	//
	// Running straight at the newest position instead — a fresh straight line
	// on every packet — keeps the path smooth but makes the *speed* jump:
	// measured on a rising airship it swung between a standstill and twice
	// the real speed, ten times a second, because packets never arrive
	// exactly when they are due. From outside the eye averages that away.
	// Standing on the deck, with the camera bolted to it, there is nothing to
	// average: it reads as shaking. Damping spends the jitter on a fraction
	// of a block of lag instead.
	const f32 tau = rangelim(anim_time, 0.05f, 0.5f);
	const f32 omega = 2.0f / tau;
	const f32 x = omega * dtime;

	// Padé approximation of exp(-x); cheaper and stable for large steps
	const f32 decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

	const T offset = val_current - val_target;
	const T damped = (val_rate + offset * omega) * dtime;

	val_rate = (val_rate - damped * omega) * decay;
	val_current = val_target + (offset + damped) * decay;
}

void SmoothTranslatorWrapped::translate(f32 dtime)
{
	anim_time_counter = anim_time_counter + dtime;
	f32 val_diff = std::abs(val_target - val_old);
	if (val_diff > 180.f)
		val_diff = 360.f - val_diff;

	f32 moveratio = 1.0;
	if (anim_time > 0.001)
		moveratio = anim_time_counter / anim_time;
	f32 move_end = aim_is_end ? 1.0 : 1.5;

	// Move a bit less than should, to avoid oscillation
	moveratio = std::min(moveratio * 0.8f, move_end);
	wrappedApproachShortest(val_current, val_target,
		val_diff * moveratio, 360.f);
}

void SmoothTranslatorWrappedv3f::translate(f32 dtime)
{
	anim_time_counter = anim_time_counter + dtime;

	v3f val_diff_v3f;
	val_diff_v3f.X = std::abs(val_target.X - val_old.X);
	val_diff_v3f.Y = std::abs(val_target.Y - val_old.Y);
	val_diff_v3f.Z = std::abs(val_target.Z - val_old.Z);

	if (val_diff_v3f.X > 180.f)
		val_diff_v3f.X = 360.f - val_diff_v3f.X;

	if (val_diff_v3f.Y > 180.f)
		val_diff_v3f.Y = 360.f - val_diff_v3f.Y;

	if (val_diff_v3f.Z > 180.f)
		val_diff_v3f.Z = 360.f - val_diff_v3f.Z;

	f32 moveratio = 1.0;
	if (anim_time > 0.001)
		moveratio = anim_time_counter / anim_time;
	f32 move_end = aim_is_end ? 1.0 : 1.5;

	// Move a bit less than should, to avoid oscillation
	moveratio = std::min(moveratio * 0.8f, move_end);
	wrappedApproachShortest(val_current.X, val_target.X,
		val_diff_v3f.X * moveratio, 360.f);

	wrappedApproachShortest(val_current.Y, val_target.Y,
		val_diff_v3f.Y * moveratio, 360.f);

	wrappedApproachShortest(val_current.Z, val_target.Z,
		val_diff_v3f.Z * moveratio, 360.f);
}

/*
	Other stuff
*/

static bool setMaterialTextureAndFilters(video::SMaterial &material,
	const std::string &texturestring, ITextureSource *tsrc)
{
	bool use_trilinear_filter = g_settings->getBool("trilinear_filter");
	bool use_bilinear_filter = g_settings->getBool("bilinear_filter");
	bool use_anisotropic_filter = g_settings->getBool("anisotropic_filter");

	video::ITexture *texture = tsrc->getTextureForMesh(texturestring);
	if (!texture)
		return false;

	material.setTexture(0, texture);

	// don't filter low-res textures, makes them look blurry
	const core::dimension2d<u32> &size = texture->getOriginalSize();
	if (std::min(size.Width, size.Height) < TEXTURE_FILTER_MIN_SIZE)
		use_trilinear_filter = use_bilinear_filter = false;

	material.forEachTexture([=] (auto &tex) {
		setMaterialFilters(tex, use_bilinear_filter, use_trilinear_filter,
				use_anisotropic_filter);
	});
	return true;
}

static void setBillboardTextureMatrix(scene::IBillboardSceneNode *bill,
		float txs, float tys, int col, int row)
{
	video::SMaterial& material = bill->getMaterial(0);
	core::matrix4& matrix = material.getTextureMatrix(0);
	matrix.setTextureTranslate(txs*col, tys*row);
	matrix.setTextureScale(txs, tys);
}

static bool logOnce(const std::ostringstream &from, std::ostream &log_to)
{
	thread_local std::vector<u64> logged;

	std::string message = from.str();
	u64 hash = murmur_hash_64_ua(message.data(), message.length(), 0xBADBABE);

	if (std::find(logged.begin(), logged.end(), hash) != logged.end())
		return false;
	logged.push_back(hash);
	log_to << message << std::endl;
	return true;
}

static void setColorParam(scene::ISceneNode *node, video::SColor color)
{
	for (u32 i = 0; i < node->getMaterialCount(); ++i)
		node->getMaterial(i).ColorParam = color;
}

static scene::SMesh *generateNodeMesh(Client *client, MapNode n,
	std::vector<MeshAnimationInfo> &animation)
{
	auto *ndef = client->ndef();
	auto *shdsrc = client->getShaderSource();

	MeshCollector collector(v3f(0), v3f());
	{
		MeshMakeData mmd(ndef, 1, MeshGrid{1});
		n.setParam1(0xff);
		mmd.fillSingleNode(n);
		MapblockMeshGenerator(&mmd, &collector).generate();
	}

	const AlphaMode alpha_mode = ndef->get(n).alpha;

	auto mesh = make_irr<scene::SMesh>();
	animation.clear();
	for (int layer = 0; layer < MAX_TILE_LAYERS; layer++) {
		for (PreMeshBuffer &p : collector.prebuffers[layer]) {
			// reset the pre-computed light data stored in the vertex color,
			// since we do that ourselves via updateLight().
			for (auto &v : p.vertices)
				v.Color.set(0xFFFFFFFF);
			// but still apply the tile color
			p.applyTileColor();

			if (p.layer.material_flags & MATERIAL_FLAG_ANIMATION) {
				animation.emplace_back(MeshAnimationInfo{
					mesh->getMeshBufferCount(), 0, p.layer});
			}

			auto buf = make_irr<scene::SMeshBuffer>();
			buf->append(&p.vertices[0], p.vertices.size(),
					&p.indices[0], p.indices.size());

			// Set up material
			auto &mat = buf->Material;
			p.layer.applyMaterialOptions(mat, layer);
			getAdHocNodeShader(mat, shdsrc, "object_shader", alpha_mode, layer == 1);

			mesh->addMeshBuffer(buf.get());
		}
	}
	mesh->recalculateBoundingBox();
	return mesh.release();
}

/*
	GenericCAO
*/

GenericCAO::GenericCAO(Client *client, ClientEnvironment *env):
		ClientActiveObject(0, client, env)
{
	if (!client) {
		ClientActiveObject::registerType(getType(), create);
	} else {
		m_client = client;
	}
}

bool GenericCAO::getCollisionBox(aabb3f *toset) const
{
	if (m_prop.physical)
	{
		//update collision box
		toset->MinEdge = m_prop.collisionbox.MinEdge * BS;
		toset->MaxEdge = m_prop.collisionbox.MaxEdge * BS;

		// An attached object carries no position of its own: m_position still
		// holds whatever it had before being attached. getPosition() resolves
		// the parent chain, so a box attached to a moving object ends up where
		// it is actually drawn and can be stood on.
		const v3f position = getParent() ? getPosition() : m_position;

		toset->MinEdge += position;
		toset->MaxEdge += position;

		return true;
	}

	return false;
}

bool GenericCAO::collideWithObjects() const
{
	return m_prop.collideWithObjects;
}

void GenericCAO::initialize(const std::string &data)
{
	processInitData(data);
}

void GenericCAO::processInitData(const std::string &data)
{
	std::istringstream is(data, std::ios::binary);
	const u8 version = readU8(is);

	if (version < 1) {
		errorstream << "GenericCAO: Unsupported init data version"
				<< std::endl;
		return;
	}

	// PROTOCOL_VERSION >= 37
	m_name = deSerializeString16(is);
	m_is_player = readU8(is);
	m_id = readU16(is);
	m_position = readV3F32(is);
	m_rotation = readV3F32(is);
	m_hp = readU16(is);

	if (m_is_player) {
		// Check if it's the current player
		LocalPlayer *player = m_env->getLocalPlayer();
		if (player && player->getName() == m_name) {
			m_is_local_player = true;
			m_is_visible = false;
			player->setCAO(this);
		}
	}

	const u8 num_messages = readU8(is);
	for (u8 i = 0; i < num_messages; i++) {
		std::string message = deSerializeString32(is);
		processMessage(message);
	}

	m_rotation = wrapDegrees_0_360_v3f(m_rotation);
	pos_translator.init(m_position);
	rot_translator.init(m_rotation);
	updateNodePos();
}

GenericCAO::~GenericCAO()
{
	removeFromScene(true);
}

bool GenericCAO::getSelectionBox(aabb3f *toset) const
{
	if (!m_prop.is_visible || !m_is_visible || m_is_local_player) {
		return false;
	}
	*toset = m_selection_box;
	return true;
}

void GenericCAO::updateParentChain() const
{
	if (!m_matrixnode)
		return;
	// Update the entire chain of nodes to ensure absolute position is correct
	std::vector<scene::ISceneNode *> chain;
	for (scene::ISceneNode *node = m_matrixnode; node; node = node->getParent())
		chain.push_back(node);
	for (auto it = chain.rbegin(); it != chain.rend(); ++it)
		(*it)->updateAbsolutePosition();
}

const v3f GenericCAO::getPosition() const
{
	if (!getParent())
		return pos_translator.val_current;

	// Calculate real position in world based on MatrixNode
	if (m_matrixnode) {
		// FIXME work around #16221 which is caused by the camera position and thus
		// offset not being in sync with the player (parent) CAO position.
		// A better solution might restrict this update to the local player only
		// or keep player and camera position in sync.
		GenericCAO::updateParentChain();
		v3s16 camera_offset = m_env->getCameraOffset();
		return m_matrixnode->getAbsolutePosition() +
				intToFloat(camera_offset, BS);
	}

	return m_position;
}

bool GenericCAO::isImmortal() const
{
	return itemgroup_get(getGroups(), "immortal");
}

scene::ISceneNode *GenericCAO::getSceneNode() const
{
	if (m_meshnode) {
		return m_meshnode;
	}

	if (m_animated_meshnode) {
		return m_animated_meshnode;
	}

	if (m_wield_meshnode) {
		return m_wield_meshnode;
	}

	if (m_spritenode) {
		return m_spritenode;
	}
	return NULL;
}

scene::AnimatedMeshSceneNode *GenericCAO::getAnimatedMeshSceneNode() const
{
	return m_animated_meshnode;
}

void GenericCAO::setChildrenVisible(bool toset)
{
	for (object_t cao_id : m_attachment_child_ids) {
		GenericCAO *obj = m_env->getGenericCAO(cao_id);
		if (obj) {
			// Check if the entity is forced to appear in first person.
			obj->setVisible(obj->m_force_visible ? true : toset);
		}
	}
}

void GenericCAO::setAttachment(object_t parent_id, const std::string &bone,
		v3f position, v3f rotation, bool force_visible)
{
	// Do checks to avoid circular references
	// See similar check in `UnitSAO::setAttachment` (but with different types).
	{
		auto *obj = m_env->getActiveObject(parent_id);
		if (obj == this) {
			assert(false);
			return;
		}
		bool problem = false;
		if (obj) {
			// The chain of wanted parent must not refer or contain "this"
			for (obj = obj->getParent(); obj; obj = obj->getParent()) {
				if (obj == this) {
					problem = true;
					break;
				}
			}
		}
		if (problem) {
			warningstream << "Network or mod bug: "
				<< "Attempted to attach object " << m_id << " to parent "
				<< parent_id << " but former is an (in)direct parent of latter." << std::endl;
			return;
		}
	}

	const auto old_parent = m_attachment_parent_id;
	m_attachment_parent_id = parent_id;
	m_attachment_bone = bone;
	m_attachment_position = position;
	m_attachment_rotation = rotation;
	m_force_visible = force_visible;

	ClientActiveObject *parent = m_env->getActiveObject(parent_id);

	if (parent_id != old_parent) {
		if (auto *o = m_env->getActiveObject(old_parent))
			o->removeAttachmentChild(m_id);
		if (parent)
			parent->addAttachmentChild(m_id);
	}
	updateAttachments();

	// Forcibly show attachments if required by set_attach
	if (m_force_visible) {
		m_is_visible = true;
	} else if (!m_is_local_player) {
		// Objects attached to the local player should be hidden in first person
		m_is_visible = !m_attached_to_local ||
			m_client->getCamera()->getCameraMode() != CAMERA_MODE_FIRST;
		m_force_visible = false;
	} else {
		// Local players need to have this set,
		// otherwise first person attachments fail.
		m_is_visible = true;
	}
}

void GenericCAO::getAttachment(object_t *parent_id, std::string *bone, v3f *position,
	v3f *rotation, bool *force_visible) const
{
	*parent_id = m_attachment_parent_id;
	*bone = m_attachment_bone;
	*position = m_attachment_position;
	*rotation = m_attachment_rotation;
	*force_visible = m_force_visible;
}

void GenericCAO::clearChildAttachments()
{
	// Cannot use for-loop here: setAttachment() modifies 'm_attachment_child_ids'!
	while (!m_attachment_child_ids.empty()) {
		const auto child_id = *m_attachment_child_ids.begin();

		if (auto *child = m_env->getActiveObject(child_id))
			child->clearParentAttachment();
		else
			removeAttachmentChild(child_id);
	}
}

void GenericCAO::addAttachmentChild(object_t child_id)
{
	m_attachment_child_ids.insert(child_id);
}

void GenericCAO::removeAttachmentChild(object_t child_id)
{
	m_attachment_child_ids.erase(child_id);
}

ClientActiveObject* GenericCAO::getParent() const
{
	return m_attachment_parent_id ? m_env->getActiveObject(m_attachment_parent_id) :
			nullptr;
}

void GenericCAO::removeFromScene(bool permanent)
{
	// Should be true when removing the object permanently
	// and false when refreshing (eg: updating visuals)
	if (m_env && permanent) {
		// The client does not know whether this object does re-appear to
		// a later time, thus do not clear child attachments.

		clearParentAttachment();
	}

	if (auto shadow = RenderingEngine::get_shadow_renderer())
		if (auto node = getSceneNode())
			shadow->removeNodeFromShadowList(node);

	if (m_meshnode) {
		m_meshnode->remove();
		m_meshnode->drop();
		m_meshnode = nullptr;
	} else if (m_animated_meshnode)	{
		m_animated_meshnode->remove();
		m_animated_meshnode->drop();
		m_animated_meshnode = nullptr;
	} else if (m_wield_meshnode) {
		m_wield_meshnode->remove();
		m_wield_meshnode->drop();
		m_wield_meshnode = nullptr;
	} else if (m_spritenode) {
		m_spritenode->remove();
		m_spritenode->drop();
		m_spritenode = nullptr;
	}

	m_meshnode_animation.clear();

	if (m_matrixnode) {
		m_matrixnode->remove();
		m_matrixnode->drop();
		m_matrixnode = nullptr;
	}

	if (m_nametag) {
		m_client->getCamera()->removeNametag(m_nametag);
		m_nametag = nullptr;
	}

	if (m_marker && m_client->getMinimap())
		m_client->getMinimap()->removeMarker(&m_marker);
}

void GenericCAO::addToScene(ITextureSource *tsrc, scene::ISceneManager *smgr)
{
	m_smgr = smgr;

	if (getSceneNode() != NULL) {
		return;
	}

	m_visuals_expired = false;

	if (!m_prop.is_visible)
		return;

	infostream << "GenericCAO::addToScene(): " <<
		enum_to_string(es_ObjectVisual, m_prop.visual)<< std::endl;

	auto updateMaterialType = [this](bool hw_skin) {
		if (m_prop.visual != OBJECTVISUAL_NODE &&
				m_prop.visual != OBJECTVISUAL_WIELDITEM &&
				m_prop.visual != OBJECTVISUAL_ITEM)
		{
			IShaderSource *shader_source = m_client->getShaderSource();
			MaterialType material_type;

			if (m_prop.shaded && m_prop.glow == 0)
				material_type = (m_prop.use_texture_alpha) ?
					TILE_MATERIAL_ALPHA : TILE_MATERIAL_BASIC;
			else
				material_type = (m_prop.use_texture_alpha) ?
					TILE_MATERIAL_PLAIN_ALPHA : TILE_MATERIAL_PLAIN;

			ShaderFeatures features;
			features.skinning = hw_skin;
			u32 shader_id = shader_source->getShader(
					"object_shader", material_type, NDT_NORMAL, features);
			m_material_type = shader_source->getShaderInfo(shader_id).material;
		} else {
			// Not used, so make sure it's not valid
			m_material_type = video::EMT_INVALID;
		}
	};

	m_matrixnode = m_smgr->addDummyTransformationSceneNode();
	m_matrixnode->grab();

	auto setMaterial = [this](video::SMaterial &mat) {
		if (m_material_type != video::EMT_INVALID)
			mat.MaterialType = m_material_type;
		mat.FogEnable = true;
		mat.forEachTexture([] (auto &tex) {
			// Как и у блоков карты: тексель берётся один, а между уровнями
			// мельчения идёт переход, см. setMaterialFilters()
			tex.MinFilter = video::ETMINF_NEAREST_MIPMAP_LINEAR;
			tex.MagFilter = video::ETMAGF_NEAREST;
		});
	};

	auto setSceneNodeMaterials = [&] (scene::ISceneNode *node, bool hw_skin = false) {
		updateMaterialType(hw_skin);
		node->forEachMaterial(setMaterial);
	};

	switch(m_prop.visual) {
	case OBJECTVISUAL_UPRIGHT_SPRITE: {
		updateMaterialType(false);

		auto mesh = make_irr<scene::SMesh>();
		f32 dx = BS * m_prop.visual_size.X / 2;
		f32 dy = BS * m_prop.visual_size.Y / 2;
		video::SColor c(0xFFFFFFFF);

		video::S3DVertex vertices[4] = {
			video::S3DVertex(-dx, -dy, 0, 0,0,1, c, 1,1),
			video::S3DVertex( dx, -dy, 0, 0,0,1, c, 0,1),
			video::S3DVertex( dx,  dy, 0, 0,0,1, c, 0,0),
			video::S3DVertex(-dx,  dy, 0, 0,0,1, c, 1,0),
		};
		if (m_is_player) {
			// Move minimal Y position to 0 (feet position)
			for (auto &vertex : vertices)
				vertex.Pos.Y += dy;
		}
		const u16 indices[] = {0,1,2,2,3,0};

		for (int face : {0, 1}) {
			auto buf = make_irr<scene::SMeshBuffer>();
			// Front (0) or Back (1)
			if (face == 1) {
				for (auto &v : vertices)
					v.Normal *= -1;
				for (int i : {0, 2})
					std::swap(vertices[i].Pos, vertices[i+1].Pos);
			}
			buf->append(vertices, 4, indices, 6);

			// Set material
			setMaterial(buf->getMaterial());
			buf->getMaterial().ColorParam = c;

			// Add to mesh
			mesh->addMeshBuffer(buf.get());
		}

		mesh->recalculateBoundingBox();
		m_meshnode = m_smgr->addMeshSceneNode(mesh.get(), m_matrixnode);
		m_meshnode->grab();
		break;
	} case OBJECTVISUAL_CUBE: {
		scene::IMesh *mesh = createCubeMesh(v3f(BS,BS,BS));
		m_meshnode = m_smgr->addMeshSceneNode(mesh, m_matrixnode);
		m_meshnode->grab();
		mesh->drop();

		m_meshnode->setScale(m_prop.visual_size);

		setSceneNodeMaterials(m_meshnode);

		m_meshnode->forEachMaterial([this] (auto &mat) {
			mat.BackfaceCulling = m_prop.backface_culling;
		});
		break;
	} case OBJECTVISUAL_MESH: {
		scene::IAnimatedMesh *mesh = m_client->getMesh(m_prop.mesh, true);
		if (mesh) {
			if (!checkMeshNormals(mesh)) {
				infostream << "GenericCAO: recalculating normals for mesh "
					<< m_prop.mesh << std::endl;
				m_smgr->getMeshManipulator()->
						recalculateNormals(mesh, true, false);
			}

			m_animated_meshnode = m_smgr->addAnimatedMeshSceneNode(mesh, m_matrixnode);
			m_animated_meshnode->grab();
			mesh->drop(); // The scene node took hold of it
			m_animated_meshnode->setScale(m_prop.visual_size);

			// set vertex colors to ensure alpha is set
			setMeshColor(m_animated_meshnode->getMesh(), video::SColor(0xFFFFFFFF));

			setSceneNodeMaterials(m_animated_meshnode, mesh->needsHwSkinning());

			m_animated_meshnode->forEachMaterial([this] (auto &mat) {
				mat.BackfaceCulling = m_prop.backface_culling;
			});

			m_animated_meshnode->setOnAnimateCallback([&](f32 dtime) {
				for (auto it = m_bone_override.begin(); it != m_bone_override.end();) {
					BoneOverride &props = it->second;
					props.dtime_passed += dtime;

					if (props.isIdentity()) {
						it = m_bone_override.erase(it);
						continue;
					}

					if (auto *bone = m_animated_meshnode->getJointNode(it->first.c_str())) {
						bone->setPosition(props.getPosition(bone->getPosition()));
						bone->setRotation(props.getRotationEulerDeg(bone->getRotation()));
						bone->setScale(props.getScale(bone->getScale()));
					}
					++it;
				}
			});
		} else
			errorstream<<"GenericCAO::addToScene(): Could not load mesh "<<m_prop.mesh<<std::endl;
		break;
	}
	case OBJECTVISUAL_WIELDITEM:
	case OBJECTVISUAL_ITEM: {
		ItemStack item;
		if (m_prop.wield_item.empty()) {
			// Old format, only textures are specified.
			infostream << "textures: " << m_prop.textures.size() << std::endl;
			if (!m_prop.textures.empty()) {
				infostream << "textures[0]: " << m_prop.textures[0]
					<< std::endl;
				IItemDefManager *idef = m_client->idef();
				item = ItemStack(m_prop.textures[0], 1, 0, idef);
			}
		} else {
			infostream << "serialized form: " << m_prop.wield_item << std::endl;
			item.deSerialize(m_prop.wield_item, m_client->idef());
		}
		m_wield_meshnode = new WieldMeshSceneNode(m_smgr, -1);
		m_wield_meshnode->setItem(item, m_client,
			(m_prop.visual == OBJECTVISUAL_WIELDITEM));

		m_wield_meshnode->setScale(m_prop.visual_size / 2.0f);
		break;
	} case OBJECTVISUAL_NODE: {
		auto *mesh = generateNodeMesh(m_client, m_prop.node, m_meshnode_animation);
		assert(mesh);

		m_meshnode = m_smgr->addMeshSceneNode(mesh, m_matrixnode);
		m_meshnode->setSharedMaterials(true);
		m_meshnode->grab();
		mesh->drop();

		m_meshnode->setScale(m_prop.visual_size);

		setSceneNodeMaterials(m_meshnode);
		break;
	} default:
		m_spritenode = m_smgr->addBillboardSceneNode(m_matrixnode);
		m_spritenode->grab();

		setSceneNodeMaterials(m_spritenode);

		m_spritenode->setSize(v2f(m_prop.visual_size.X,
				m_prop.visual_size.Y) * BS);
		setBillboardTextureMatrix(m_spritenode, 1, 1, 0, 0);

		// This also serves as fallback for unknown visual types
		if (m_prop.visual != OBJECTVISUAL_SPRITE) {
			m_spritenode->getMaterial(0).setTexture(0,
				tsrc->getTextureForMesh("unknown_object.png"));
		}
		break;
	}

	/* don't update while punch texture modifier is active */
	if (m_reset_textures_timer < 0)
		updateTextures(m_current_texture_modifier);

	if (scene::ISceneNode *node = getSceneNode()) {
		node->setParent(m_matrixnode);

		if (auto shadow = RenderingEngine::get_shadow_renderer())
			shadow->addNodeToShadowList(node);
	}

	updateNametag();
	updateMarker();
	updateNodePos();
	updateAttachments();
	setNodeLight(m_last_light);
	updateMeshCulling();

	if (m_animated_meshnode) {
		u32 mat_count = m_animated_meshnode->getMaterialCount();
		assert(mat_count == m_animated_meshnode->getMesh()->getMeshBufferCount());
		u32 max_tex_idx = 0;
		for (u32 i = 0; i < mat_count; ++i) {
			max_tex_idx = std::max(max_tex_idx,
					m_animated_meshnode->getMesh()->getTextureSlot(i));
		}
		if (mat_count == 0 || m_prop.textures.empty()) {
			// nothing
		} else if (max_tex_idx >= m_prop.textures.size()) {
			std::ostringstream oss;
			oss << "GenericCAO::addToScene(): Model "
				<< m_prop.mesh << " is missing " << (max_tex_idx + 1 - m_prop.textures.size())
				<< " more texture(s), this is deprecated.";
			logOnce(oss, warningstream);

			video::ITexture *last = m_animated_meshnode->getMaterial(0).TextureLayers[0].Texture;
			for (u32 i = 1; i < mat_count; i++) {
				auto &layer = m_animated_meshnode->getMaterial(i).TextureLayers[0];
				if (!layer.Texture)
					layer.Texture = last;
				last = layer.Texture;
			}
		}
	}

	for (auto &&[track_name, anim] : deferred_set_animation_cmds) {
		applyTrackAnimation(std::move(track_name), anim);
	}
	deferred_set_animation_cmds.clear();
}

void GenericCAO::updateLight(u32 day_night_ratio)
{
	if (m_prop.glow < 0)
		return;

	u16 light_at_pos = 0;
	u8 light_at_pos_intensity = 0;
	bool pos_ok = false;

	v3s16 pos[3];
	u16 npos = getLightPosition(pos);
	for (u16 i = 0; i < npos; i++) {
		bool this_ok;
		MapNode n = m_env->getMap().getNode(pos[i], &this_ok);
		if (this_ok) {
			// Get light level at the position plus the entity glow
			u16 this_light = getInteriorLight(n, m_prop.glow, m_client->ndef());
			u8 this_light_intensity = MYMAX(this_light & 0xFF, this_light >> 8);
			if (this_light_intensity > light_at_pos_intensity) {
				light_at_pos = this_light;
				light_at_pos_intensity = this_light_intensity;
			}
			pos_ok = true;
		}
	}
	if (!pos_ok)
		light_at_pos = LIGHT_SUN;

	video::SColor light;

	// Encode light into color, adding a small boost
	// based on the entity glow.
	light = encode_light(light_at_pos, m_prop.glow);

	if (light != m_last_light) {
		m_last_light = light;
		setNodeLight(light);
	}
}

void GenericCAO::setNodeLight(const video::SColor &light_color)
{
	if (m_prop.visual == OBJECTVISUAL_WIELDITEM || m_prop.visual == OBJECTVISUAL_ITEM) {
		if (m_wield_meshnode)
			m_wield_meshnode->setLightColorAndAnimation(light_color,
					m_client->getAnimationTime());
		return;
	}

	{
		auto *node = getSceneNode();
		if (!node)
			return;
		setColorParam(node, light_color);
	}
}

u16 GenericCAO::getLightPosition(v3s16 *pos)
{
	const auto &box = m_prop.collisionbox;
	pos[0] = floatToInt(m_position + box.MinEdge * BS, BS);
	pos[1] = floatToInt(m_position + box.MaxEdge * BS, BS);

	// Skip center pos if it falls into the same node as Min or MaxEdge
	if ((box.MaxEdge - box.MinEdge).getLengthSQ() < 3.0f)
		return 2;
	pos[2] = floatToInt(m_position + box.getCenter() * BS, BS);
	return 3;
}

void GenericCAO::updateMarker()
{
	if (!m_client->getMinimap())
		return;

	if (!m_prop.show_on_minimap) {
		if (m_marker)
			m_client->getMinimap()->removeMarker(&m_marker);
		return;
	}

	if (m_marker)
		return;

	scene::ISceneNode *node = getSceneNode();
	if (!node)
		return;
	m_marker = m_client->getMinimap()->addMarker(node);
}

void GenericCAO::updateNametag()
{
	if (m_is_local_player) // No nametag for local player
		return;

	if (m_prop.nametag.empty() || m_prop.nametag_color.getAlpha() == 0) {
		// Delete nametag
		if (m_nametag) {
			m_client->getCamera()->removeNametag(m_nametag);
			m_nametag = nullptr;
		}
		return;
	}

	scene::ISceneNode *node = getSceneNode();
	if (!node)
		return;

	v3f pos;
	pos.Y = m_prop.selectionbox.MaxEdge.Y + 0.3f;
	// Add or update nametag
	Nametag tmp{node, m_prop.nametag, m_prop.nametag_color,
			m_prop.nametag_bgcolor, m_prop.nametag_fontsize, pos,
			m_prop.nametag_scale_z};
	if (!m_nametag) {
		m_nametag = m_client->getCamera()->addNametag(tmp);
		assert(m_nametag);
	} else {
		*m_nametag = tmp;
	}
}

void GenericCAO::updateNodePos()
{
	if (getParent() != NULL)
		return;

	scene::ISceneNode *node = getSceneNode();

	if (node) {
		assert(m_matrixnode);
		v3s16 camera_offset = m_env->getCameraOffset();
		v3f pos = pos_translator.val_current -
				intToFloat(camera_offset, BS);
		getPosRotMatrix().setTranslation(pos);
		if (node != m_spritenode) { // rotate if not a sprite
			v3f rot = m_is_local_player ? -m_rotation : -rot_translator.val_current;
			setPitchYawRoll(getPosRotMatrix(), rot);
		}
	}
}

void GenericCAO::followLocalPlayer()
{
	if (!m_is_local_player)
		return;

	LocalPlayer *player = m_env->getLocalPlayer();
	if (!player)
		return;

	m_position = player->getPosition();
	pos_translator.val_current = m_position;
	pos_translator.val_target = m_position;
	m_rotation.Y = wrapDegrees_0_360(player->getYaw());
	rot_translator.val_current = m_rotation;
	rot_translator.val_target = m_rotation;

	updateNodePos();
}

/// Seconds over which a change of ride is eased out, see m_ride_blend
static constexpr f32 RIDE_BLEND_TIME = 0.25f;

void GenericCAO::step(float dtime, ClientEnvironment *env)
{
	// Handle model animations and update positions instantly to prevent lags
	if (m_is_local_player) {
		LocalPlayer *player = m_env->getLocalPlayer();
		m_position = player->getPosition();
		pos_translator.val_current = m_position;
		m_rotation.Y = wrapDegrees_0_360(player->getYaw());
		rot_translator.val_current = m_rotation;

		if (m_is_visible) {
			m_velocity = v3f(0,0,0);
			m_acceleration = v3f(0,0,0);
			const PlayerControl &controls = player->getPlayerControl();
			f32 new_speed = player->local_animation_speed;

			bool walking = false;
			if (controls.movement_speed > 0.001f) {
				new_speed *= controls.movement_speed;
				walking = true;
			}

			LocalPlayerAnimation new_anim = LocalPlayerAnimation::NO_ANIM;

			// increase speed if using fast or flying fast
			if((g_settings->getBool("fast_move") &&
					m_client->checkLocalPrivilege("fast")) &&
					(controls.aux1 ||
					(!player->touching_ground &&
					g_settings->getBool("free_move") &&
					m_client->checkLocalPrivilege("fly"))))
			{
				new_speed *= 1.5;
			}
			// slowdown speed if sneaking
			if (controls.sneak && walking)
				new_speed /= 2;

			if (walking && (controls.dig || controls.place)) {
				new_anim = LocalPlayerAnimation::WD_ANIM;
			} else if (walking) {
				new_anim = LocalPlayerAnimation::WALK_ANIM;
			} else if (controls.dig || controls.place) {
				new_anim = LocalPlayerAnimation::DIG_ANIM;
			}

			if (getParent()) {
				// If attached: Idle animation only
				new_anim = LocalPlayerAnimation::NO_ANIM;
			}

			if (new_anim == LocalPlayerAnimation::NO_ANIM) {
				new_speed = player->local_animation_speed;
			}

			setLocalPlayerAnimation(new_anim, new_speed);
		}
	}

	if (m_visuals_expired && m_smgr) {
		m_visuals_expired = false;

		// Attachments, part 1: All attached objects must be unparented first,
		// or Irrlicht causes a segmentation fault
		for (u16 cao_id : m_attachment_child_ids) {
			ClientActiveObject *obj = m_env->getActiveObject(cao_id);
			if (obj) {
				scene::ISceneNode *child_node = obj->getSceneNode();
				// The node's parent is always an IDummyTransformationSceneNode,
				// so we need to reparent that one instead.
				if (child_node)
					child_node->getParent()->setParent(m_smgr->getRootSceneNode());
			}
		}

		if (m_animated_meshnode) {
			// Preserve current frames of playing animations
			// TODO might want to preserve bone transformation matrices in the future
			const auto &anim = m_animated_meshnode->getAnimation();
			for (const auto &[track_nr, track] : anim.tracks) {
				m_animation.tracks[track_nr].cur_frame = track.cur_frame;
			}
		}

		removeFromScene(false);
		addToScene(m_client->tsrc(), m_smgr);

		if (m_animated_meshnode && m_animated_meshnode->getMesh()) {
			for (auto &[track_nr, track] : m_animation.tracks) {
				track.clamp(m_animated_meshnode->getMesh()->getMaxFrameNumber(track_nr));
			}
			m_animated_meshnode->getAnimation() = m_animation; // Restore animation
		}

		// Attachments, part 2: Now that the parent has been refreshed, put its attachments back
		for (u16 cao_id : m_attachment_child_ids) {
			ClientActiveObject *obj = m_env->getActiveObject(cao_id);
			if (obj)
				obj->updateAttachments();
		}
	}

	// Make sure m_is_visible is always applied
	scene::ISceneNode *node = getSceneNode();
	if (node)
		node->setVisible(m_is_visible);

	if(getParent() != NULL) // Attachments should be glued to their parent by Irrlicht
	{
		// Set these for later
		m_position = getPosition();
		m_velocity = v3f(0,0,0);
		m_acceleration = v3f(0,0,0);
		pos_translator.val_current = m_position;
		pos_translator.val_target = m_position;

		// An attached object is what a player actually stands on, so it is
		// what the instrument has to watch: its motion is the parent's, seen
		// through the scene graph
		if (g_netdiag)
			g_netdiag->objectDrawn(getId(), m_position, dtime,
				m_is_player && !m_is_local_player);
	} else {
		rot_translator.translate(dtime);
		v3f lastpos = pos_translator.val_current;

		if(m_prop.physical)
		{
			aabb3f box = m_prop.collisionbox;
			box.MinEdge *= BS;
			box.MaxEdge *= BS;
			CollisionMoveResult moveresult;
			v3f p_pos = m_position;
			v3f p_velocity = m_velocity;
			moveresult = collisionMoveSimple(env,env->getGameDef(),
					box, m_prop.stepheight, dtime,
					&p_pos, &p_velocity, m_acceleration,
					this, m_prop.collideWithObjects, m_prop.step_up_mode);
			// Apply results
			m_position = p_pos;
			m_velocity = p_velocity;

			bool is_end_position = moveresult.collides;
			pos_translator.update(m_position, is_end_position, dtime);
			pos_translator.translate(dtime);
		} else {
			v3f played_pos;
			v3f played_rot;

			if (playMotion(dtime, &played_pos, &played_rot)) {
				// Replaying the server's own timeline: nothing to predict,
				// nothing to correct, and therefore nothing to jitter.
				m_position = played_pos;
				pos_translator.val_current = played_pos;
				pos_translator.val_target = played_pos;
				rot_translator.val_current = played_rot;
				rot_translator.val_target = played_rot;
				m_rotation = played_rot;
			} else {
				m_position += dtime * m_velocity + 0.5 * dtime * dtime * m_acceleration;
				m_velocity += dtime * m_acceleration;
				pos_translator.update(m_position, pos_translator.aim_is_end,
						pos_translator.anim_time);
				pos_translator.translate(dtime);
			}

		}
		// Someone standing on something moving is drawn against it rather
		// than against the world: their world position describes the deck as
		// they saw it, which is not the deck this client is drawing.
		if (m_ride_id != 0) {
			if (ClientActiveObject *ride = m_env->getActiveObject(m_ride_id)) {
				const v3f carried = ride->getPosition() + m_ride_offset;

				m_position = carried;
				pos_translator.val_current = carried;
				pos_translator.val_target = carried;
			}
		}

		// Stepping on or off swaps which position is drawn, and the two are
		// apart by however far the deck moved since the world one was true.
		// Left alone that lands as a jump, so it is paid off over a moment.
		v3f drawn = pos_translator.val_current;

		if (m_ride_id != m_ride_previous) {
			if (m_ride_has_last)
				m_ride_blend = m_ride_last_drawn - drawn;

			m_ride_blend_left = RIDE_BLEND_TIME;
			m_ride_previous = m_ride_id;
		}

		if (m_ride_blend_left > 0.0f) {
			m_ride_blend_left = std::max(0.0f, m_ride_blend_left - dtime);

			drawn += m_ride_blend * (m_ride_blend_left / RIDE_BLEND_TIME);
			m_position = drawn;
			pos_translator.val_current = drawn;
			pos_translator.val_target = drawn;
		}

		m_ride_last_drawn = drawn;
		m_ride_has_last = true;

		updateNodePos();

		if (g_netdiag)
			g_netdiag->objectDrawn(getId(), pos_translator.val_current, dtime,
					m_is_player && !m_is_local_player, m_ride_id);

		float moved = lastpos.getDistanceFrom(pos_translator.val_current);
		m_step_distance_counter += moved;
		if (m_step_distance_counter > 1.5f * BS) {
			m_step_distance_counter = 0.0f;
			if (!m_is_local_player && m_prop.makes_footstep_sound) {
				const NodeDefManager *ndef = m_client->ndef();
				v3f foot_pos = getPosition() * (1.0f/BS)
						+ v3f(0.0f, m_prop.collisionbox.MinEdge.Y, 0.0f);
				v3s16 node_below_pos = floatToInt(foot_pos + v3f(0.0f, -0.5f, 0.0f),
						1.0f);
				MapNode n = m_env->getMap().getNode(node_below_pos);
				SoundSpec spec = ndef->get(n).sound_footstep;
				// Reduce footstep gain, as non-local-player footsteps are
				// somehow louder.
				spec.gain *= 0.6f;
				// The footstep-sound doesn't travel with the object. => vel=0
				m_client->sound()->playSoundAt(0, spec, foot_pos, v3f(0.0f));
			}
		}
	}

	m_anim_timer += dtime;
	if(m_anim_timer >= m_anim_framelength)
	{
		m_anim_timer -= m_anim_framelength;
		m_anim_frame++;
		if(m_anim_frame >= m_anim_num_frames)
			m_anim_frame = 0;
	}

	updateTextureAnim();

	if(m_reset_textures_timer >= 0)
	{
		m_reset_textures_timer -= dtime;
		if(m_reset_textures_timer <= 0) {
			m_reset_textures_timer = -1;
			updateTextures(m_previous_texture_modifier);
		}
	}

	if (node && std::abs(m_prop.automatic_rotate) > 0.001f) {
		// This is the child node's rotation. It is only used for automatic_rotate.
		v3f local_rot = node->getRotation();
		local_rot.Y = modulo360f(local_rot.Y - dtime * core::RADTODEG *
				m_prop.automatic_rotate);
		node->setRotation(local_rot);
	}

	if (!getParent() && m_prop.automatic_face_movement_dir &&
			(fabs(m_velocity.Z) > 0.001f || fabs(m_velocity.X) > 0.001f)) {
		float target_yaw = atan2(m_velocity.Z, m_velocity.X) * 180 / M_PI
				+ m_prop.automatic_face_movement_dir_offset;
		float max_rotation_per_sec =
				m_prop.automatic_face_movement_max_rotation_per_sec;

		if (max_rotation_per_sec > 0) {
			wrappedApproachShortest(m_rotation.Y, target_yaw,
				dtime * max_rotation_per_sec, 360.f);
		} else {
			// Negative values of max_rotation_per_sec mean disabled.
			m_rotation.Y = target_yaw;
		}

		rot_translator.val_current = m_rotation;
		updateNodePos();
	}
}

static void setMeshBufferTextureCoords(scene::IMeshBuffer *buf, const v2f *uv, u32 count)
{
	assert(buf->getVertexType() == video::EVT_STANDARD);
	assert(buf->getVertexCount() == count);
	auto *vertices = static_cast<video::S3DVertex *>(buf->getVertices());
	for (u32 i = 0; i < count; i++)
		vertices[i].TCoords = uv[i];
	buf->setDirty(scene::EBT_VERTEX);
}

void GenericCAO::updateTextureAnim()
{
	if(m_spritenode)
	{
		scene::ICameraSceneNode* camera =
				m_spritenode->getSceneManager()->getActiveCamera();
		if(!camera)
			return;
		v3f cam_to_entity = m_spritenode->getAbsolutePosition()
				- camera->getAbsolutePosition();
		cam_to_entity.normalize();

		int row = m_tx_basepos.Y;
		int col = m_tx_basepos.X;

		// Yawpitch goes rightwards
		if (m_tx_select_horiz_by_yawpitch) {
			if (cam_to_entity.Y > 0.75)
				col += 5;
			else if (cam_to_entity.Y < -0.75)
				col += 4;
			else {
				float mob_dir =
						atan2(cam_to_entity.Z, cam_to_entity.X) / M_PI * 180.;
				float dir = mob_dir - m_rotation.Y;
				dir = wrapDegrees_180(dir);
				if (std::fabs(wrapDegrees_180(dir - 0)) <= 45.1f)
					col += 2;
				else if(std::fabs(wrapDegrees_180(dir - 90)) <= 45.1f)
					col += 3;
				else if(std::fabs(wrapDegrees_180(dir - 180)) <= 45.1f)
					col += 0;
				else if(std::fabs(wrapDegrees_180(dir + 90)) <= 45.1f)
					col += 1;
				else
					col += 4;
			}
		}

		// Animation goes downwards
		row += m_anim_frame;

		float txs = m_tx_size.X;
		float tys = m_tx_size.Y;
		setBillboardTextureMatrix(m_spritenode, txs, tys, col, row);
	}

	else if (m_meshnode) {
		if (m_prop.visual == OBJECTVISUAL_UPRIGHT_SPRITE) {
			int row = m_tx_basepos.Y;
			int col = m_tx_basepos.X;

			// Animation goes downwards
			row += m_anim_frame;

			const auto &tx = m_tx_size;
			v2f t[4] = { // cf. vertices in GenericCAO::addToScene()
				tx * v2f(col+1, row+1),
				tx * v2f(col, row+1),
				tx * v2f(col, row),
				tx * v2f(col+1, row),
			};
			auto mesh = m_meshnode->getMesh();
			setMeshBufferTextureCoords(mesh->getMeshBuffer(0), t, 4);
			setMeshBufferTextureCoords(mesh->getMeshBuffer(1), t, 4);
		} else if (m_prop.visual == OBJECTVISUAL_NODE) {
			// same calculation as MapBlockMesh::animate() with a global timer
			const float time = m_client->getAnimationTime();
			for (auto &it : m_meshnode_animation) {
				const TileLayer &tile = it.tile;
				int frameno = (int)(time * 1000 / tile.animation_frame_length_ms)
					% tile.animation_frame_count;

				if (frameno == it.frame)
					continue;
				it.frame = frameno;

				auto *buf = m_meshnode->getMesh()->getMeshBuffer(it.i);

				const FrameSpec &frame = (*tile.frames)[frameno];
				buf->getMaterial().setTexture(0, frame.texture);
			}
		}
	}
}

// Do not pass by reference, see header.
void GenericCAO::updateTextures(std::string mod)
{
	ITextureSource *tsrc = m_client->tsrc();

	m_previous_texture_modifier = m_current_texture_modifier;
	m_current_texture_modifier = mod;

	if (m_spritenode) {
		if (m_prop.visual == OBJECTVISUAL_SPRITE) {
			std::string texturestring = "no_texture.png";
			if (!m_prop.textures.empty())
				texturestring = m_prop.textures[0];
			texturestring += mod;

			video::SMaterial &material = m_spritenode->getMaterial(0);
			material.MaterialType = m_material_type;
			setMaterialTextureAndFilters(material, texturestring, tsrc);
		}
	}

	else if (m_animated_meshnode) {
		if (m_prop.visual == OBJECTVISUAL_MESH) {
			for (u32 i = 0; i < m_animated_meshnode->getMaterialCount(); ++i) {
				const auto texture_idx = m_animated_meshnode->getMesh()->getTextureSlot(i);
				if (texture_idx >= m_prop.textures.size())
					continue;
				std::string texturestring = m_prop.textures[texture_idx];
				if (texturestring.empty())
					continue; // Empty texture string means don't modify that material
				texturestring += mod;

				// Set material flags and texture
				video::SMaterial &material = m_animated_meshnode->getMaterial(i);
				material.MaterialType = m_material_type;
				material.BackfaceCulling = m_prop.backface_culling;
				setMaterialTextureAndFilters(material, texturestring, tsrc);
			}
		}
	}

	else if (m_meshnode) {
		if(m_prop.visual == OBJECTVISUAL_CUBE)
		{
			for (u32 i = 0; i < 6; ++i)
			{
				std::string texturestring = "no_texture.png";
				if(m_prop.textures.size() > i)
					texturestring = m_prop.textures[i];
				texturestring += mod;

				// Set material flags and texture
				video::SMaterial &material = m_meshnode->getMaterial(i);
				material.MaterialType = m_material_type;
				setMaterialTextureAndFilters(material, texturestring, tsrc);
			}
		} else if (m_prop.visual == OBJECTVISUAL_UPRIGHT_SPRITE) {
			scene::IMesh *mesh = m_meshnode->getMesh();
			{
				std::string tname = "no_texture.png";
				if (!m_prop.textures.empty())
					tname = m_prop.textures[0];
				tname += mod;

				auto &material = m_meshnode->getMaterial(0);
				setMaterialTextureAndFilters(material, tname, tsrc);
			}
			{
				std::string tname = "no_texture.png";
				if (m_prop.textures.size() >= 2)
					tname = m_prop.textures[1];
				else if (!m_prop.textures.empty())
					tname = m_prop.textures[0];
				tname += mod;

				auto &material = m_meshnode->getMaterial(1);
				setMaterialTextureAndFilters(material, tname, tsrc);
			}
			// Set mesh color (only if lighting is disabled)
			if (m_prop.glow < 0)
				setMeshColor(mesh, {255, 255, 255, 255});
		}
	}
	// Prevent showing the player after changing texture
	if (m_is_local_player)
		updateMeshCulling();
}

void GenericCAO::updateAnimation(u16 track_nr)
{
	if (!m_animated_meshnode)
		return;

	if (m_local_player_animation) {
		// Reset local player animation override
		m_local_player_animation = false;
		m_animated_meshnode->getAnimation() = m_animation;
		return;
	}

	m_animated_meshnode->getAnimation().tracks[track_nr] = m_animation.tracks[track_nr];
}

void GenericCAO::setLocalPlayerAnimation(LocalPlayerAnimation local_anim, float speed)
{
	if (!m_animated_meshnode)
		return;

	assert(m_is_local_player);
	LocalPlayer *player = m_env->getLocalPlayer();

	if (local_anim == player->last_animation &&
			speed == player->last_animation_speed)
		return; // no change

	v2f range = player->local_animations[static_cast<u8>(local_anim)];
	if (range == v2f()) {
		if (m_local_player_animation) {
			// Reset local player animation override
			m_local_player_animation = false;
			m_animated_meshnode->getAnimation() = m_animation;
		}
		return; // animation not defined, stick to current animation
	}

	scene::TrackAnimSpec anim;
	anim.setFrameRange(range.X, range.Y);
	anim.fps = speed;
	anim.cur_frame = anim.fps >= 0 ? anim.min_frame : anim.max_frame;

	m_local_player_animation = true;
	m_animated_meshnode->getAnimation() = scene::AnimSpec{{{0, anim}}};

	player->last_animation = local_anim;
	player->last_animation_speed = speed;
}

void GenericCAO::updateAttachments()
{
	ClientActiveObject *parent = getParent();

	m_attached_to_local = parent && parent->isLocalPlayer();

	/*
	Following cases exist:
		m_attachment_parent_id == 0 && !parent
			This object is not attached
		m_attachment_parent_id != 0 && parent
			This object is attached
		m_attachment_parent_id != 0 && !parent
			This object will be attached as soon the parent is known
		m_attachment_parent_id == 0 && parent
			Impossible case
	*/

	if (!parent) { // Detach or don't attach
		if (m_matrixnode) {
			v3s16 camera_offset = m_env->getCameraOffset();
			v3f old_pos = getPosition();

			m_matrixnode->setParent(m_smgr->getRootSceneNode());
			getPosRotMatrix().setTranslation(old_pos - intToFloat(camera_offset, BS));
			m_matrixnode->updateAbsolutePosition();
		}
	}
	else // Attach
	{
		parent->updateAttachments();
		scene::ISceneNode *parent_node = parent->getSceneNode();
		scene::AnimatedMeshSceneNode *parent_animated_mesh_node =
				parent->getAnimatedMeshSceneNode();
		if (parent_animated_mesh_node && !m_attachment_bone.empty()) {
			parent_node = parent_animated_mesh_node->getJointNode(m_attachment_bone.c_str());
		}

		if (m_matrixnode && parent_node) {
			m_matrixnode->setParent(parent_node);
			parent_node->updateAbsolutePosition();
			getPosRotMatrix().setTranslation(m_attachment_position);
			//setPitchYawRoll(getPosRotMatrix(), m_attachment_rotation);
			// use Irrlicht eulers instead
			getPosRotMatrix().setRotationDegrees(m_attachment_rotation);
			m_matrixnode->updateAbsolutePosition();
		}
	}
}

bool GenericCAO::visualExpiryRequired(const ObjectProperties &new_) const
{
	const ObjectProperties &old = m_prop;
	/* Visuals do not need to be expired for:
	 * - nametag props: handled by updateNametag()
	 * - textures:      handled by updateTextures()
	 * - sprite props:  handled by updateTextureAnim()
	 * - glow:          handled by updateLight()
	 * - any other properties that do not change appearance
	 */

	bool uses_legacy_texture = new_.wield_item.empty() &&
		(new_.visual == OBJECTVISUAL_WIELDITEM || new_.visual == OBJECTVISUAL_ITEM);
	// Ordered to compare primitive types before std::vectors
	return old.backface_culling != new_.backface_culling ||
		old.is_visible != new_.is_visible ||
		old.shaded != new_.shaded ||
		old.use_texture_alpha != new_.use_texture_alpha ||
		old.node != new_.node ||
		old.mesh != new_.mesh ||
		old.visual != new_.visual ||
		old.visual_size != new_.visual_size ||
		old.wield_item != new_.wield_item ||
		old.colors != new_.colors ||
		(uses_legacy_texture && old.textures != new_.textures);
}

static scene::TrackId readTrackIdentifier(std::istringstream &is)
{
	// Possible formats:
	// - Track number > 0, no track name
	// - Track number = 0, track name follows
	u16 track_number = readU16(is);
	if (track_number > 0)
		return (u16)(track_number - 1);
	return deSerializeString16(is);
}

std::optional<u16> GenericCAO::resolveTrackId(const scene::TrackId &track_id)
{
	if (!m_animated_meshnode)
		return std::nullopt;

	const auto *mesh = m_animated_meshnode->getMesh();

	if (const auto *track_name = std::get_if<std::string>(&track_id)) {
		if (const std::optional<u16> opt = mesh->getTrackNumber(*track_name))
			return *opt;
		warningstream << "Track name " << track_name << " not found in mesh " << m_prop.mesh << std::endl;
		return std::nullopt;
	}

	u16 track_nr = std::get<u16>(track_id);
	u16 max_track_nr = mesh->getTrackCount();
	if (track_nr >= max_track_nr) {
		// 1-indexed track number for consistency with Lua API
		warningstream << "Track number " << (track_nr + 1) << " out of bounds for mesh " << m_prop.mesh
			<< " (max: " << max_track_nr << ")" << std::endl;
		return std::nullopt;
	}

	return track_nr;
}

void GenericCAO::applyTrackAnimation(scene::TrackId &&track_id, scene::TrackAnimSpec anim)
{
	auto *track_name = std::get_if<std::string>(&track_id);
	if (track_name && !m_smgr) {
		// Not added to scene yet, mesh has not been resolved.
		// Defer resolving the track name to after the scene node has been set up.
		deferred_set_animation_cmds.emplace_back(std::move(*track_name), anim);
		return;
	}

	const auto track_nr = resolveTrackId(track_id);
	if (!track_nr)
		return;

	if (m_animated_meshnode) {
		anim.clamp(m_animated_meshnode->getMesh()->getMaxFrameNumber(*track_nr));
	}

	// Update stored animation in either case.
	// This becomes relevant if local animations are left unspecified,
	// in which case the stored animation is reapplied.
	m_animation.tracks[*track_nr] = anim;
	if (!m_is_local_player) {
		updateAnimation(*track_nr);
	} else {
		const auto &local_anims = m_env->getLocalPlayer()->local_animations;
		bool is_known = track_nr == 0 && std::any_of(local_anims.begin(), local_anims.end(),
				[&](v2f range) {
					return anim.min_frame == range.X && anim.max_frame == range.Y;
				});
		// Apply the animation if it is not a known local animation
		if (!is_known) {
			updateAnimation(*track_nr);
		}
	}
}


/*
	Network motion playback
*/

/// How many samples the timeline keeps. Enough to cover a stall of a second
/// at the usual packet rate, and bounded so a long session cannot grow it.
static constexpr size_t MOTION_HISTORY = 24;

void GenericCAO::resetMotion()
{
	if (g_netdiag && m_motion_active)
		g_netdiag->objectReset(getId());

	m_motion.clear();
	m_motion_active = false;
	m_motion_newest = 0.0f;
	m_motion_clock = 0.0f;
}

/// A gap this much larger than the going rate did not come from the network:
/// the object stood still and the server had nothing to send. There is no path
/// between the two places to play back, so the timeline starts afresh.
static constexpr f32 MOTION_PAUSE_FACTOR = 4.0f;

/**
 * How far past the newest packet playback may carry an object on, in intervals.
 *
 * Beyond this the object is guessing, and a guess that runs long has to be
 * taken back visibly. One interval covers the ordinary case of a packet a
 * little late; a server that went quiet leaves the object standing, which is
 * the honest thing to show.
 */
static constexpr f32 MOTION_COAST_LIMIT = 1.0f;

/**
 * How many lost packets in a row the timeline will account for.
 *
 * Past this the arrival is no longer evidence of a few dropped packets but of
 * a connection that stopped; stretching one step across it would replay the
 * whole silence as slow motion.
 */
static constexpr int MOTION_MAX_MISSED = 4;

/// How many recent gaps between arrivals the jitter estimate is drawn from
static constexpr size_t MOTION_JITTER_WINDOW = 8;

void GenericCAO::pushMotion(f32 interval, v3f pos, v3f rot)
{
	const f32 arrived = porting::getTimeMs() / 1000.0f;

	// A pause is not slow movement. Carrying its length into the timeline
	// would both stretch one step across it and leave the buffer sized for a
	// gap that will not come again.
	if (m_motion_active && m_motion.size() >= 2) {
		const f32 usual = (m_motion.back().time - m_motion.front().time)
			/ (m_motion.size() - 1);

		if (interval > usual * MOTION_PAUSE_FACTOR)
			resetMotion();
	}

	// The interval is what the server measured between this packet and the
	// previous one, so stamping it forward rebuilds the server's own clock.
	f32 step = std::max(interval, 0.001f);

	// A packet that never arrived still took its time. The server knows
	// nothing of the loss and keeps reporting one interval per packet, so
	// stamping that blindly moves playback one interval closer to the end of
	// the data with every packet lost - the buffer then drains for good and
	// what is left is a stall and a jump. Counting the missing steps by how
	// long it has actually been puts the gap back in the timeline, where
	// interpolation crosses it as movement.
	if (m_motion_active && m_motion.size() >= 2) {
		// A late packet and a lost one look alike in a single gap. What tells
		// them apart is how unevenly this connection has been delivering:
		// anything the observed jitter can account for is treated as late,
		// because counting it as lost would stretch a step that was never
		// missed and set playback drifting the other way.
		//
		// Jitter is measured from below - the middle of the gaps against the
		// shortest of them. Loss only ever makes gaps longer, so it moves the
		// top of that range and leaves this estimate alone; an estimate that
		// grew with the loss would hide the very thing it is here to find.
		std::array<f32, MOTION_JITTER_WINDOW> gaps;
		size_t count = 0;

		for (size_t i = m_motion.size() - 1; i > 0 && count < gaps.size(); i--)
			gaps[count++] = m_motion[i].arrived - m_motion[i - 1].arrived;

		std::sort(gaps.begin(), gaps.begin() + count);

		const f32 jitter = count >= 3
				? std::max(0.0f, gaps[count / 2] - gaps[0]) : 0.0f;
		const f32 since = arrived - m_motion.back().arrived;

		if (since > step * 1.5f + jitter) {
			const int missed = std::min(
					(int)std::floor((since - jitter) / step + 0.5f) - 1,
					MOTION_MAX_MISSED);

			if (missed > 0)
				step += step * missed;
		}
	}

	m_motion_newest += step;

	m_motion.push_back({m_motion_newest, arrived, pos, rot});

	while (m_motion.size() > MOTION_HISTORY)
		m_motion.pop_front();

	if (!m_motion_active) {
		m_motion_active = true;
		m_motion_clock = m_motion_newest - interval;
	}
}

bool GenericCAO::playMotion(f32 dtime, v3f *pos, v3f *rot)
{
	if (!m_motion_active || m_motion.size() < 2)
		return false;

	m_motion_clock += dtime;

	// How far behind the newest packet playback must sit.
	//
	// The depth has to cover how unevenly packets *arrive*, which is not the
	// same as how unevenly the server *sends*: measured on a live server the
	// sending was steady to half a millisecond while one packet in five
	// hundred arrived 130 ms late. Sizing the buffer by the sending rhythm
	// left it dry exactly on those packets, and a dry buffer is a stall
	// followed by a catch-up — the small, rare jerk that was left.
	//
	// So the depth is the spread between the earliest and latest a packet has
	// arrived relative to its place on the timeline, plus one interval so
	// there is always a sample ahead to interpolate towards.
	f32 offset_min = std::numeric_limits<f32>::max();
	f32 offset_max = std::numeric_limits<f32>::lowest();

	for (const MotionSample &sample : m_motion) {
		const f32 offset = sample.arrived - sample.time;

		offset_min = std::min(offset_min, offset);
		offset_max = std::max(offset_max, offset);
	}

	const f32 interval = (m_motion.back().time - m_motion.front().time)
		/ (m_motion.size() - 1);

	// The spread of the window alone leaves the buffer living on the edge:
	// measured on a live server it came to about one interval of slack, while
	// one packet in a hundred arrived half an interval late. The buffer then
	// ran dry more than once a second, and every dry frame is a stop followed
	// by a catch-up. Half an interval of headroom costs that much latency once
	// and buys back those stops.
	const f32 target = (offset_max - offset_min) + interval * 1.5f;

	const f32 behind = m_motion_newest - m_motion_clock;

	if (g_netdiag) {
		// Playback that has run past the newest packet is holding still: the
		// buffer is dry, which is what a stall on screen is made of
		const bool dry = m_motion_clock >= m_motion.back().time;
		g_netdiag->objectPlayback(getId(), behind, target, m_motion.size(), dry);
	}

	// Playback runs on the local clock, which drifts against the server's.
	// Rather than snapping — a snap is a jump on screen — the clock is run a
	// few percent fast or slow until the buffer is the right depth again.
	if (behind > target * 2.0f)
		m_motion_clock += dtime * 0.05f;
	else if (behind < target * 0.5f)
		m_motion_clock -= dtime * 0.05f;

	// A gap far beyond anything normal means the connection stalled; picking
	// up where the timeline left off would replay old motion in fast forward.
	if (behind > target * 8.0f) {
		m_motion_clock = m_motion_newest - target;
	}

	const MotionSample &oldest = m_motion.front();

	if (m_motion_clock < oldest.time)
		m_motion_clock = oldest.time;

	for (size_t i = 1; i < m_motion.size(); i++) {
		const MotionSample &a = m_motion[i - 1];
		const MotionSample &b = m_motion[i];

		if (m_motion_clock > b.time)
			continue;

		const f32 span = b.time - a.time;
		const f32 t = span > 0.0001f ? (m_motion_clock - a.time) / span : 1.0f;

		*pos = a.pos + (b.pos - a.pos) * t;

		// Angles wrap, so each one takes the short way round
		auto turn = [](f32 from, f32 to, f32 part) {
			f32 diff = to - from;

			while (diff > 180.0f)
				diff -= 360.0f;
			while (diff < -180.0f)
				diff += 360.0f;

			return from + diff * part;
		};

		rot->X = turn(a.rot.X, b.rot.X, t);
		rot->Y = turn(a.rot.Y, b.rot.Y, t);
		rot->Z = turn(a.rot.Z, b.rot.Z, t);

		return true;
	}

	// Playback has run past the newest packet. Holding the last known state
	// stops the object dead for a frame and then jumps it when the next packet
	// lands, which is exactly what the eye picks out as a stutter. Carrying it
	// on at the speed it last had keeps the motion continuous; the error that
	// buys is bounded by how long we do it, so it is only done for a short
	// while and then it does stop.
	const MotionSample &last = m_motion.back();
	const MotionSample &before = m_motion[m_motion.size() - 2];

	const f32 span = last.time - before.time;
	const f32 ahead = m_motion_clock - last.time;

	if (span > 0.0001f && ahead < MOTION_COAST_LIMIT * span) {
		const v3f speed = (last.pos - before.pos) / span;

		*pos = last.pos + speed * ahead;
	} else {
		*pos = last.pos;
	}

	*rot = last.rot;

	return true;
}

void GenericCAO::processMessage(const std::string &data)
{
	//infostream<<"GenericCAO: Got message"<<std::endl;
	std::istringstream is(data, std::ios::binary);
	// command
	u8 cmd = readU8(is);
	if (cmd == AO_CMD_SET_PROPERTIES) {
		ObjectProperties newprops;
		newprops.show_on_minimap = m_is_player; // default

		newprops.deSerialize(is);

		// Check what exactly changed
		bool expire_visuals = visualExpiryRequired(newprops);
		bool textures_changed = m_prop.textures != newprops.textures;

		// Apply changes
		m_prop = std::move(newprops);

		m_selection_box = m_prop.selectionbox;
		m_selection_box.MinEdge *= BS;
		m_selection_box.MaxEdge *= BS;

		m_tx_size.X = 1.0f / m_prop.spritediv.X;
		m_tx_size.Y = 1.0f / m_prop.spritediv.Y;

		if(!m_initial_tx_basepos_set){
			m_initial_tx_basepos_set = true;
			m_tx_basepos = m_prop.initial_sprite_basepos;
		}
		if (m_is_local_player) {
			LocalPlayer *player = m_env->getLocalPlayer();
			player->makes_footstep_sound = m_prop.makes_footstep_sound;
			aabb3f collision_box = m_prop.collisionbox;
			collision_box.MinEdge *= BS;
			collision_box.MaxEdge *= BS;
			player->setCollisionbox(collision_box);
			player->setEyeHeight(m_prop.eye_height);
			player->setZoomFOV(m_prop.zoom_fov);
		}

		if ((m_is_player && !m_is_local_player) && m_prop.nametag.empty())
			m_prop.nametag = m_name;
		if (m_is_local_player)
			m_prop.show_on_minimap = false;

		if (expire_visuals) {
			expireVisuals();
		} else {
			if (textures_changed) {
				// don't update while punch texture modifier is active
				if (m_reset_textures_timer < 0)
					updateTextures(m_current_texture_modifier);
			}
			updateNametag();
			updateMarker();
		}
	} else if (cmd == AO_CMD_UPDATE_POSITION) {
		// Not sent by the server if this object is an attachment.
		// We might however get here if the server notices the object being detached before the client.
		m_position = readV3F32(is);
		m_velocity = readV3F32(is);
		m_acceleration = readV3F32(is);
		m_rotation = readV3F32(is);

		m_rotation = wrapDegrees_0_360_v3f(m_rotation);
		bool do_interpolate = readU8(is);
		bool is_end_position = readU8(is);
		float update_interval = readF32(is);

		// the Axis: what carries this object, when something does. Optional
		// and last in the message, so its absence just means nothing does.
		m_ride_id = 0;
		if (is.rdbuf()->in_avail() > 0) {
			m_ride_id = readU16(is);
			m_ride_offset = readV3F32(is);
		}

		if(getParent() != NULL) // Just in case
			return;

		// Diagnosis: what arrived, and when. Lets the server's own motion be
		// reconstructed from the packets and compared against what the
		// interpolator finally puts on screen — the three candidate causes
		// (uneven simulation, uneven delivery, interpolation artefact) look
		// identical at the output and quite different here.
		if (!m_prop.physical && !getParent() &&
				g_settings->getBool("debug_platform_ride")) {
			const u64 now = porting::getTimeMs();

			warningstream << "recv"
				<< " id=" << m_id
				<< " t=" << now
				<< " since=" << (m_last_packet_ms ? now - m_last_packet_ms : 0)
				<< " y=" << (m_position.Y / BS)
				<< " vy=" << (m_velocity.Y / BS)
				<< " interval=" << update_interval
				<< " end=" << (int)is_end_position
				<< " render_y=" << (pos_translator.val_current.Y / BS)
				<< std::endl;

			m_last_packet_ms = now;
		}

		// Motion goes onto the playback timeline; a teleport clears it, since
		// there is no path between the two places to play back.
		const bool buffered = !m_prop.physical && !getParent();

		if (buffered && do_interpolate) {
			pushMotion(update_interval, m_position, m_rotation);
		} else if (buffered) {
			resetMotion();
		}

		if (g_netdiag)
			g_netdiag->objectPacket(getId(), m_name, update_interval);

		if(do_interpolate)
		{
			if(!m_prop.physical)
				pos_translator.update(m_position, is_end_position, update_interval);
		} else {
			pos_translator.init(m_position);
		}
		rot_translator.update(m_rotation, false, update_interval);
		updateNodePos();
	} else if (cmd == AO_CMD_SET_TEXTURE_MOD) {
		std::string mod = deSerializeString16(is);

		// immediately reset an engine issued texture modifier if a mod sends a different one
		if (m_reset_textures_timer > 0) {
			m_reset_textures_timer = -1;
			updateTextures(m_previous_texture_modifier);
		}
		updateTextures(mod);
	} else if (cmd == AO_CMD_SET_SPRITE) {
		v2s16 p = readV2S16(is);
		int num_frames = readU16(is);
		float framelength = readF32(is);
		bool select_horiz_by_yawpitch = readU8(is);

		m_tx_basepos = p;
		m_anim_num_frames = num_frames;
		m_anim_frame = 0;
		m_anim_framelength = framelength;
		m_tx_select_horiz_by_yawpitch = select_horiz_by_yawpitch;

		updateTextureAnim();
	} else if (cmd == AO_CMD_SET_PHYSICS_OVERRIDE) {
		PlayerPhysicsOverride phys; // defaults defined by ctor

		phys.speed   = readF32(is);
		phys.jump    = readF32(is);
		phys.gravity = readF32(is);

		// MT 0.4.10 legacy: send inverted for detault `true` if the server sends nothing
		phys.sneak        = !readU8(is);
		phys.sneak_glitch = !readU8(is);
		phys.new_move     = !readU8(is);

		// new overrides since 5.8.0
		if (canRead(is)) {
			phys.speed_climb            = readF32(is);
			phys.speed_crouch           = readF32(is);
			phys.liquid_fluidity        = readF32(is);
			phys.liquid_fluidity_smooth = readF32(is);
			phys.liquid_sink            = readF32(is);
			phys.acceleration_default   = readF32(is);
			phys.acceleration_air       = readF32(is);
		}

		// new overrides since 5.9.0
		if (canRead(is)) {
			phys.speed_fast        = readF32(is);
			phys.acceleration_fast = readF32(is);
			phys.speed_walk        = readF32(is);
		}

		if (m_is_local_player) {
			m_env->getLocalPlayer()->physics_override = phys;
		}
	} else if (cmd == AO_CMD_SET_ANIMATION) {
		// Read animation
		scene::TrackAnimSpec anim;
		v2f range = readV2F32(is);
		anim.fps = readF32(is);
		anim.blend_duration = readF32(is);
		// these are sent inverted so we get true when the server sends nothing
		anim.loop = !readU8(is);

		scene::TrackId track_id = (u16) 0;
		std::optional<f32> cur_frame;
		if (canRead(is)) {
			// New animation API since 5.17.0
			track_id = readTrackIdentifier(is);
			anim.priority = readS32(is);
			cur_frame = std::max(0.0f, readF32(is));
		}

		anim.setFrameRange(range.X, range.Y);
		anim.cur_frame = cur_frame.value_or(anim.fps >= 0 ? anim.min_frame : anim.max_frame);

		// Also clamps cur_frame & max_frame to the track max frame number in the mesh
		applyTrackAnimation(std::move(track_id), anim);
	} else if (cmd == AO_CMD_SET_ANIMATION_SPEED) {
		f32 new_fps = readF32(is);
		scene::TrackId track_id = (u16) 0;
		if (canRead(is)) {
			// New animation API since 5.17.0
			track_id = readTrackIdentifier(is);
		}

		auto track_nr_opt = resolveTrackId(track_id);
		if (!track_nr_opt)
			return;
		u16 track_nr = *track_nr_opt;

		auto it = m_animation.tracks.find(track_nr);
		if (it != m_animation.tracks.end()) {
			it->second.fps = new_fps;
			m_animated_meshnode->getAnimation().tracks[track_nr].fps = new_fps;
		}
	} else if (cmd == AO_CMD_STOP_ANIMATION) {
		// New animation API since 5.17.0
		const auto track_id = readTrackIdentifier(is);

		auto track_nr_opt = resolveTrackId(track_id);
		if (!track_nr_opt)
			return;
		u16 track_nr = *track_nr_opt;

		auto it = m_animation.tracks.find(track_nr);
		if (it != m_animation.tracks.end()) {
			m_animation.tracks.erase(it);
			m_animated_meshnode->getAnimation().tracks.erase(track_nr);
		}
	} else if (cmd == AO_CMD_SET_BONE_POSITION) {
		std::string bone = deSerializeString16(is);
		auto it = m_bone_override.find(bone);
		BoneOverride props;
		if (it != m_bone_override.end()) {
			props = it->second;
			// Reset timer
			props.dtime_passed = 0;
			// Save previous values for interpolation
			props.position.previous = props.position.vector;
			props.rotation.previous = props.rotation.next;
			props.scale.previous = props.scale.vector;
		} else {
			// Disable interpolation
			props.position.interp_duration = 0.0f;
			props.rotation.interp_duration = 0.0f;
			props.scale.interp_duration = 0.0f;
		}
		// Read new values
		props.position.vector = readV3F32(is);
		props.rotation.next = core::quaternion(readV3F32(is) * core::DEGTORAD);

		if (!canRead(is)) {
			// For PROTOCOL_VERSION < 44
			// scale.vector : default
			props.position.absolute = true;
			props.rotation.absolute = true;
		} else {
			// For PROTOCOL_VERSION >= 44
			props.scale.vector = readV3F32(is);
			props.position.interp_duration = readF32(is);
			props.rotation.interp_duration = readF32(is);
			props.scale.interp_duration = readF32(is);
			u8 absoluteFlag = readU8(is);
			props.position.absolute = (absoluteFlag & 1) > 0;
			props.rotation.absolute = (absoluteFlag & 2) > 0;
			props.scale.absolute = (absoluteFlag & 4) > 0;
		}
		m_bone_override[bone] = props;
	} else if (cmd == AO_CMD_ATTACH_TO) {
		u16 parent_id = readS16(is);
		std::string bone = deSerializeString16(is);
		v3f position = readV3F32(is);
		v3f rotation = readV3F32(is);
		bool force_visible = false;
		if (canRead(is)) {
			// >= 5.4.0-dev
			force_visible = readU8(is);
		}

		setAttachment(parent_id, bone, position, rotation, force_visible);
	} else if (cmd == AO_CMD_PUNCHED) {
		u16 result_hp = readU16(is);

		// Use this instead of the send damage to not interfere with prediction
		s32 damage = (s32)m_hp - (s32)result_hp;

		m_hp = result_hp;

		if (m_is_local_player)
			m_env->getLocalPlayer()->hp = m_hp;

		if (damage > 0)
		{
			if (m_hp == 0)
			{
				// TODO: Execute defined fast response
				// As there is no definition, make a smoke puff
				ClientSimpleObject *simple = createSmokePuff(
						m_smgr, m_env, m_position,
						v2f(m_prop.visual_size.X, m_prop.visual_size.Y) * BS);
				m_env->addSimpleObject(simple);
			} else if (m_reset_textures_timer < 0 && !m_prop.damage_texture_modifier.empty()) {
				m_reset_textures_timer = 0.05;
				if(damage >= 2)
					m_reset_textures_timer += 0.05 * damage;
				// Cap damage overlay to 1 second
				m_reset_textures_timer = std::min(m_reset_textures_timer, 1.0f);
				updateTextures(m_current_texture_modifier + m_prop.damage_texture_modifier);
			}
		}

		if (m_hp == 0) {
			// Same as 'Server::DiePlayer'
			clearParentAttachment();
			// Same as 'ObjectRef::l_remove'
			if (!m_is_player)
				clearChildAttachments();
		}
	} else if (cmd == AO_CMD_UPDATE_ARMOR_GROUPS) {
		m_armor_groups.clear();
		int armor_groups_size = readU16(is);
		for(int i=0; i<armor_groups_size; i++)
		{
			std::string name = deSerializeString16(is);
			int rating = readS16(is);
			m_armor_groups[name] = rating;
		}
	} else if (cmd == AO_CMD_SPAWN_INFANT) {
		u16 child_id = readU16(is);
		u8 type = readU8(is); // maybe this will be useful later
		(void)type;

		addAttachmentChild(child_id);
	} else if (cmd == AO_CMD_OBSOLETE1) {
		// Don't do anything and also don't log a warning
	} else {
		warningstream << FUNCTION_NAME
			<< ": unknown command or outdated client \""
			<< +cmd << "\"" << std::endl;
	}
}

/* \pre punchitem != NULL
 */
bool GenericCAO::directReportPunch(v3f dir, const ItemStack *punchitem,
	const ItemStack *hand_item, float time_from_last_punch)
{
	assert(punchitem);	// pre-condition
	const ToolCapabilities &toolcap =
			punchitem->getToolCapabilities(m_client->idef(), hand_item);
	PunchDamageResult result = getPunchDamage(
			m_armor_groups,
			toolcap,
			punchitem,
			time_from_last_punch,
			punchitem->wear);

	if(result.did_punch && result.damage != 0)
	{
		if(result.damage < m_hp)
		{
			m_hp -= result.damage;
		} else {
			m_hp = 0;
			// TODO: Execute defined fast response
			// As there is no definition, make a smoke puff
			ClientSimpleObject *simple = createSmokePuff(
					m_smgr, m_env, m_position,
					v2f(m_prop.visual_size.X, m_prop.visual_size.Y) * BS);
			m_env->addSimpleObject(simple);
		}
		if (m_reset_textures_timer < 0 && !m_prop.damage_texture_modifier.empty()) {
			m_reset_textures_timer = 0.05;
			if (result.damage >= 2)
				m_reset_textures_timer += 0.05 * result.damage;
			// Cap damage overlay to 1 second
			m_reset_textures_timer = std::min(m_reset_textures_timer, 1.0f);
			updateTextures(m_current_texture_modifier + m_prop.damage_texture_modifier);
		}
	}

	return false;
}

std::string GenericCAO::debugInfoText()
{
	std::ostringstream os(std::ios::binary);
	os<<"GenericCAO hp="<<m_hp<<"\n";
	os<<"armor={";
	for(ItemGroupList::const_iterator i = m_armor_groups.begin();
			i != m_armor_groups.end(); ++i)
	{
		os<<i->first<<"="<<i->second<<", ";
	}
	os<<"}";
	return os.str();
}

void GenericCAO::updateMeshCulling()
{
	if (!m_is_local_player)
		return;

	const bool hidden = m_client->getCamera()->getCameraMode() == CAMERA_MODE_FIRST;

	scene::ISceneNode *node = getSceneNode();

	if (!node)
		return;

	if (m_prop.visual ==  OBJECTVISUAL_UPRIGHT_SPRITE) {
		// upright sprite has no backface culling
		node->forEachMaterial([=] (auto &mat) {
			mat.FrontfaceCulling = hidden;
		});
		return;
	}

	if (hidden) {
		// Hide the mesh by culling both front and
		// back faces. Serious hackyness but it works for our
		// purposes. This also preserves the skeletal armature.
		node->forEachMaterial([] (auto &mat) {
			mat.BackfaceCulling = true;
			mat.FrontfaceCulling = true;
		});
	} else {
		// Restore mesh visibility.
		node->forEachMaterial([this] (auto &mat) {
			mat.BackfaceCulling = m_prop.backface_culling;
			mat.FrontfaceCulling = false;
		});
	}
}

// Prototype
static GenericCAO proto_GenericCAO(nullptr, nullptr);
