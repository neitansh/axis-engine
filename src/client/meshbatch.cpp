// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "meshbatch.h"

#include "profiler.h"
#include "renderingengine.h"
#include "shadows/dynamicshadowsrender.h"

#include <ISceneManager.h>
#include <IVideoDriver.h>
#include <algorithm>
#include <cmath>

/*
 * Шестнадцать бит на индекс вершины — отсюда и предел буфера. Один пакет
 * держит столько экземпляров, сколько влезает вершин; дальше заводится
 * следующий, и россыпь стоит уже двух команд рисования вместо тысячи.
 */
static constexpr u32 MAX_VERTICES_PER_BATCH = 65536;

MeshBatch::MeshBatch(scene::ISceneManager *mgr, scene::ISceneNode *parent,
		const video::SMaterial &material, bool transparent,
		bool casts_shadow,
		const video::S3DVertex *vertices, u32 vertex_count,
		const u16 *indices, u32 index_count) :
	scene::ISceneNode(parent, mgr),
	m_transparent(transparent),
	m_mesh_buffer(make_irr<scene::SMeshBuffer>())
{
	m_template_vertices.assign(vertices, vertices + vertex_count);
	m_template_indices.assign(indices, indices + index_count);

	// Радиус образца: коробка экземпляра, годная при любом повороте. Считать
	// точную коробку повёрнутой модели незачем — мусор мелкий, и отсечение
	// по шару ошибётся разве что на сантиметры.
	for (const auto &v : m_template_vertices)
		m_template_radius = std::max(m_template_radius, v.Pos.getLength());

	m_capacity = vertex_count > 0
			? std::max<u32>(1, MAX_VERTICES_PER_BATCH / vertex_count) : 1;

	m_mesh_buffer->getMaterial() = material;
	// Вершины ложатся в мировых координатах и переписываются по одной, а не
	// целыми пачками: буфер должен жить в памяти, а не быть отлитым один раз.
	m_mesh_buffer->setHardwareMappingHint(scene::EHM_STREAM, scene::EBT_VERTEX);
	m_mesh_buffer->setHardwareMappingHint(scene::EHM_DYNAMIC, scene::EBT_INDEX);

	// В карту теней пакет пишется один раз за всю россыпь — там, где тысяча
	// отдельных сущностей стоила бы тысячи проходов.
	if (casts_shadow) {
		if (auto *shadow = RenderingEngine::get_shadow_renderer())
			shadow->addNodeToShadowList(this);
	}
}

MeshBatch::~MeshBatch()
{
	if (auto *shadow = RenderingEngine::get_shadow_renderer())
		shadow->removeNodeFromShadowList(this);
}

std::optional<u32> MeshBatch::allocate()
{
	u32 slot;

	if (!m_free_list.empty()) {
		slot = m_free_list.back();
		m_free_list.pop_back();
	} else {
		const u32 verts = verticesPerInstance();
		slot = m_live.size();
		if (slot >= m_capacity)
			return std::nullopt;

		// Место под экземпляр отводится сразу и навсегда: индексы считаются
		// умножением номера на размер образца, и сдвигать соседей нельзя.
		// Пока в него не написали, вершины схлопнуты в точку и не рисуются.
		auto &vbuf = m_mesh_buffer->Vertices->Data;
		vbuf.resize(vbuf.size() + verts, video::S3DVertex());

		m_live.push_back(false);
		m_centres.emplace_back(0, 0, 0);
	}

	m_live[slot] = true;
	m_used++;
	m_indices_dirty = true;
	m_box_dirty = true;
	return slot;
}

void MeshBatch::release(u32 slot)
{
	if (slot >= m_live.size() || !m_live[slot])
		return;

	m_live[slot] = false;
	m_used--;
	m_free_list.push_back(slot);
	m_indices_dirty = true;
	m_box_dirty = true;
}

void MeshBatch::write(u32 slot, const core::matrix4 &transform, video::SColor color)
{
	if (slot >= m_live.size() || !m_live[slot])
		return;

	const u32 verts = verticesPerInstance();
	auto &vbuf = m_mesh_buffer->Vertices->Data;

	/*
	 * Поворачивать нормали полной матрицей нельзя, если в ней есть размер:
	 * растянутая нормаль перестаёт быть единичной, и свет на модели плывёт.
	 * Поэтому направление берётся отдельной матрицей — только поворот.
	 */
	core::matrix4 rotation = transform;
	rotation.setTranslation(v3f(0, 0, 0));

	for (u32 i = 0; i < verts; i++) {
		const video::S3DVertex &src = m_template_vertices[i];
		video::S3DVertex &dst = vbuf[slot * verts + i];

		dst.Pos = src.Pos;
		transform.transformVect(dst.Pos);

		dst.Normal = rotation.transformVect(src.Normal);
		dst.Normal.normalize();

		dst.TCoords = src.TCoords;
		/*
		 * Вспомогательное поле вершины несёт номер слоя в массиве текстур:
		 * плитки нод давно лежат не по одной, а стопкой, и без этого числа
		 * кусок бетона одевается в первый попавшийся слой — на деле в
		 * «неизвестную ноду».
		 */
		dst.Aux = src.Aux;
		/*
		 * Свет ложится поверх собственного цвета вершины, а не вместо него:
		 * у куска мира в этом цвете живёт окраска плитки — та, которой
		 * трава зелёная, а не серая. Затереть её светом значило бы обесцветить
		 * всё, что красится не текстурой.
		 */
		dst.Color.set(
			(src.Color.getAlpha() * color.getAlpha()) / 255,
			(src.Color.getRed() * color.getRed()) / 255,
			(src.Color.getGreen() * color.getGreen()) / 255,
			(src.Color.getBlue() * color.getBlue()) / 255);
	}

	m_centres[slot] = transform.getTranslation();
	m_box_dirty = true;
	m_mesh_buffer->setDirty(scene::EBT_VERTEX);
}

void MeshBatch::updateIndices()
{
	if (!m_indices_dirty)
		return;

	const u32 verts = verticesPerInstance();
	auto &ibuf = m_mesh_buffer->Indices->Data;
	ibuf.clear();
	ibuf.reserve(m_used * m_template_indices.size());

	// Пустые места просто не попадают в индексы: их вершины остаются в
	// буфере, но не рисуются. Это дешевле, чем сдвигать всё живое к началу.
	for (u32 slot = 0; slot < m_live.size(); slot++) {
		if (!m_live[slot])
			continue;
		const u32 base = slot * verts;
		for (u16 index : m_template_indices)
			ibuf.push_back(static_cast<u16>(base + index));
	}

	// Перестройка индексов — не бесплатная работа: буфер уезжает на карту
	// заново. Считаем её, чтобы было видно, если пакет начнёт перестраиваться
	// каждый кадр, а не тогда, когда жильцы приходят и уходят.
	g_profiler->avg("MeshBatch: перестроек индексов [#]", 1);
	m_mesh_buffer->setDirty(scene::EBT_INDEX);
	m_indices_dirty = false;
}

void MeshBatch::OnRegisterSceneNode()
{
	if (IsVisible && !isEmpty()) {
		// Коробка считается здесь, а не в render(): по ней сцена решает,
		// попадает ли пакет в кадр, и посчитанная позже она опоздала бы
		// ровно на тот кадр, в котором мусор появился.
		updateBoundingBox();
		SceneManager->registerNodeForRendering(this,
				m_transparent ? scene::ESNRP_TRANSPARENT : scene::ESNRP_SOLID);
	}
	scene::ISceneNode::OnRegisterSceneNode();
}

void MeshBatch::updateBoundingBox()
{
	if (!m_box_dirty)
		return;

	core::aabbox3df box(v3f(0, 0, 0));
	bool first = true;
	for (u32 slot = 0; slot < m_live.size(); slot++) {
		if (!m_live[slot])
			continue;
		if (first) {
			box.reset(m_centres[slot]);
			first = false;
		} else {
			box.addInternalPoint(m_centres[slot]);
		}
	}
	// Раздуваем на радиус образца: коробка экземпляра годится при любом
	// его повороте.
	box.MinEdge -= v3f(m_template_radius);
	box.MaxEdge += v3f(m_template_radius);
	m_bounding_box = box;
	m_box_dirty = false;
}

void MeshBatch::render()
{
	if (isEmpty())
		return;

	updateIndices();

	video::IVideoDriver *driver = SceneManager->getVideoDriver();
	// Вершины уже в координатах сцены: узлу двигать нечего.
	driver->setTransform(video::ETS_WORLD, core::matrix4());
	driver->setMaterial(m_mesh_buffer->getMaterial());
	driver->drawMeshBuffer(m_mesh_buffer.get());
}

/*
	MeshBatchManager
*/

MeshBatchManager::~MeshBatchManager()
{
	clearAll();
}

MeshBatchManager::Slot MeshBatchManager::acquire(const std::string &key,
		const video::SMaterial &material, bool transparent, bool casts_shadow,
		const video::S3DVertex *vertices, u32 vertex_count,
		const u16 *indices, u32 index_count)
{
	if (!m_smgr || vertex_count == 0 || index_count == 0)
		return {};

	auto &list = m_batches[key];

	// Ищем с конца: заведённые раньше заполнены доверху, и перебирать их
	// каждый раз незачем. Освободившиеся места в них подберутся, когда до
	// них дойдёт очередь.
	for (auto it = list.rbegin(); it != list.rend(); ++it) {
		if (!(*it)->hasRoom())
			continue;
		if (auto slot = (*it)->allocate()) {
			Slot out;
			out.batch = *it;
			out.index = *slot;
			return out;
		}
	}

	auto batch = make_irr<MeshBatch>(m_smgr, m_smgr->getRootSceneNode(),
			material, transparent, casts_shadow,
			vertices, vertex_count, indices, index_count);
	MeshBatch *raw = batch.get();
	list.push_back(std::move(batch));

	auto index = raw->allocate();
	if (!index)
		return {};

	Slot out;
	out.batch = list.back();
	out.index = *index;
	return out;
}

void MeshBatchManager::collectEmpty()
{
	for (auto it = m_batches.begin(); it != m_batches.end();) {
		auto &list = it->second;
		// Последний пакет ключа не трогаем даже пустым: мусор такого рода
		// появляется приступами, и заводить буфер заново на каждый выстрел
		// дороже, чем подержать пустой.
		while (list.size() > 1 && list.back()->isEmpty()) {
			list.back()->remove();
			list.pop_back();
		}
		if (list.size() == 1 && list.back()->isEmpty()) {
			list.back()->remove();
			it = m_batches.erase(it);
		} else {
			++it;
		}
	}
}

void MeshBatchManager::clearAll()
{
	for (auto &[key, list] : m_batches) {
		for (auto &batch : list)
			batch->remove();
	}
	m_batches.clear();
}
