// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "irrlichttypes_bloated.h"
#include "irr_ptr.h"
#include "util/basic_macros.h"
#include "ISceneNode.h"
#include "S3DVertex.h"
#include "CMeshBuffer.h"

#include <string>
#include <unordered_map>
#include <optional>
#include <vector>

/*!
 * Пакет одинаковых мешей: одна команда рисования на всю россыпь.
 *
 * Задача та же, что у буфера частиц, но для сущностей. Сущность — это узел
 * сцены, а узел сцены — это своя команда рисования: шестьсот гильз на полу
 * стоят шестисот команд, и кадр ложится прежде, чем поле боя начинает
 * выглядеть обжитым. Между тем гильзы одинаковы: одна модель, одна текстура,
 * один материал. Разные у них только место, поворот и свет.
 *
 * Поэтому вершины всех таких сущностей лежат в одном буфере. Экземпляру
 * отводится в нём постоянное место — столько вершин, сколько их в модели, —
 * и при движении он переписывает своё место, ничего не зная о соседях.
 * Рисуются они все разом.
 *
 * Что за это отдано: у пакетной сущности нет ни скелетной анимации (кости
 * двигают меш, а меш здесь общий), ни привязок, ни таблички с именем. Мусору
 * это не нужно.
 */
class MeshBatch : public scene::ISceneNode
{
public:
	/*!
	 * \param mgr    сцена, в которую встаёт пакет
	 * \param parent узел-родитель (корень сцены)
	 * \param material материал, общий для всех экземпляров
	 * \param vertices,vertex_count образец: вершины одной модели
	 * \param indices,index_count образец: её же индексы
	 */
	MeshBatch(scene::ISceneManager *mgr, scene::ISceneNode *parent,
			const video::SMaterial &material, bool transparent,
			bool casts_shadow,
			const video::S3DVertex *vertices, u32 vertex_count,
			const u16 *indices, u32 index_count);

	~MeshBatch() override;

	DISABLE_CLASS_COPY(MeshBatch)

	//! Занять место под экземпляр. Пусто — пакет полон.
	std::optional<u32> allocate();

	//! Освободить место. Вершины гаснут до следующего жильца.
	void release(u32 slot);

	/*!
	 * Переписать экземпляр: где он, как повёрнут и каким светом освещён.
	 *
	 * \param transform место, поворот и размер — в координатах сцены
	 * \param color свет, которым покрашены вершины
	 */
	void write(u32 slot, const core::matrix4 &transform, video::SColor color);

	//! Сколько экземпляров вмещает пакет. Ограничение — шестнадцать бит на
	//! индексы вершин, то есть 65536 вершин на буфер.
	u32 capacity() const { return m_capacity; }

	//! Есть ли куда положить ещё одного.
	bool hasRoom() const
	{ return !m_free_list.empty() || m_live.size() < m_capacity; }

	//! Сколько вершин в образце. По нему считается место экземпляра.
	u32 verticesPerInstance() const { return m_template_vertices.size(); }

	bool isEmpty() const { return m_used == 0; }

	// --- ISceneNode ---

	void OnRegisterSceneNode() override;
	void render() override;
	const core::aabbox3df &getBoundingBox() const override { return m_bounding_box; }
	video::SMaterial &getMaterial(u32 num) override
	{ return m_mesh_buffer->getMaterial(); }
	u32 getMaterialCount() const override { return 1; }

private:
	//! Перестроить индексы: рисуются только занятые места.
	void updateIndices();

	//! Пересчитать общую коробку по местам жильцов.
	void updateBoundingBox();

	/*!
	 * Полупрозрачный ли пакет.
	 *
	 * Полупрозрачное рисуется в своём проходе и по-хорошему требует порядка
	 * от дальнего к ближнему. Внутри пакета порядка нет: экземпляры лежат
	 * там, где нашлось место. Для мусора с прорезанной альфой — гильзы,
	 * отметины — это безразлично, потому что глубина всё равно пишется; для
	 * дыма и стекла пакет не годится, и в документации так и сказано.
	 */
	bool m_transparent = false;

	std::vector<video::S3DVertex> m_template_vertices;
	std::vector<u16> m_template_indices;

	irr_ptr<scene::SMeshBuffer> m_mesh_buffer;

	u32 m_capacity = 0;
	u32 m_used = 0;
	std::vector<u32> m_free_list;
	std::vector<bool> m_live;
	//! Место каждого экземпляра — по нему считается общая коробка.
	std::vector<v3f> m_centres;
	//! Наибольшее удаление вершины образца от его начала: коробке экземпляра
	//! этого хватает при любом повороте.
	f32 m_template_radius = 0.0f;

	bool m_indices_dirty = true;
	bool m_box_dirty = true;
	core::aabbox3df m_bounding_box{{0, 0, 0}};
};

/*!
 * Кто сводит одинаковые меши вместе.
 *
 * Пакет заводится по ключу, который описывает всё, что должно совпадать:
 * модель, текстура, свойства материала. Совпало — экземпляр ложится в уже
 * заведённый пакет; не совпало — заводится новый. Опустевшие пакеты уходят
 * сами.
 */
class MeshBatchManager
{
public:
	explicit MeshBatchManager(scene::ISceneManager *smgr) : m_smgr(smgr) {}
	~MeshBatchManager();

	DISABLE_CLASS_COPY(MeshBatchManager)

	/*!
	 * Место в пакете: сам пакет и номер экземпляра в нём.
	 *
	 * Пакет держится ссылкой, а не голым указателем, и это не педантизм:
	 * жилец переживает менеджера. Клиент разбирает себя по частям, и порядок,
	 * в котором уходят окружение и буферы, — не то, на что стоит полагаться;
	 * с ссылкой последний уходящий гасит свет сам.
	 */
	struct Slot
	{
		irr_ptr<MeshBatch> batch;
		u32 index = 0;

		bool valid() const { return batch.get() != nullptr; }
	};

	/*!
	 * Занять место для такой геометрии — в подходящем пакете или в новом.
	 *
	 * \param key что должно совпадать у соседей по пакету
	 * \return место; недействительное, если сцены нет или геометрия пуста
	 */
	Slot acquire(const std::string &key, const video::SMaterial &material,
			bool transparent, bool casts_shadow,
			const video::S3DVertex *vertices, u32 vertex_count,
			const u16 *indices, u32 index_count);

	//! Убрать пакеты, из которых ушёл последний жилец.
	void collectEmpty();

	//! Снести всё: смена мира, выход.
	void clearAll();

	//! Сколько пакетов сейчас живо. Для замеров.
	size_t count() const { return m_batches.size(); }

private:
	scene::ISceneManager *m_smgr;
	/*!
	 * Пакетов на один ключ может быть несколько: место в буфере кончается
	 * раньше, чем мусор на поле.
	 */
	std::unordered_map<std::string, std::vector<irr_ptr<MeshBatch>>> m_batches;
};
