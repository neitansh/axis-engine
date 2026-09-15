// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2015 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "minimap.h"
#include <cmath>
#include "camera.h"
#include "client.h"
#include "mapblock.h" // getNodeBlockPos
#include "node_visuals.h"
#include "settings.h"
#include "client/renderingengine.h"
#include "client/texturesource.h"
#include "gettext.h"
#include "voxel.h"

////
//// MinimapUpdateThread
////

MinimapUpdateThread::~MinimapUpdateThread()
{
	for (auto &it : m_blocks_cache) {
		delete it.second;
	}

	for (auto &q : m_update_queue) {
		delete q.data;
	}
}

bool MinimapUpdateThread::pushBlockUpdate(v3s16 pos, MinimapMapblock *data)
{
	MutexAutoLock lock(m_queue_mutex);

	for (QueuedMinimapUpdate &q : m_update_queue) {
		if (q.pos == pos) {
			delete q.data;
			q.data = data;
			return false;
		}
	}

	QueuedMinimapUpdate q;
	q.pos  = pos;
	q.data = data;
	m_update_queue.push_back(q);

	return true;
}

bool MinimapUpdateThread::popBlockUpdate(QueuedMinimapUpdate *update)
{
	MutexAutoLock lock(m_queue_mutex);

	if (m_update_queue.empty())
		return false;

	*update = m_update_queue.front();
	m_update_queue.pop_front();

	return true;
}

void MinimapUpdateThread::enqueueBlock(v3s16 pos, MinimapMapblock *data)
{
	pushBlockUpdate(pos, data);
	invalidate();
}

static void mergeSurfaceTiles(std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> &into,
		std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> &&tiles)
{
	for (auto &it : tiles) {
		auto existing = into.find(it.first);
		if (existing == into.end()) {
			into.emplace(it.first, std::move(it.second));
			continue;
		}
		for (size_t i = 0; i < MAP_BLOCKSIZE * MAP_BLOCKSIZE; i++) {
			if (it.second->data[i].set)
				existing->second->data[i] = it.second->data[i];
		}
	}
}

void MinimapUpdateThread::enqueueSurface(
		std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> &&tiles, bool replace)
{
	{
		MutexAutoLock lock(m_queue_mutex);
		if (replace) {
			m_surface_queue.clear();
			m_surface_replace = true;
		}
		mergeSurfaceTiles(m_surface_queue, std::move(tiles));
	}
	invalidate();
}

void MinimapUpdateThread::invalidate()
{
	m_dirty = true;
	deferUpdate();
}

bool MinimapUpdateThread::takeScan(MinimapScan &out)
{
	MutexAutoLock lock(m_scan_mutex);
	if (!m_ready_fresh)
		return false;
	std::swap(out, m_ready);
	m_ready_fresh = false;
	return true;
}

void MinimapUpdateThread::doUpdate()
{
	QueuedMinimapUpdate update;

	while (popBlockUpdate(&update)) {
		if (update.data) {
			auto result = m_blocks_cache.emplace(update.pos, update.data);
			if (!result.second) {
				delete result.first->second;
				result.first->second = update.data;
			}
		} else {
			auto it = m_blocks_cache.find(update.pos);
			if (it != m_blocks_cache.end()) {
				delete it->second;
				m_blocks_cache.erase(it);
			}
		}
	}

	{
		std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> tiles;
		bool replace;
		{
			MutexAutoLock lock(m_queue_mutex);
			tiles.swap(m_surface_queue);
			replace = m_surface_replace;
			m_surface_replace = false;
		}
		if (replace)
			m_surface.clear();
		mergeSurfaceTiles(m_surface, std::move(tiles));
	}

	// Флаг сбрасывается до скана: всё, что придёт во время него, снова
	// поднимет флаг и разбудит поток, и ни одно обновление не потеряется.
	if (!m_dirty.exchange(false))
		return;

	v3s16 pos;
	s16 size, height;
	u32 generation;
	MinimapType type;
	{
		MutexAutoLock lock(*data_mutex);
		pos = data->pos;
		size = data->scan_size;
		height = data->mode.scan_height;
		type = data->mode.type;
		generation = data->generation;
	}

	if (size <= 0 || (type != MINIMAP_TYPE_RADAR && type != MINIMAP_TYPE_SURFACE))
		return;

	// Кэш мапблоков растёт с каждым замешенным блоком; далёкие от игрока в
	// скан не попадут, а память держат. Чистится по порогу, не на каждом
	// обходе: обход всего кэша дороже одного скана.
	if (m_blocks_cache.size() > m_evict_at) {
		const v3s16 center = getNodeBlockPos(pos);
		const s16 reach = std::max<s16>(size, 512) / MAP_BLOCKSIZE + 1;
		for (auto it = m_blocks_cache.begin(); it != m_blocks_cache.end();) {
			v3s16 d = it->first - center;
			if (std::abs(d.X) > reach || std::abs(d.Z) > reach
					|| std::abs(d.Y) > reach) {
				delete it->second;
				it = m_blocks_cache.erase(it);
			} else {
				++it;
			}
		}
		// Всё, что осталось, — рядом с игроком; следующий обход имеет
		// смысл, когда кэш снова заметно вырастет.
		m_evict_at = std::max<size_t>(4096, m_blocks_cache.size() * 2);
	}

	MinimapScan scan = std::move(m_spare);
	getMap(pos, size, height, scan);
	scan.generation = generation;

	{
		MutexAutoLock lock(m_scan_mutex);
		std::swap(m_ready, scan);
		m_ready_fresh = true;
	}
	m_spare = std::move(scan);
}

void MinimapUpdateThread::getMap(v3s16 pos, s16 size, s16 height, MinimapScan &scan)
{
	v3s16 pos_min(pos.X - size / 2, pos.Y - height / 2, pos.Z - size / 2);
	v3s16 pos_max(pos_min.X + size - 1, pos.Y + height / 2, pos_min.Z + size - 1);
	v3s16 blockpos_min = getNodeBlockPos(pos_min);
	v3s16 blockpos_max = getNodeBlockPos(pos_max);

	scan.min = pos_min;
	scan.size = size;
	scan.columns.assign((size_t)size * size, MinimapColumn());

	v3s16 blockpos;
	for (blockpos.Z = blockpos_min.Z; blockpos.Z <= blockpos_max.Z; ++blockpos.Z)
	for (blockpos.X = blockpos_min.X; blockpos.X <= blockpos_max.X; ++blockpos.X) {
		// Сверху вниз: первый мапблок с поверхностью в столбце — верхний,
		// и ниже него смотреть незачем, только счёт воздуха для радара.
		for (blockpos.Y = blockpos_max.Y; blockpos.Y >= blockpos_min.Y; --blockpos.Y) {
			auto pblock = m_blocks_cache.find(blockpos);
			if (pblock == m_blocks_cache.end())
				continue;
			const MinimapMapblock &block = *pblock->second;

			v3s16 block_node_min(blockpos * MAP_BLOCKSIZE);
			v3s16 block_node_max(block_node_min + MAP_BLOCKSIZE - 1);
			v3s16 range_min = componentwise_max(block_node_min, pos_min);
			v3s16 range_max = componentwise_min(block_node_max, pos_max);

			v3s16 p;
			for (p.Z = range_min.Z; p.Z <= range_max.Z; ++p.Z)
			for (p.X = range_min.X; p.X <= range_max.X; ++p.X) {
				v3s16 inblock_pos = p - block_node_min;
				const MinimapPixel &in_pixel =
					block.data[inblock_pos.Z * MAP_BLOCKSIZE + inblock_pos.X];

				v2s16 inmap_pos(p.X - pos_min.X, p.Z - pos_min.Z);
				MinimapColumn &out = scan.columns[inmap_pos.X + inmap_pos.Y * size];

				out.air_count = std::min<int>(255, out.air_count + in_pixel.air_count);
				if (out.known || in_pixel.n.param0 == CONTENT_AIR)
					continue;
				out.n = in_pixel.n;
				out.y = block_node_min.Y + in_pixel.height;
				out.liquid_depth = in_pixel.liquid_depth;
				out.known = true;
			}
		}
	}

	if (m_surface.empty())
		return;

	v2s16 tile_min = getContainerPos(v2s16(pos_min.X, pos_min.Z), MAP_BLOCKSIZE);
	v2s16 tile_max = getContainerPos(v2s16(pos_max.X, pos_max.Z), MAP_BLOCKSIZE);
	v2s16 tp;
	for (tp.Y = tile_min.Y; tp.Y <= tile_max.Y; ++tp.Y)
	for (tp.X = tile_min.X; tp.X <= tile_max.X; ++tp.X) {
		auto it = m_surface.find(tp);
		if (it == m_surface.end())
			continue;
		const MinimapSurfaceTile &tile = *it->second;
		v2s16 tile_node_min = tp * MAP_BLOCKSIZE;
		v2s16 range_min(std::max<s16>(tile_node_min.X, pos_min.X),
				std::max<s16>(tile_node_min.Y, pos_min.Z));
		v2s16 range_max(std::min<s16>(tile_node_min.X + MAP_BLOCKSIZE - 1, pos_max.X),
				std::min<s16>(tile_node_min.Y + MAP_BLOCKSIZE - 1, pos_max.Z));
		for (s16 z = range_min.Y; z <= range_max.Y; ++z)
		for (s16 x = range_min.X; x <= range_max.X; ++x) {
			MinimapColumn &out = scan.columns[(x - pos_min.X) + (z - pos_min.Z) * size];
			if (out.known)
				continue;
			const MinimapSurfaceColumn &in = tile.data[(z - tile_node_min.Y) * MAP_BLOCKSIZE
					+ (x - tile_node_min.X)];
			if (in.n == CONTENT_IGNORE)
				continue;
			out.n = MapNode(in.n);
			out.y = in.y;
			out.known = true;
		}
	}
}

////
//// Minimap
////

Minimap::Minimap(Client *client)
{
	this->client    = client;
	this->driver    = RenderingEngine::get_video_driver();
	this->m_tsrc    = client->getTextureSource();
	this->m_ndef    = client->getNodeDefManager();

	m_angle = 0.f;
	m_current_mode_index = 0;

	m_surface_mode_scan_height =
		g_settings->getBool("minimap_double_scan_height") ? 256 : 128;

	addMode(MINIMAP_TYPE_OFF);
	addMode(MINIMAP_TYPE_SURFACE, 256);
	addMode(MINIMAP_TYPE_SURFACE, 128);
	addMode(MINIMAP_TYPE_SURFACE, 64);
	addMode(MINIMAP_TYPE_RADAR,   512);
	addMode(MINIMAP_TYPE_RADAR,   256);
	addMode(MINIMAP_TYPE_RADAR,   128);

	data = std::make_unique<MinimapData>();
	data->minimap_shape_round = g_settings->getBool("minimap_shape_round");

	m_meshbuffer = make_irr<scene::SMeshBuffer>();
	m_meshbuffer->Vertices->Data.resize(4);
	m_meshbuffer->Indices->Data = {0, 1, 2, 2, 3, 0};
	m_meshbuffer->setHardwareMappingHint(scene::EHM_STREAM);

	m_minimap_update_thread = std::make_unique<MinimapUpdateThread>();
	m_minimap_update_thread->data = data.get();
	m_minimap_update_thread->data_mutex = &m_mutex;

	setModeIndex(0);

	m_minimap_update_thread->start();
}

Minimap::~Minimap()
{
	m_minimap_update_thread->stop();
	m_minimap_update_thread->wait();

	m_meshbuffer.reset();

	if (data->texture)
		driver->removeTexture(data->texture);

	m_markers.clear();

	data.reset();
	m_minimap_update_thread.reset();
}

void Minimap::addBlock(v3s16 pos, MinimapMapblock *data)
{
	m_minimap_update_thread->enqueueBlock(pos, data);
}

void Minimap::addSurface(v2s16 min, v2s16 max, const std::vector<content_t> &content,
		const std::vector<s16> &height, bool replace)
{
	std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> tiles;
	const s32 width = max.X - min.X + 1;
	for (s16 z = min.Y; z <= max.Y; ++z)
	for (s16 x = min.X; x <= max.X; ++x) {
		v2s16 tp = getContainerPos(v2s16(x, z), MAP_BLOCKSIZE);
		auto &tile = tiles[tp];
		if (!tile)
			tile = std::make_unique<MinimapSurfaceTile>();
		v2s16 in_tile = v2s16(x, z) - tp * MAP_BLOCKSIZE;
		size_t src = (size_t)(z - min.Y) * width + (x - min.X);
		MinimapSurfaceColumn &col = tile->data[in_tile.Y * MAP_BLOCKSIZE + in_tile.X];
		col.n = content[src];
		col.y = height[src];
		col.set = true;
	}
	m_minimap_update_thread->enqueueSurface(std::move(tiles), replace);
}

void Minimap::clearSurface()
{
	m_minimap_update_thread->enqueueSurface({}, true);
}

void Minimap::toggleMinimapShape()
{
	if (isShapeLocked())
		return;
	setMinimapShape(data->minimap_shape_round ? MINIMAP_SHAPE_SQUARE : MINIMAP_SHAPE_ROUND);
}

void Minimap::setMinimapShape(MinimapShape shape)
{
	if (isShapeLocked())
		return;

	{
		MutexAutoLock lock(m_mutex);
		data->minimap_shape_round = shape == MINIMAP_SHAPE_ROUND;
	}
	g_settings->setBool("minimap_shape_round", data->minimap_shape_round);
	m_minimap_update_thread->invalidate();
}

MinimapShape Minimap::getMinimapShape()
{
	return data->minimap_shape_round ? MINIMAP_SHAPE_ROUND : MINIMAP_SHAPE_SQUARE;
}

bool Minimap::isShapeLocked() const
{
	return data->mode.shape != MINIMAP_SHAPE_FREE;
}

void Minimap::setModeIndex(size_t index)
{
	{
		MutexAutoLock lock(m_mutex);

		if (index < m_modes.size()) {
			data->mode = m_modes[index];
			m_current_mode_index = index;
		} else {
			data->mode = {MINIMAP_TYPE_OFF, gettext("Minimap hidden"), 0, 0, "", 0,
					MINIMAP_SHAPE_FREE};
			m_current_mode_index = 0;
		}

		switch (data->mode.shape) {
		case MINIMAP_SHAPE_LOCK_SQUARE:
			data->minimap_shape_round = false;
			break;
		case MINIMAP_SHAPE_LOCK_ROUND:
			data->minimap_shape_round = true;
			break;
		default:
			data->minimap_shape_round = g_settings->getBool("minimap_shape_round");
		}
		data->scan_size = 0;
		data->generation++;
	}

	m_scan_valid = false;
	if (m_minimap_update_thread)
		m_minimap_update_thread->invalidate();
}

void Minimap::addMode(MinimapModeDef mode)
{
	if (mode.type == MINIMAP_TYPE_TEXTURE) {
		if (mode.texture.empty())
			return;
		if (mode.scale < 1)
			mode.scale = 1;
	}

	int zoom = -1;

	if (mode.label.empty()) {
		switch (mode.type) {
			case MINIMAP_TYPE_OFF:
				mode.label = gettext("Minimap hidden");
				break;
			case MINIMAP_TYPE_SURFACE:
				mode.label = gettext("Minimap in surface mode, Zoom x%d");
				if (mode.map_size > 0)
					zoom = 256 / mode.map_size;
				break;
			case MINIMAP_TYPE_RADAR:
				mode.label = gettext("Minimap in radar mode, Zoom x%d");
				if (mode.map_size > 0)
					zoom = 512 / mode.map_size;
				break;
			case MINIMAP_TYPE_TEXTURE:
				mode.label = gettext("Minimap in texture mode");
				break;
			default:
				break;
		}
	}

	if (zoom >= 0) {
		char label_buf[1024];
		porting::mt_snprintf(label_buf, sizeof(label_buf),
			mode.label.c_str(), zoom);
		mode.label = label_buf;
	}

	m_modes.push_back(mode);
}

void Minimap::addMode(MinimapType type, u16 size, const std::string &label,
		const std::string &texture, u16 scale, MinimapShapeLock shape)
{
	MinimapModeDef mode;
	mode.type = type;
	mode.label = label;
	mode.map_size = size;
	mode.texture = texture;
	mode.scale = scale;
	mode.shape = shape;
	switch (type) {
		case MINIMAP_TYPE_SURFACE:
			mode.scan_height = m_surface_mode_scan_height;
			break;
		case MINIMAP_TYPE_RADAR:
			mode.scan_height = 32;
			break;
		default:
			mode.scan_height = 0;
	}
	addMode(mode);
}

void Minimap::nextMode()
{
	if (m_modes.empty())
		return;
	m_current_mode_index++;
	if (m_current_mode_index >= m_modes.size())
		m_current_mode_index = 0;

	setModeIndex(m_current_mode_index);
}

void Minimap::setPlayerPos(v3f pos)
{
	m_player_pos = pos / BS;
	setPos(floatToInt(pos, BS));
}

void Minimap::setPos(v3s16 pos)
{
	bool changed = false;
	{
		MutexAutoLock lock(m_mutex);
		if (pos != data->pos) {
			data->pos = pos;
			changed = true;
		}
	}
	if (changed)
		m_minimap_update_thread->invalidate();
}

void Minimap::setAngle(f32 angle)
{
	m_angle = angle;
}

////
//// Показ
////

Minimap::View Minimap::computeView(s32 panel) const
{
	View v;
	v.panel = panel;
	// Показывается не меньше нод, чем просил режим: увеличение округляется
	// вниз, уменьшение — вверх.
	u16 wanted = data->mode.map_size > 0 ? data->mode.map_size : panel;
	float scale = (float)panel / wanted;
	if (scale >= 1.0f)
		v.px_per_texel = std::max(1, (int)std::floor(scale));
	else
		v.nodes_per_texel = std::max(1, (int)std::ceil(1.0f / scale));
	// Тексель запаса с каждой стороны: дробный сдвиг под положение игрока
	// никогда не откроет край текстуры.
	v.texels = (panel + v.px_per_texel - 1) / v.px_per_texel + 2;
	// Нода запаса с каждой стороны — соседи для затенения крайних текселей.
	u32 scan = (u32)v.texels * v.nodes_per_texel + 2;
	if (scan > MINIMAP_MAX_SIZE) {
		scan = MINIMAP_MAX_SIZE;
		v.texels = (scan - 2) / v.nodes_per_texel;
	}
	v.scan_size = scan;
	return v;
}

video::SColor Minimap::columnColor(const MinimapColumn &c) const
{
	const ContentFeatures &f = m_ndef->get(c.n);
	const auto &tile = f.tiledef[0], &overlay = f.tiledef_overlay[0];

	video::SColor tilecolor;
	if (!overlay.name.empty() && overlay.has_color) {
		tilecolor = overlay.color;
	} else if (overlay.name.empty() && tile.has_color) {
		tilecolor = tile.color;
	} else {
		f.visuals->getColor(c.n.param2, &tilecolor);
	}
	const video::SColor &minimap_color = f.visuals->minimap_color;
	tilecolor.setRed(tilecolor.getRed() * minimap_color.getRed() / 255);
	tilecolor.setGreen(tilecolor.getGreen() * minimap_color.getGreen() / 255);
	tilecolor.setBlue(tilecolor.getBlue() * minimap_color.getBlue() / 255);
	tilecolor.setAlpha(255);
	return tilecolor;
}

static inline video::SColor scaleColor(video::SColor c, float k)
{
	return video::SColor(c.getAlpha(),
		core::clamp((int)std::lround(c.getRed() * k), 0, 255),
		core::clamp((int)std::lround(c.getGreen() * k), 0, 255),
		core::clamp((int)std::lround(c.getBlue() * k), 0, 255));
}

void Minimap::rebuildTexture(const MinimapScan &scan, const View &view)
{
	const u16 T = view.texels;
	const u16 npp = view.nodes_per_texel;
	const s16 S = scan.size;
	const bool radar = data->mode.type == MINIMAP_TYPE_RADAR;
	const bool round = data->minimap_shape_round;

	static const video::SColor unknown(255, 10, 11, 13);
	// Свет с северо-запада: склон к нему светлее, от него темнее. Пять
	// ступеней по сумме знаков перепада к северному и западному соседу.
	static const float relief[5] = {0.66f, 0.82f, 1.0f, 1.12f, 1.25f};

	auto column = [&](s32 x, s32 z) -> const MinimapColumn & {
		return scan.columns[x + z * S];
	};

	auto nodeColor = [&](s32 x, s32 z, bool &known) -> video::SColor {
		const MinimapColumn &c = column(x, z);
		known = c.known;
		if (radar) {
			int g = c.air_count > 0
				? core::clamp(core::round32(32 + c.air_count * 8), 0, 255) : 0;
			return video::SColor(255, 0, g, 0);
		}
		if (!c.known)
			return unknown;
		video::SColor base = columnColor(c);
		int slope = 2;
		const MinimapColumn &north = column(x, z + 1);
		const MinimapColumn &west = column(x - 1, z);
		if (north.known)
			slope += (c.y > north.y) - (c.y < north.y);
		if (west.known)
			slope += (c.y > west.y) - (c.y < west.y);
		float k = relief[slope];
		if (c.liquid_depth > 0)
			k *= std::max(0.55f, 1.0f - c.liquid_depth * 0.045f);
		return scaleColor(base, k);
	};

	core::dimension2d<u32> dim(T, T);
	video::IImage *image = driver->createImage(video::ECF_A8R8G8B8, dim);

	// Центр круга — тексель игрока; радиус — половина панели.
	const float cx = (float)(S / 2 - 1) / npp + 0.5f;
	const float radius = (float)view.panel / (2.0f * view.px_per_texel) + 0.5f;

	for (u16 ty = 0; ty < T; ty++)
	for (u16 tx = 0; tx < T; tx++) {
		s32 x0 = 1 + tx * npp;
		s32 z0 = 1 + (T - 1 - ty) * npp;
		video::SColor col;
		if (npp == 1) {
			bool known;
			col = nodeColor(x0, z0, known);
		} else {
			u32 r = 0, g = 0, b = 0, n = 0;
			for (s32 dz = 0; dz < npp; dz++)
			for (s32 dx = 0; dx < npp; dx++) {
				s32 x = x0 + dx, z = z0 + dz;
				if (x + 1 >= S || z + 1 >= S)
					continue;
				bool known;
				video::SColor c = nodeColor(x, z, known);
				if (!known && !radar)
					continue;
				r += c.getRed(); g += c.getGreen(); b += c.getBlue(); n++;
			}
			col = n ? video::SColor(255, r / n, g / n, b / n) : unknown;
		}
		if (round) {
			float dx = tx + 0.5f - cx, dy = (T - 1 - ty) + 0.5f - cx;
			if (dx * dx + dy * dy > radius * radius)
				col = video::SColor(0, 0, 0, 0);
		}
		image->setPixel(tx, ty, col);
	}

	if (data->texture)
		driver->removeTexture(data->texture);
	bool mipmaps = driver->getTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, false);
	data->texture = driver->addTexture("minimap__", image);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, mipmaps);
	image->drop();
	m_texture_view = view;
}

// Вершины квадрата в NDC панели; повёрнутого вокруг центра, если круг.
static void setQuad(scene::SMeshBuffer *buf, s32 panel, f32 left, f32 top,
		f32 size, f32 rotate_deg)
{
	auto &v = buf->Vertices->Data;
	static const video::SColor white(255, 255, 255, 255);
	const f32 s = std::sin(rotate_deg * core::DEGTORAD);
	const f32 c = std::cos(rotate_deg * core::DEGTORAD);
	auto put = [&](int i, f32 px, f32 py, f32 u, f32 tv) {
		f32 dx = px - panel / 2.0f, dy = py - panel / 2.0f;
		f32 rx = dx * c + dy * s, ry = -dx * s + dy * c;
		f32 x = (rx + panel / 2.0f) / panel * 2.0f - 1.0f;
		f32 y = 1.0f - (ry + panel / 2.0f) / panel * 2.0f;
		v[i] = video::S3DVertex(x, y, 0, 0, 0, 1, white, u, tv);
	};
	put(0, left, top + size, 0, 1);
	put(1, left, top, 0, 0);
	put(2, left + size, top, 1, 0);
	put(3, left + size, top + size, 1, 1);
	buf->setDirty(scene::EBT_VERTEX);
}

void Minimap::drawMinimap(core::rect<s32> rect)
{
	if (data->mode.type == MINIMAP_TYPE_OFF)
		return;

	if (!data->textures_initialised) {
		data->minimap_overlay_round = m_tsrc->getTexture("minimap_overlay_round.png");
		data->marker_default = m_tsrc->getTexture("minimap_marker.png");
		data->textures_initialised = true;
	}

	const s32 panel = std::min(rect.getWidth(), rect.getHeight());
	if (panel <= 0)
		return;
	rect.LowerRightCorner = rect.UpperLeftCorner + v2s32(panel, panel);
	const bool round = data->minimap_shape_round;

	if (data->mode.type == MINIMAP_TYPE_TEXTURE) {
		drawTextureMode(rect);
	} else {
		View view = computeView(panel);
		u32 generation;
		{
			MutexAutoLock lock(m_mutex);
			if (data->scan_size != view.scan_size) {
				data->scan_size = view.scan_size;
				data->generation++;
				m_minimap_update_thread->invalidate();
			}
			generation = data->generation;
		}
		if (m_minimap_update_thread->takeScan(m_scan)) {
			m_scan_valid = m_scan.generation == generation
				&& m_scan.size == view.scan_size;
			if (m_scan_valid)
				rebuildTexture(m_scan, view);
		}

		if (m_scan_valid && data->texture) {
			const View &tv = m_texture_view;
			const f32 k = (f32)tv.px_per_texel / tv.nodes_per_texel;
			// Левый край текселя 0 и верхний край строки 0 в нодах: нода с
			// целой координатой n занимает [n - 0.5, n + 0.5).
			const f32 left_node = m_scan.min.X + 1 - 0.5f;
			const f32 top_node = m_scan.min.Z + 1 + (f32)tv.texels * tv.nodes_per_texel - 0.5f;
			f32 left = panel / 2.0f + (left_node - m_player_pos.X) * k;
			f32 top = panel / 2.0f - (top_node - m_player_pos.Z) * k;
			if (!round) {
				left = std::round(left);
				top = std::round(top);
			}
			setQuad(m_meshbuffer.get(), panel, left, top,
				(f32)tv.texels * tv.px_per_texel, round ? -m_angle : 0.0f);

			core::rect<s32> oldViewPort = driver->getViewPort();
			core::matrix4 oldProjMat = driver->getTransform(video::ETS_PROJECTION);
			core::matrix4 oldViewMat = driver->getTransform(video::ETS_VIEW);

			driver->setViewPort(rect);
			driver->setTransform(video::ETS_PROJECTION, core::matrix4());
			driver->setTransform(video::ETS_VIEW, core::matrix4());
			driver->setTransform(video::ETS_WORLD, core::matrix4());

			video::SMaterial &material = m_meshbuffer->getMaterial();
			material.forEachTexture([&] (auto &tex) {
				tex.MinFilter = round ? video::ETMINF_LINEAR_MIPMAP_NEAREST
					: video::ETMINF_NEAREST_MIPMAP_NEAREST;
				tex.MagFilter = round ? video::ETMAGF_LINEAR : video::ETMAGF_NEAREST;
				tex.TextureWrapU = video::ETC_CLAMP_TO_EDGE;
				tex.TextureWrapV = video::ETC_CLAMP_TO_EDGE;
			});
			material.TextureLayers[0].Texture = data->texture;
			material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
			material.ZWriteEnable = video::EZW_OFF;
			material.ZBuffer = video::ECFN_DISABLED;
			material.BackfaceCulling = false;
			driver->setMaterial(material);
			driver->drawMeshBuffer(m_meshbuffer.get());

			if (round) {
				setQuad(m_meshbuffer.get(), panel, 0, 0, (f32)panel, 0.0f);
				material.TextureLayers[0].Texture = data->minimap_overlay_round;
				driver->setMaterial(material);
				driver->drawMeshBuffer(m_meshbuffer.get());
			}

			driver->setTransform(video::ETS_VIEW, oldViewMat);
			driver->setTransform(video::ETS_PROJECTION, oldProjMat);
			driver->setViewPort(oldViewPort);
		}

		drawMarkers(rect, view);
	}

	if (!round)
		drawFrame(rect);
	drawPlayerArrow(rect, round ? 0.0f : m_angle);
}

void Minimap::drawTextureMode(const core::rect<s32> &rect)
{
	video::ITexture *texture = m_tsrc->getTexture(data->mode.texture);
	if (!texture)
		return;
	const s32 panel = rect.getWidth();
	const core::dimension2du dim = texture->getOriginalSize();
	const f32 nodes = data->mode.map_size > 0 ? data->mode.map_size : panel;
	// Текстура центрирована на (0, 0) мира, пиксель — scale нод, +Z вверх.
	const f32 wx = dim.Width * (f32)data->mode.scale;
	const f32 wz = dim.Height * (f32)data->mode.scale;
	const f32 u0 = 0.5f + (m_player_pos.X - nodes / 2) / wx;
	const f32 u1 = 0.5f + (m_player_pos.X + nodes / 2) / wx;
	const f32 v0 = 0.5f - (m_player_pos.Z + nodes / 2) / wz;
	const f32 v1 = 0.5f - (m_player_pos.Z - nodes / 2) / wz;

	auto &v = m_meshbuffer->Vertices->Data;
	static const video::SColor white(255, 255, 255, 255);
	v[0] = video::S3DVertex(-1, -1, 0, 0, 0, 1, white, u0, v1);
	v[1] = video::S3DVertex(-1,  1, 0, 0, 0, 1, white, u0, v0);
	v[2] = video::S3DVertex( 1,  1, 0, 0, 0, 1, white, u1, v0);
	v[3] = video::S3DVertex( 1, -1, 0, 0, 0, 1, white, u1, v1);
	m_meshbuffer->setDirty(scene::EBT_VERTEX);

	core::rect<s32> oldViewPort = driver->getViewPort();
	core::matrix4 oldProjMat = driver->getTransform(video::ETS_PROJECTION);
	core::matrix4 oldViewMat = driver->getTransform(video::ETS_VIEW);
	driver->setViewPort(rect);
	driver->setTransform(video::ETS_PROJECTION, core::matrix4());
	driver->setTransform(video::ETS_VIEW, core::matrix4());
	driver->setTransform(video::ETS_WORLD, core::matrix4());

	video::SMaterial &material = m_meshbuffer->getMaterial();
	material.forEachTexture([] (auto &tex) {
		tex.MinFilter = video::ETMINF_LINEAR_MIPMAP_NEAREST;
		tex.MagFilter = video::ETMAGF_LINEAR;
		tex.TextureWrapU = video::ETC_CLAMP_TO_EDGE;
		tex.TextureWrapV = video::ETC_CLAMP_TO_EDGE;
	});
	material.TextureLayers[0].Texture = texture;
	material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
	material.ZWriteEnable = video::EZW_OFF;
	material.ZBuffer = video::ECFN_DISABLED;
	driver->setMaterial(material);
	driver->drawMeshBuffer(m_meshbuffer.get());

	driver->setTransform(video::ETS_VIEW, oldViewMat);
	driver->setTransform(video::ETS_PROJECTION, oldProjMat);
	driver->setViewPort(oldViewPort);
}

static void drawOutline(video::IVideoDriver *driver, const core::rect<s32> &r,
		video::SColor color)
{
	const s32 x0 = r.UpperLeftCorner.X, y0 = r.UpperLeftCorner.Y;
	const s32 x1 = r.LowerRightCorner.X, y1 = r.LowerRightCorner.Y;
	driver->draw2DRectangle(color, core::rect<s32>(x0, y0, x1, y0 + 1));
	driver->draw2DRectangle(color, core::rect<s32>(x0, y1 - 1, x1, y1));
	driver->draw2DRectangle(color, core::rect<s32>(x0, y0 + 1, x0 + 1, y1 - 1));
	driver->draw2DRectangle(color, core::rect<s32>(x1 - 1, y0 + 1, x1, y1 - 1));
}

void Minimap::drawFrame(const core::rect<s32> &rect)
{
	static const video::SColor outer(220, 0, 0, 0);
	static const video::SColor inner(70, 255, 255, 255);
	core::rect<s32> r = rect;
	r.UpperLeftCorner -= v2s32(1, 1);
	r.LowerRightCorner += v2s32(1, 1);
	drawOutline(driver, r, outer);
	drawOutline(driver, rect, inner);
}

void Minimap::drawPlayerArrow(const core::rect<s32> &rect, f32 angle)
{
	const s32 panel = rect.getWidth();
	const f32 s = std::max(6.0f, std::round(panel * 0.045f));
	const f32 cx = rect.UpperLeftCorner.X + panel / 2.0f;
	const f32 cy = rect.UpperLeftCorner.Y + panel / 2.0f;
	// Стрелка: остриё, левое крыло, вырез, правое крыло. Yaw растёт против
	// часовой (от +Z к -X), на экране с осью Y вниз — тоже против часовой.
	const f32 sa = std::sin(angle * core::DEGTORAD);
	const f32 ca = std::cos(angle * core::DEGTORAD);
	static const f32 shape[4][2] = {{0, -1}, {-0.62f, 0.62f}, {0, 0.22f}, {0.62f, 0.62f}};
	// Обход по часовой на экране, как у guiInventoryList: отсечение задних
	// граней в 2D-режиме остаётся от последнего 3D-материала.
	static const u16 indices[6] = {0, 2, 1, 0, 3, 2};

	auto draw = [&](f32 scale, video::SColor color) {
		video::S3DVertex v[4];
		for (int i = 0; i < 4; i++) {
			f32 x = shape[i][0] * s * scale, y = shape[i][1] * s * scale;
			f32 rx = x * ca + y * sa, ry = -x * sa + y * ca;
			v[i] = video::S3DVertex(cx + rx, cy + ry, 0, 0, 0, 1, color, 0, 0);
		}
		driver->draw2DVertexPrimitiveList(v, 4, indices, 2,
			video::EVT_STANDARD, scene::EPT_TRIANGLES, video::EIT_16BIT);
	};

	video::SMaterial mat = driver->getMaterial2D();
	mat.MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
	driver->setMaterial(mat);
	draw(1.0f + 2.5f / s, video::SColor(230, 0, 0, 0));
	draw(1.0f, video::SColor(255, 255, 255, 255));
}

void Minimap::drawMarkers(const core::rect<s32> &rect, const View &view)
{
	if (m_markers.empty())
		return;

	const s32 panel = rect.getWidth();
	const f32 k = (f32)view.px_per_texel / view.nodes_per_texel;
	const bool round = data->minimap_shape_round;
	const f32 sa = std::sin(-m_angle * core::DEGTORAD);
	const f32 ca = std::cos(-m_angle * core::DEGTORAD);
	const s32 size = std::max(5, (s32)std::round(panel * 0.035f));
	const s32 half = size / 2;
	const f32 limit = panel / 2.0f;
	const s16 dy_limit = data->mode.scan_height / 2;
	v3f cam_offset = intToFloat(client->getCamera()->getOffset(), BS);

	for (auto &&marker : m_markers) {
		v3f p = (marker->parent_node->getAbsolutePosition() + cam_offset) / BS;
		if (std::abs(p.Y - m_player_pos.Y) > dy_limit)
			continue;
		f32 dx = (p.X - m_player_pos.X) * k;
		f32 dy = -(p.Z - m_player_pos.Z) * k;
		if (round) {
			f32 rx = dx * ca + dy * sa, ry = -dx * sa + dy * ca;
			dx = rx; dy = ry;
			if (dx * dx + dy * dy > limit * limit)
				continue;
		} else if (std::abs(dx) > limit || std::abs(dy) > limit) {
			continue;
		}
		s32 px = rect.UpperLeftCorner.X + (s32)std::round(panel / 2.0f + dx);
		s32 py = rect.UpperLeftCorner.Y + (s32)std::round(panel / 2.0f + dy);
		video::ITexture *tex = marker->texture.empty()
			? data->marker_default : m_tsrc->getTexture(marker->texture);
		if (!tex)
			continue;
		core::dimension2di imgsize(tex->getOriginalSize());
		core::rect<s32> src(0, 0, imgsize.Width, imgsize.Height);
		core::rect<s32> dst(px - half, py - half, px - half + size, py - half + size);
		const video::SColor c[4] = {marker->color, marker->color, marker->color, marker->color};
		driver->draw2DImage(tex, dst, src, &rect, c, true);
	}
}

MinimapMarker *Minimap::addMarker(scene::ISceneNode *parent_node)
{
	auto m = std::make_unique<MinimapMarker>(parent_node);
	auto ret = m.get();
	m_markers.push_back(std::move(m));
	return ret;
}

void Minimap::removeMarker(MinimapMarker **m)
{
	MinimapMarker *ptr = *m;
	*m = nullptr;

	auto it = std::find_if(m_markers.begin(), m_markers.end(), [&] (const auto &it) {
		return it.get() == ptr;
	});
	assert(it != m_markers.end());
	m_markers.erase(it);
}

////
//// MinimapMapblock
////

// Ноды, которые карта считает прозрачными: украшения над землёй — то, сквозь
// что ходят и поверх чего ставят. Столбец показывает то, что под ними, иначе
// поле травы рисуется цветом пучков, а луг — крапом камушков.
static bool isMinimapTransparent(const ContentFeatures &f)
{
	if (f.drawtype == NDT_AIRLIKE)
		return true;
	if (f.isLiquid())
		return false;
	return !f.walkable && f.buildable_to;
}

void MinimapMapblock::getMinimapNodes(VoxelManipulator *vmanip, const NodeDefManager *nodedef, const v3s16 &pos)
{
	for (s16 x = 0; x < MAP_BLOCKSIZE; x++)
	for (s16 z = 0; z < MAP_BLOCKSIZE; z++) {
		s16 air_count = 0;
		bool surface_found = false;
		bool in_liquid = false;
		MinimapPixel *mmpixel = &data[z * MAP_BLOCKSIZE + x];
		mmpixel->liquid_depth = 0;

		for (s16 y = MAP_BLOCKSIZE -1; y >= 0; y--) {
			v3s16 p(x, y, z);
			MapNode n = vmanip->getNodeNoEx(pos + p);
			const ContentFeatures &f = nodedef->get(n);
			if (f.drawtype == NDT_AIRLIKE)
				air_count++;
			if (surface_found) {
				if (!in_liquid)
					continue;
				if (f.isLiquid() && mmpixel->liquid_depth < 255)
					mmpixel->liquid_depth++;
				else
					in_liquid = false;
				continue;
			}
			if (isMinimapTransparent(f))
				continue;
			mmpixel->height = y;
			mmpixel->n = n;
			surface_found = true;
			in_liquid = f.isLiquid();
			if (in_liquid)
				mmpixel->liquid_depth = 1;
		}

		if (!surface_found)
			mmpixel->n = MapNode(CONTENT_AIR);

		mmpixel->air_count = air_count;
	}
}
