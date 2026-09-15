// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2015 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include "irrlichttypes.h"
#include "irr_ptr.h"
#include "rect.h"
#include "CMeshBuffer.h"

#include "constants.h"
#include "hud_element.h"
#include "mapnode.h"
#include "util/thread.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace video {
	class IVideoDriver;
	class IImage;
	class ITexture;
}

namespace scene {
	class ISceneNode;
}

class Client;
class NodeDefManager;
class ITextureSource;
class VoxelManipulator;

// Предел стороны области сканирования в нодах. Ограничивает и размер
// текстуры, и работу потока: на панели в четверть экрана больше нод не
// поместится ни при каком масштабе.
#define MINIMAP_MAX_SIZE 1024

enum MinimapShape {
	MINIMAP_SHAPE_SQUARE,
	MINIMAP_SHAPE_ROUND,
};

// Форма, назначенная режиму сервером: FREE — как у клиента в настройках,
// остальные фиксируют форму и отключают её переключение с клавиатуры.
enum MinimapShapeLock : u8 {
	MINIMAP_SHAPE_FREE = 0,
	MINIMAP_SHAPE_LOCK_SQUARE = 1,
	MINIMAP_SHAPE_LOCK_ROUND = 2,
};

struct MinimapModeDef {
	MinimapType type = MINIMAP_TYPE_OFF;
	std::string label;
	u16 scan_height = 0;
	u16 map_size = 0;
	std::string texture;
	u16 scale = 1;
	MinimapShapeLock shape = MINIMAP_SHAPE_FREE;
};

struct MinimapMarker {
	MinimapMarker(scene::ISceneNode *parent_node):
		parent_node(parent_node)
	{}

	scene::ISceneNode *parent_node;
	std::string texture;
	video::SColor color = video::SColor(255, 255, 255, 255);
};

// Верхняя нода столбца внутри одного мапблока.
struct MinimapPixel {
	MapNode n;
	u8 height;
	u8 air_count;
	// Сколько нод жидкости подряд под поверхностью: чем глубже, тем темнее вода.
	u8 liquid_depth;
};

struct MinimapMapblock {
	void getMinimapNodes(VoxelManipulator *vmanip, const NodeDefManager *nodedef, const v3s16 &pos);

	MinimapPixel data[MAP_BLOCKSIZE * MAP_BLOCKSIZE];
};

// Столбец поверхности, присланный сервером заранее. Показывается там, где
// клиент ещё не получил мапблоки; живые данные всегда его перекрывают.
// n == CONTENT_IGNORE — столбец неизвестен; присланный таким, он стирает
// то, что было прислано раньше.
struct MinimapSurfaceColumn {
	content_t n = CONTENT_IGNORE;
	s16 y = 0;
	// Входит в присланный прямоугольник: остальные столбцы плитки при
	// слиянии не трогаются.
	bool set = false;
};

struct MinimapSurfaceTile {
	MinimapSurfaceColumn data[MAP_BLOCKSIZE * MAP_BLOCKSIZE];
};

// Итог сканирования одного столбца области вокруг игрока.
struct MinimapColumn {
	MapNode n = MapNode(CONTENT_AIR);
	s16 y = 0;
	u8 air_count = 0;
	u8 liquid_depth = 0;
	bool known = false;
};

// Готовый скан: сторона size нод, начало min (x, z), высоты абсолютные.
struct MinimapScan {
	v3s16 min;
	u16 size = 0;
	u32 generation = 0;
	std::vector<MinimapColumn> columns;
};

struct MinimapData {
	MinimapModeDef mode;
	v3s16 pos;
	// Сколько нод сканировать по стороне; считает главный поток из размера
	// панели и масштаба режима.
	u16 scan_size = 0;
	// Растёт при смене режима и размера: скан старого поколения, догнавший
	// главный поток, отбрасывается, а не рисуется под новым режимом.
	u32 generation = 0;
	bool minimap_shape_round;
	video::ITexture *texture = nullptr;
	bool textures_initialised = false;
	video::ITexture *minimap_overlay_round = nullptr;
	video::ITexture *marker_default = nullptr;
};

struct QueuedMinimapUpdate {
	v3s16 pos;
	MinimapMapblock *data = nullptr;
};

class MinimapUpdateThread : public UpdateThread {
public:
	MinimapUpdateThread() : UpdateThread("Minimap") {}
	virtual ~MinimapUpdateThread();

	void enqueueBlock(v3s16 pos, MinimapMapblock *data);
	void enqueueSurface(std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> &&tiles,
			bool replace);
	// Требует пересканировать область: позиция, режим или данные изменились.
	void invalidate();

	// Забрать готовый скан, если он появился после прошлого вызова.
	bool takeScan(MinimapScan &out);

	MinimapData *data = nullptr;
	std::mutex *data_mutex = nullptr;

protected:
	virtual void doUpdate();

private:
	bool pushBlockUpdate(v3s16 pos, MinimapMapblock *data);
	bool popBlockUpdate(QueuedMinimapUpdate *update);
	void getMap(v3s16 pos, s16 size, s16 height, MinimapScan &scan);

	std::mutex m_queue_mutex;
	std::deque<QueuedMinimapUpdate> m_update_queue;
	std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> m_surface_queue;
	bool m_surface_replace = false;
	std::map<v3s16, MinimapMapblock *> m_blocks_cache;
	size_t m_evict_at = 4096;
	std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> m_surface;
	std::atomic<bool> m_dirty{true};

	std::mutex m_scan_mutex;
	MinimapScan m_ready;
	MinimapScan m_spare;
	bool m_ready_fresh = false;
};

class Minimap {
public:
	Minimap(Client *client);
	~Minimap();

	void addBlock(v3s16 pos, MinimapMapblock *data);
	// Поверхность от сервера: столбцы прямоугольника [min, max] по x и z,
	// content и y по строкам z, внутри строки — по x. replace = true сначала
	// выбрасывает всё, что было прислано раньше.
	void addSurface(v2s16 min, v2s16 max, const std::vector<content_t> &content,
			const std::vector<s16> &height, bool replace);
	void clearSurface();

	// Положение игрока в нодах с дробной частью: карта скользит плавно, а не
	// прыгает на ноду.
	void setPlayerPos(v3f pos);
	void setPos(v3s16 pos);
	v3s16 getPos() const { return data->pos; }
	void setAngle(f32 angle);
	f32 getAngle() const { return m_angle; }
	void toggleMinimapShape();
	void setMinimapShape(MinimapShape shape);
	MinimapShape getMinimapShape();
	// Форма зафиксирована сервером для текущего режима.
	bool isShapeLocked() const;

	void clearModes() { m_modes.clear(); };
	void addMode(MinimapModeDef mode);
	void addMode(MinimapType type, u16 size = 0, const std::string &label = "",
			const std::string &texture = "", u16 scale = 1,
			MinimapShapeLock shape = MINIMAP_SHAPE_FREE);

	void setModeIndex(size_t index);
	size_t getModeIndex() const { return m_current_mode_index; };
	size_t getMaxModeIndex() const { return m_modes.size() - 1; };
	void nextMode();

	MinimapModeDef getModeDef() const { return data->mode; }

	MinimapMarker* addMarker(scene::ISceneNode *parent_node);
	void removeMarker(MinimapMarker **marker);

	void drawMinimap(core::rect<s32> rect);

	video::IVideoDriver *driver = nullptr;
	Client *client = nullptr;
	std::unique_ptr<MinimapData> data;

private:
	// Масштаб показа, выведенный из размера панели и режима.
	struct View {
		s32 panel = 0;         // сторона панели в пикселях
		u16 px_per_texel = 1;  // целое увеличение текселя
		u16 nodes_per_texel = 1; // сколько нод усредняется в тексель
		u16 texels = 0;        // сторона текстуры
		u16 scan_size = 0;     // сторона скана в нодах (с полем под затенение)
	};
	View computeView(s32 panel) const;
	void rebuildTexture(const MinimapScan &scan, const View &view);
	video::SColor columnColor(const MinimapColumn &c) const;
	void drawFrame(const core::rect<s32> &rect);
	void drawPlayerArrow(const core::rect<s32> &rect, f32 angle);
	void drawMarkers(const core::rect<s32> &rect, const View &view);
	void drawTextureMode(const core::rect<s32> &rect);

	ITextureSource *m_tsrc = nullptr;
	const NodeDefManager *m_ndef = nullptr;
	std::unique_ptr<MinimapUpdateThread> m_minimap_update_thread;
	irr_ptr<scene::SMeshBuffer> m_meshbuffer;
	std::vector<MinimapModeDef> m_modes;
	size_t m_current_mode_index;
	u16 m_surface_mode_scan_height;
	f32 m_angle;
	v3f m_player_pos;

	MinimapScan m_scan;
	bool m_scan_valid = false;
	View m_texture_view;

	std::mutex m_mutex;
	std::vector<std::unique_ptr<MinimapMarker>> m_markers;
};
