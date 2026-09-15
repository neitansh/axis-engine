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

// Предел стороны текстуры карты в текселях; скан на две ноды шире — под
// соседей для затенения. Ограничивает и память, и работу потока: больше
// текселей не разглядеть ни на какой панели.
#define MINIMAP_MAX_SIZE 1280

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

// Итог сканирования одного столбца. Столбец, о котором ничего не известно,
// несёт CONTENT_IGNORE; восемь байт на столбец — большая карта держит их
// больше миллиона.
struct MinimapColumn {
	MapNode n = MapNode(CONTENT_IGNORE);
	s16 y = 0;
	u8 air_count = 0;
	u8 liquid_depth = 0;

	bool known() const { return n.param0 != CONTENT_IGNORE; }
};

// Что сканировать: прямоугольник столбцов от min (x, z) размером size_x на
// size_z нод, по высоте — height нод вокруг min.Y. Поколение растёт при смене
// режима и размера: скан старого поколения, догнавший главный поток,
// отбрасывается, а не рисуется под новым режимом.
struct MinimapScanRequest {
	bool active = false;
	v3s16 min;
	u16 size_x = 0;
	u16 size_z = 0;
	u16 height = 0;
	u32 generation = 0;
};

// Готовый скан: столбцы по строкам z, внутри строки — по x; высоты абсолютные.
struct MinimapScan {
	v3s16 min;
	u16 size_x = 0;
	u16 size_z = 0;
	u32 generation = 0;
	std::vector<MinimapColumn> columns;
};

enum MinimapScanKind {
	MINIMAP_SCAN_MINI = 0,
	MINIMAP_SCAN_BIG = 1,
};

// Отметка на карте от игры: элемент HUD map_marker.
struct MinimapMapMarker {
	v3f pos;
	std::wstring label;
	std::string texture;
	video::SColor color;
	f32 scale = 1.0f;
};

struct MinimapData {
	MinimapModeDef mode;
	v3s16 pos;
	MinimapScanRequest requests[2];
	bool minimap_shape_round;
	video::ITexture *texture = nullptr;
	video::ITexture *big_texture = nullptr;
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
	// Требует пересканировать области: позиция, режим или данные изменились.
	void invalidate();

	// Забрать готовый скан, если он появился после прошлого вызова.
	bool takeScan(MinimapScanKind kind, MinimapScan &out);

	MinimapData *data = nullptr;
	std::mutex *data_mutex = nullptr;

protected:
	virtual void doUpdate();

private:
	bool pushBlockUpdate(v3s16 pos, MinimapMapblock *data);
	bool popBlockUpdate(QueuedMinimapUpdate *update);
	void getMap(const MinimapScanRequest &req, MinimapScan &scan);
	void scan(MinimapScanKind kind, const MinimapScanRequest &req);

	std::mutex m_queue_mutex;
	std::deque<QueuedMinimapUpdate> m_update_queue;
	std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> m_surface_queue;
	bool m_surface_replace = false;
	std::map<v3s16, MinimapMapblock *> m_blocks_cache;
	size_t m_evict_at = 4096;
	std::map<v2s16, std::unique_ptr<MinimapSurfaceTile>> m_surface;
	std::atomic<bool> m_dirty{true};
	// Большая карта — до миллиона столбцов; пересчитывать её на каждый
	// пришедший мапблок незачем, раз в секунду глазу хватает.
	bool m_big_pending = true;
	u64 m_big_scanned_at = 0;

	std::mutex m_scan_mutex;
	MinimapScan m_ready[2];
	MinimapScan m_spare[2];
	bool m_ready_fresh[2] = {false, false};
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

	// Область большой карты от сервера; без неё — 512 нод вокруг игрока.
	// up — какая сторона света сверху: 0 север, 1 восток, 2 юг, 3 запад.
	void setArea(bool set, v2s16 min, v2s16 max, u8 up);
	void toggleBigMap();
	bool isBigMapOpen() const { return m_big_open; }

	void drawMinimap(core::rect<s32> rect, const std::vector<MinimapMapMarker> &markers);
	// Большая карта поверх всего экрана: рисуется после остального HUD.
	void drawBigMap(const core::rect<s32> &screen, const std::vector<MinimapMapMarker> &markers);

	video::IVideoDriver *driver = nullptr;
	Client *client = nullptr;
	std::unique_ptr<MinimapData> data;

private:
	// Масштаб показа: сколько нод в текселе и сколько пикселей на тексель.
	// У миникарты пиксели на тексель целые, у большой карты — какие влезут:
	// обзору всей арены дробное уменьшение с мип-уровнями не вредит.
	struct View {
		s32 panel = 0;           // сторона панели миникарты в пикселях
		f32 px_per_texel = 1;
		u16 nodes_per_texel = 1;
		u16 texels_x = 0;
		u16 texels_z = 0;
		u16 scan_x = 0;          // размер скана в нодах, с полем под затенение
		u16 scan_z = 0;
	};
	// Как положить скан на экран: пиксель = origin + (нода − node_origin) * k.
	// Текстура лежит квадратом quad_w на quad_h с углом (quad_left, quad_top)
	// внутри области rect, север вверх; вся область повёрнута на angle
	// (против часовой на экране) вокруг своего центра.
	struct Placement {
		core::rect<s32> rect;    // куда рисуется карта
		f32 quad_left = 0;
		f32 quad_top = 0;
		f32 node_left = 0;       // нода у левого края текстуры
		f32 node_top = 0;        // нода у верхнего края текстуры (+z)
		f32 k = 1;               // пикселей на ноду
		bool round = false;
		f32 angle = 0;
	};
	View computeView(s32 panel) const;
	View computeBigView(u16 nodes_x, u16 nodes_z, s32 avail_w, s32 avail_h) const;
	void rebuildTexture(const MinimapScan &scan, const View &view,
			video::ITexture *&texture, bool round, bool mipmaps);
	video::SColor columnColor(const MinimapColumn &c) const;
	void drawMapQuad(const Placement &place, video::ITexture *texture, f32 quad_w, f32 quad_h,
			bool nearest);
	void drawFrame(const core::rect<s32> &rect);
	s32 frameUnit() const;
	void drawPlayerArrow(v2f center, f32 size, f32 angle);
	void drawMarkers(const Placement &place, const std::vector<MinimapMapMarker> &markers,
			bool clamp_to_edge, bool labels);
	void drawTextureMode(const core::rect<s32> &rect);
	bool toScreen(const Placement &place, v3f pos, v2f &out) const;

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

	bool m_big_open = false;
	bool m_area_set = false;
	v2s16 m_area_min, m_area_max;
	u8 m_area_up = 0;
	MinimapScan m_big_scan;
	bool m_big_valid = false;
	View m_big_view;

	std::mutex m_mutex;
	std::vector<std::unique_ptr<MinimapMarker>> m_markers;
};
