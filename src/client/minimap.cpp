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
#include "porting.h"
#include "client/fontengine.h"
#include <IGUIFont.h>
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

bool MinimapUpdateThread::takeScan(MinimapScanKind kind, MinimapScan &out)
{
	MutexAutoLock lock(m_scan_mutex);
	if (!m_ready_fresh[kind])
		return false;
	std::swap(out, m_ready[kind]);
	m_ready_fresh[kind] = false;
	return true;
}

void MinimapUpdateThread::scan(MinimapScanKind kind, const MinimapScanRequest &req)
{
	MinimapScan scan = std::move(m_spare[kind]);
	getMap(req, scan);
	scan.generation = req.generation;

	{
		MutexAutoLock lock(m_scan_mutex);
		std::swap(m_ready[kind], scan);
		m_ready_fresh[kind] = true;
	}
	m_spare[kind] = std::move(scan);
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
	const bool dirty = m_dirty.exchange(false);
	if (dirty)
		m_big_pending = true;

	v3s16 pos;
	MinimapScanRequest mini, big;
	MinimapType type;
	{
		MutexAutoLock lock(*data_mutex);
		pos = data->pos;
		mini = data->requests[MINIMAP_SCAN_MINI];
		big = data->requests[MINIMAP_SCAN_BIG];
		type = data->mode.type;
	}

	if (type != MINIMAP_TYPE_RADAR && type != MINIMAP_TYPE_SURFACE)
		return;

	// Кэш мапблоков растёт с каждым замешенным блоком; далёкие от игрока в
	// скан не попадут, а память держат. Чистится по порогу, не на каждом
	// обходе: обход всего кэша дороже одного скана.
	if (dirty && m_blocks_cache.size() > m_evict_at) {
		const v3s16 center = getNodeBlockPos(pos);
		const s16 reach = MINIMAP_MAX_SIZE / MAP_BLOCKSIZE + 1;
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

	if (dirty && mini.active)
		scan(MINIMAP_SCAN_MINI, mini);

	if (!big.active) {
		// Закрытая карта не держит свои буферы: это десятки мегабайт.
		MutexAutoLock lock(m_scan_mutex);
		m_ready[MINIMAP_SCAN_BIG] = MinimapScan();
		m_spare[MINIMAP_SCAN_BIG] = MinimapScan();
	} else if (m_big_pending) {
		const u64 now = porting::getTimeMs();
		if (now - m_big_scanned_at >= 1000) {
			scan(MINIMAP_SCAN_BIG, big);
			m_big_scanned_at = now;
			m_big_pending = false;
		}
	}
}

void MinimapUpdateThread::getMap(const MinimapScanRequest &req, MinimapScan &scan)
{
	const s16 size_x = req.size_x, size_z = req.size_z;
	v3s16 pos_min(req.min.X, req.min.Y - req.height / 2, req.min.Z);
	v3s16 pos_max(pos_min.X + size_x - 1, req.min.Y + req.height / 2, pos_min.Z + size_z - 1);
	v3s16 blockpos_min = getNodeBlockPos(pos_min);
	v3s16 blockpos_max = getNodeBlockPos(pos_max);

	scan.min = pos_min;
	scan.size_x = size_x;
	scan.size_z = size_z;
	scan.columns.assign((size_t)size_x * size_z, MinimapColumn());

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
				MinimapColumn &out = scan.columns[inmap_pos.X + inmap_pos.Y * size_x];

				out.air_count = std::min<int>(255, out.air_count + in_pixel.air_count);
				if (out.known() || in_pixel.n.param0 == CONTENT_AIR)
					continue;
				out.n = in_pixel.n;
				out.y = block_node_min.Y + in_pixel.height;
				out.liquid_depth = in_pixel.liquid_depth;
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
			MinimapColumn &out = scan.columns[(x - pos_min.X) + (z - pos_min.Z) * size_x];
			if (out.known())
				continue;
			const MinimapSurfaceColumn &in = tile.data[(z - tile_node_min.Y) * MAP_BLOCKSIZE
					+ (x - tile_node_min.X)];
			if (in.n == CONTENT_IGNORE)
				continue;
			out.n = MapNode(in.n);
			out.y = in.y;
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
		for (auto &req : data->requests) {
			req.active = false;
			req.generation++;
		}
	}

	m_scan_valid = false;
	m_big_valid = false;
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
	u16 ppt = 1;
	if (scale >= 1.0f)
		ppt = std::max(1, (int)std::floor(scale));
	else
		v.nodes_per_texel = std::max(1, (int)std::ceil(1.0f / scale));
	v.px_per_texel = ppt;
	// Тексель запаса с каждой стороны: дробный сдвиг под положение игрока
	// никогда не откроет край текстуры.
	u32 texels = (panel + ppt - 1) / ppt + 2;
	// Нода запаса с каждой стороны — соседи для затенения крайних текселей.
	texels = std::min<u32>(texels, MINIMAP_MAX_SIZE);
	v.texels_x = v.texels_z = texels;
	v.scan_x = v.scan_z = texels * v.nodes_per_texel + 2;
	return v;
}

Minimap::View Minimap::computeBigView(u16 nodes_x, u16 nodes_z, s32 avail_w, s32 avail_h) const
{
	View v;
	const u16 largest = std::max(nodes_x, nodes_z);
	v.nodes_per_texel = (largest + MINIMAP_MAX_SIZE - 1) / MINIMAP_MAX_SIZE;
	v.texels_x = (nodes_x + v.nodes_per_texel - 1) / v.nodes_per_texel;
	v.texels_z = (nodes_z + v.nodes_per_texel - 1) / v.nodes_per_texel;
	v.scan_x = v.texels_x * v.nodes_per_texel + 2;
	v.scan_z = v.texels_z * v.nodes_per_texel + 2;
	f32 scale = std::min((f32)avail_w / v.texels_x, (f32)avail_h / v.texels_z);
	v.px_per_texel = scale >= 1.0f ? std::floor(scale) : scale;
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

void Minimap::rebuildTexture(const MinimapScan &scan, const View &view,
		video::ITexture *&texture, bool round, bool mipmaps)
{
	const u16 TX = view.texels_x, TZ = view.texels_z;
	const u16 npp = view.nodes_per_texel;
	const s16 SX = scan.size_x, SZ = scan.size_z;
	const bool radar = data->mode.type == MINIMAP_TYPE_RADAR;

	static const video::SColor unknown(255, 10, 11, 13);
	// Свет с северо-запада: склон к нему светлее, от него темнее. Пять
	// ступеней по сумме знаков перепада к северному и западному соседу.
	static const float relief[5] = {0.66f, 0.82f, 1.0f, 1.12f, 1.25f};

	auto column = [&](s32 x, s32 z) -> const MinimapColumn & {
		return scan.columns[x + z * SX];
	};

	auto nodeColor = [&](s32 x, s32 z, bool &known) -> video::SColor {
		const MinimapColumn &c = column(x, z);
		known = c.known();
		if (radar) {
			int g = c.air_count > 0
				? core::clamp(core::round32(32 + c.air_count * 8), 0, 255) : 0;
			return video::SColor(255, 0, g, 0);
		}
		if (!c.known())
			return unknown;
		video::SColor base = columnColor(c);
		int slope = 2;
		const MinimapColumn &north = column(x, z + 1);
		const MinimapColumn &west = column(x - 1, z);
		if (north.known())
			slope += (c.y > north.y) - (c.y < north.y);
		if (west.known())
			slope += (c.y > west.y) - (c.y < west.y);
		float k = relief[slope];
		if (c.liquid_depth > 0)
			k *= std::max(0.55f, 1.0f - c.liquid_depth * 0.045f);
		return scaleColor(base, k);
	};

	core::dimension2d<u32> dim(TX, TZ);
	video::IImage *image = driver->createImage(video::ECF_A8R8G8B8, dim);
	u32 *pixels = static_cast<u32 *>(image->getData());
	const u32 pitch = image->getPitch() / sizeof(u32);

	// Центр круга — тексель игрока; радиус — половина панели.
	const float cx = (float)(SX / 2 - 1) / npp + 0.5f;
	const float radius = (float)view.panel / (2.0f * view.px_per_texel) + 0.5f;

	for (u16 ty = 0; ty < TZ; ty++)
	for (u16 tx = 0; tx < TX; tx++) {
		s32 x0 = 1 + tx * npp;
		s32 z0 = 1 + (TZ - 1 - ty) * npp;
		video::SColor col;
		if (npp == 1) {
			bool known;
			col = nodeColor(x0, z0, known);
		} else {
			u32 r = 0, g = 0, b = 0, n = 0;
			for (s32 dz = 0; dz < npp; dz++)
			for (s32 dx = 0; dx < npp; dx++) {
				s32 x = x0 + dx, z = z0 + dz;
				if (x + 1 >= SX || z + 1 >= SZ)
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
			float dx = tx + 0.5f - cx, dy = (TZ - 1 - ty) + 0.5f - cx;
			if (dx * dx + dy * dy > radius * radius)
				col = video::SColor(0, 0, 0, 0);
		}
		pixels[tx + ty * pitch] = col.color;
	}

	if (texture)
		driver->removeTexture(texture);
	bool had_mipmaps = driver->getTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, mipmaps);
	texture = driver->addTexture(round || !mipmaps ? "minimap__" : "minimap_big__", image);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, had_mipmaps);
	image->drop();
}

// Квадрат с текстурой в пикселях области place.rect; повёрнут вокруг центра
// области, если карта круглая.
void Minimap::drawMapQuad(const Placement &place, video::ITexture *texture,
		f32 quad_w, f32 quad_h, f32 left, f32 top, bool nearest)
{
	const s32 w = place.rect.getWidth(), h = place.rect.getHeight();
	auto &v = m_meshbuffer->Vertices->Data;
	static const video::SColor white(255, 255, 255, 255);
	const f32 s = std::sin(place.angle * core::DEGTORAD);
	const f32 c = std::cos(place.angle * core::DEGTORAD);
	auto put = [&](int i, f32 px, f32 py, f32 u, f32 tv) {
		f32 dx = px - w / 2.0f, dy = py - h / 2.0f;
		f32 rx = dx * c + dy * s, ry = -dx * s + dy * c;
		f32 x = (rx + w / 2.0f) / w * 2.0f - 1.0f;
		f32 y = 1.0f - (ry + h / 2.0f) / h * 2.0f;
		v[i] = video::S3DVertex(x, y, 0, 0, 0, 1, white, u, tv);
	};
	put(0, left, top + quad_h, 0, 1);
	put(1, left, top, 0, 0);
	put(2, left + quad_w, top, 1, 0);
	put(3, left + quad_w, top + quad_h, 1, 1);
	m_meshbuffer->setDirty(scene::EBT_VERTEX);

	core::rect<s32> oldViewPort = driver->getViewPort();
	core::matrix4 oldProjMat = driver->getTransform(video::ETS_PROJECTION);
	core::matrix4 oldViewMat = driver->getTransform(video::ETS_VIEW);

	driver->setViewPort(place.rect);
	driver->setTransform(video::ETS_PROJECTION, core::matrix4());
	driver->setTransform(video::ETS_VIEW, core::matrix4());
	driver->setTransform(video::ETS_WORLD, core::matrix4());

	video::SMaterial &material = m_meshbuffer->getMaterial();
	material.forEachTexture([&] (auto &tex) {
		tex.MinFilter = nearest ? video::ETMINF_NEAREST_MIPMAP_NEAREST
			: video::ETMINF_LINEAR_MIPMAP_LINEAR;
		tex.MagFilter = nearest ? video::ETMAGF_NEAREST : video::ETMAGF_LINEAR;
		tex.TextureWrapU = video::ETC_CLAMP_TO_EDGE;
		tex.TextureWrapV = video::ETC_CLAMP_TO_EDGE;
	});
	material.TextureLayers[0].Texture = texture;
	material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
	material.ZWriteEnable = video::EZW_OFF;
	material.ZBuffer = video::ECFN_DISABLED;
	material.BackfaceCulling = false;
	driver->setMaterial(material);
	driver->drawMeshBuffer(m_meshbuffer.get());

	driver->setTransform(video::ETS_VIEW, oldViewMat);
	driver->setTransform(video::ETS_PROJECTION, oldProjMat);
	driver->setViewPort(oldViewPort);
}

bool Minimap::toScreen(const Placement &place, v3f pos, v2f &out) const
{
	const f32 w = place.rect.getWidth(), h = place.rect.getHeight();
	f32 dx = (pos.X - place.node_left) * place.k;
	f32 dy = (place.node_top - pos.Z) * place.k;
	if (place.round) {
		// Круг вращается вокруг игрока, он же — центр панели.
		dx -= w / 2.0f;
		dy -= h / 2.0f;
		const f32 sa = std::sin(-place.angle * core::DEGTORAD);
		const f32 ca = std::cos(-place.angle * core::DEGTORAD);
		f32 rx = dx * ca + dy * sa, ry = -dx * sa + dy * ca;
		dx = rx + w / 2.0f;
		dy = ry + h / 2.0f;
	}
	out = v2f(place.rect.UpperLeftCorner.X + dx, place.rect.UpperLeftCorner.Y + dy);
	return dx >= 0 && dx <= w && dy >= 0 && dy <= h
		&& (!place.round || (dx - w / 2) * (dx - w / 2) + (dy - h / 2) * (dy - h / 2)
			<= (w / 2) * (w / 2));
}

void Minimap::drawMinimap(core::rect<s32> rect, const std::vector<MinimapMapMarker> &markers)
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
			MinimapScanRequest &req = data->requests[MINIMAP_SCAN_MINI];
			v3s16 min(data->pos.X - view.scan_x / 2, data->pos.Y, data->pos.Z - view.scan_z / 2);
			if (!req.active || req.size_x != view.scan_x || req.size_z != view.scan_z) {
				req.generation++;
				m_minimap_update_thread->invalidate();
			}
			if (req.min != min || req.height != data->mode.scan_height)
				m_minimap_update_thread->invalidate();
			req.active = true;
			req.min = min;
			req.size_x = view.scan_x;
			req.size_z = view.scan_z;
			req.height = data->mode.scan_height;
			generation = req.generation;
		}
		if (m_minimap_update_thread->takeScan(MINIMAP_SCAN_MINI, m_scan)) {
			m_scan_valid = m_scan.generation == generation
				&& m_scan.size_x == view.scan_x && m_scan.size_z == view.scan_z;
			if (m_scan_valid) {
				rebuildTexture(m_scan, view, data->texture, round, false);
				m_texture_view = view;
			}
		}

		Placement place;
		place.rect = rect;
		place.round = round;
		place.angle = round ? m_angle : 0.0f;

		if (m_scan_valid && data->texture) {
			const View &tv = m_texture_view;
			const f32 k = tv.px_per_texel / tv.nodes_per_texel;
			// Левый край текселя 0 и верхний край строки 0 в нодах: нода с
			// целой координатой n занимает [n - 0.5, n + 0.5).
			const f32 left_node = m_scan.min.X + 1 - 0.5f;
			const f32 top_node = m_scan.min.Z + 1 + (f32)tv.texels_z * tv.nodes_per_texel - 0.5f;
			f32 left = panel / 2.0f + (left_node - m_player_pos.X) * k;
			f32 top = panel / 2.0f - (top_node - m_player_pos.Z) * k;
			if (!round) {
				left = std::round(left);
				top = std::round(top);
			}
			place.k = k;
			place.node_left = left_node - left / k;
			place.node_top = top_node + top / k;
			drawMapQuad(place, data->texture, tv.texels_x * tv.px_per_texel,
				tv.texels_z * tv.px_per_texel, left, top, !round);

			if (round) {
				Placement flat = place;
				flat.angle = 0;
				drawMapQuad(flat, data->minimap_overlay_round, panel, panel, 0, 0, false);
			}

			drawMarkers(place, markers, true, false);
		}
	}

	if (!round)
		drawFrame(rect);
	drawPlayerArrow(v2f(rect.UpperLeftCorner.X + panel / 2.0f,
		rect.UpperLeftCorner.Y + panel / 2.0f),
		std::max(6.0f, std::round(panel * 0.045f)), round ? 0.0f : m_angle);
}

void Minimap::setArea(bool set, v2s16 min, v2s16 max)
{
	m_area_set = set && max.X >= min.X && max.Y >= min.Y;
	m_area_min = min;
	m_area_max = max;
	m_big_valid = false;
}

void Minimap::toggleBigMap()
{
	m_big_open = !m_big_open;
	if (m_big_open)
		return;
	{
		MutexAutoLock lock(m_mutex);
		data->requests[MINIMAP_SCAN_BIG].active = false;
	}
	m_big_valid = false;
	m_big_scan = MinimapScan();
	if (data->big_texture) {
		driver->removeTexture(data->big_texture);
		data->big_texture = nullptr;
	}
	m_minimap_update_thread->deferUpdate();
}

void Minimap::drawBigMap(const core::rect<s32> &screen,
		const std::vector<MinimapMapMarker> &markers)
{
	if (!m_big_open || data->mode.type == MINIMAP_TYPE_OFF
			|| data->mode.type == MINIMAP_TYPE_TEXTURE)
		return;

	if (!data->textures_initialised) {
		data->minimap_overlay_round = m_tsrc->getTexture("minimap_overlay_round.png");
		data->marker_default = m_tsrc->getTexture("minimap_marker.png");
		data->textures_initialised = true;
	}

	// Область: от сервера или полкилометра вокруг игрока.
	v2s16 amin, amax;
	if (m_area_set) {
		amin = m_area_min;
		amax = m_area_max;
	} else {
		amin = v2s16(data->pos.X - 256, data->pos.Z - 256);
		amax = v2s16(data->pos.X + 255, data->pos.Z + 255);
	}
	const s32 margin = std::max(16, screen.getHeight() / 24);
	const s32 avail_w = screen.getWidth() - 2 * margin;
	const s32 avail_h = screen.getHeight() - 2 * margin;
	if (avail_w <= 0 || avail_h <= 0)
		return;

	View view = computeBigView(amax.X - amin.X + 1, amax.Y - amin.Y + 1, avail_w, avail_h);
	u32 generation;
	{
		MutexAutoLock lock(m_mutex);
		MinimapScanRequest &req = data->requests[MINIMAP_SCAN_BIG];
		v3s16 min(amin.X - 1, data->pos.Y, amin.Y - 1);
		if (!req.active || req.size_x != view.scan_x || req.size_z != view.scan_z
				|| req.min != min) {
			req.generation++;
			m_minimap_update_thread->invalidate();
		}
		req.active = true;
		req.min = min;
		req.size_x = view.scan_x;
		req.size_z = view.scan_z;
		req.height = data->mode.scan_height;
		generation = req.generation;
	}
	// Поток сам не просыпается по таймеру: пока карта открыта, его будит
	// каждый кадр, а он уже решает, пора ли пересчитывать.
	m_minimap_update_thread->deferUpdate();

	if (m_minimap_update_thread->takeScan(MINIMAP_SCAN_BIG, m_big_scan)) {
		m_big_valid = m_big_scan.generation == generation
			&& m_big_scan.size_x == view.scan_x && m_big_scan.size_z == view.scan_z;
		if (m_big_valid) {
			rebuildTexture(m_big_scan, view, data->big_texture, false, true);
			m_big_view = view;
		}
	}

	driver->draw2DRectangle(video::SColor(150, 0, 0, 0), screen);

	if (!m_big_valid || !data->big_texture)
		return;

	const View &tv = m_big_view;
	const s32 w = (s32)std::round(tv.texels_x * tv.px_per_texel);
	const s32 h = (s32)std::round(tv.texels_z * tv.px_per_texel);
	Placement place;
	place.rect = core::rect<s32>(0, 0, w, h);
	place.rect += v2s32(screen.UpperLeftCorner.X + margin + (avail_w - w) / 2,
		screen.UpperLeftCorner.Y + margin + (avail_h - h) / 2);
	place.k = tv.px_per_texel / tv.nodes_per_texel;
	place.node_left = m_big_scan.min.X + 1 - 0.5f;
	place.node_top = m_big_scan.min.Z + 1 + (f32)tv.texels_z * tv.nodes_per_texel - 0.5f;

	drawMapQuad(place, data->big_texture, w, h, 0, 0, tv.px_per_texel >= 1.0f);
	drawFrame(place.rect);
	drawMarkers(place, markers, false, true);

	v2f me;
	if (toScreen(place, m_player_pos, me))
		drawPlayerArrow(me, std::max(8.0f, std::round(place.rect.getHeight() * 0.02f)), m_angle);
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
	material.BackfaceCulling = false;
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

void Minimap::drawPlayerArrow(v2f center, f32 s, f32 angle)
{
	const f32 cx = center.X, cy = center.Y;
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

// Подпись с тёмной обводкой: карта пёстрая, и без обводки текст тонет.
static void drawLabel(gui::IGUIFont *font, const std::wstring &text, v2s32 pos,
		video::SColor color, const core::rect<s32> &clip)
{
	core::dimension2du dim = font->getDimension(text.c_str());
	core::rect<s32> r(pos, core::dimension2di(dim.Width, dim.Height));
	static const video::SColor shadow(200, 0, 0, 0);
	static const v2s32 offsets[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	for (const v2s32 &o : offsets)
		font->draw(text.c_str(), r + o, shadow, false, false, &clip);
	font->draw(text.c_str(), r, color, false, false, &clip);
}

void Minimap::drawMarkers(const Placement &place,
		const std::vector<MinimapMapMarker> &markers, bool clamp_to_edge, bool labels)
{
	const core::rect<s32> &rect = place.rect;
	const s32 panel = std::min(rect.getWidth(), rect.getHeight());
	const s32 base = std::max(5, (s32)std::round(panel * (labels ? 0.02f : 0.035f)));
	const s16 dy_limit = data->mode.scan_height / 2;
	const v2f center(rect.UpperLeftCorner.X + rect.getWidth() / 2.0f,
		rect.UpperLeftCorner.Y + rect.getHeight() / 2.0f);
	gui::IGUIFont *font = labels ? g_fontengine->getFont() : nullptr;

	auto drawDot = [&](v2f at, const std::string &texture, video::SColor color, s32 size) {
		video::ITexture *tex = texture.empty()
			? data->marker_default : m_tsrc->getTexture(texture);
		if (!tex)
			return;
		const s32 half = size / 2;
		const s32 px = (s32)std::round(at.X), py = (s32)std::round(at.Y);
		core::dimension2di imgsize(tex->getOriginalSize());
		core::rect<s32> src(0, 0, imgsize.Width, imgsize.Height);
		core::rect<s32> dst(px - half, py - half, px - half + size, py - half + size);
		const video::SColor c[4] = {color, color, color, color};
		driver->draw2DImage(tex, dst, src, &rect, c, true);
	};

	// Что за краем панели, прижимается к нему: по точке у рамки видно, в
	// какую сторону бежать, — ради этого карта и нужна.
	auto clamp = [&](v2f at, s32 size) -> v2f {
		v2f d = at - center;
		const f32 inset = size / 2.0f + 2.0f;
		if (place.round) {
			const f32 r = panel / 2.0f - inset;
			const f32 len = d.getLength();
			if (len > r)
				d *= r / len;
		} else {
			const f32 rx = rect.getWidth() / 2.0f - inset;
			const f32 ry = rect.getHeight() / 2.0f - inset;
			const f32 f = std::max(std::abs(d.X) / rx, std::abs(d.Y) / ry);
			if (f > 1.0f)
				d /= f;
		}
		return center + d;
	};

	v3f cam_offset = intToFloat(client->getCamera()->getOffset(), BS);
	for (auto &&marker : m_markers) {
		v3f p = (marker->parent_node->getAbsolutePosition() + cam_offset) / BS;
		if (std::abs(p.Y - m_player_pos.Y) > dy_limit)
			continue;
		v2f at;
		if (!toScreen(place, p, at))
			continue;
		drawDot(at, marker->texture, marker->color, base);
	}

	for (const MinimapMapMarker &m : markers) {
		const s32 size = std::max(4, (s32)std::round(base * m.scale));
		v2f at;
		bool inside = toScreen(place, m.pos, at);
		if (!inside) {
			if (!clamp_to_edge)
				continue;
			at = clamp(at, size);
		}
		drawDot(at, m.texture, m.color, inside ? size : std::max(4, size * 3 / 4));
		if (labels && font && !m.label.empty()) {
			core::dimension2du dim = font->getDimension(m.label.c_str());
			// Подпись справа от отметки; у правого края — слева, иначе её
			// срезала бы рамка.
			s32 x = (s32)std::round(at.X) + size / 2 + 3;
			if (x + (s32)dim.Width > rect.LowerRightCorner.X)
				x = (s32)std::round(at.X) - size / 2 - 3 - (s32)dim.Width;
			drawLabel(font, m.label, v2s32(x, (s32)std::round(at.Y) - (s32)dim.Height / 2),
				m.color, rect);
		}
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
