// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "test.h"

#include "server/player_sao.h"

#include <cmath>

/*
 * Две арифметики, на которых стоит серверный счёт падений и полёта.
 *
 * Обе перенесены на сервер оттуда, где им верили на слово: урон от падения
 * называл клиент, а высоту без опоры не проверял никто. Проверяются они здесь,
 * а не глазами в игре, по двум причинам.
 *
 * Первая — цена ошибки. Урон от падения виден каждому игроку в каждом матче, и
 * промах в множителе означает не «немного не так», а смерть с трёх нод или
 * бессмертие с пятидесяти. Одна такая ошибка уже была и стоила ровно этого:
 * movement_gravity хранится умноженной на BS, а считали её так, будто она в
 * нодах, — урон выходил втрое больше должного. Тест, написанный от физики, а не
 * от кода, такое ловит сразу.
 *
 * Вторая — то, что проверять тут есть чем. Обе величины — чистые функции от
 * чисел, без мира, без карты и без игрока, и ответы у них известны заранее:
 * скорость в конце свободного падения и высота брошенного вверх тела считаются
 * школьной формулой. Поэтому ниже сверяются не «то же, что вышло у нас», а то,
 * что должно выйти.
 *
 * Числа взяты движковые: тяжесть 9.81 ноды в секунду за секунду, прыжок 6.5
 * ноды в секунду (src/player.cpp), порог урона 14 нод в секунду (клиентский,
 * src/client/clientenvironment.cpp).
 */

namespace
{

constexpr f32 GRAVITY = 9.81f;
constexpr f32 JUMP = 6.5f;
constexpr u16 HP_MAX = 20;

/// Скорость в конце свободного падения с высоты h. Школьная формула, и
/// написана она здесь нарочно: сверять реализацию с самой собой смысла нет.
f32 speedAfterFall(f32 h)
{
	return std::sqrt(2.0f * GRAVITY * h);
}

/// Сколько урона обязано выйти по клиентскому правилу: всё, что сверх порога.
int expectedDamage(f32 h, f32 factor = 1.0f)
{
	const f32 over = speedAfterFall(h) * factor - 14.0f;
	if (over <= 0.0f)
		return 0;
	return (int)std::min(over + 0.5f, (f32)HP_MAX);
}

} // namespace

class TestPlayerFooting : public TestBase
{
public:
	TestPlayerFooting() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestPlayerFooting"; }

	void runTests(ICrateDef *cratedef);

	void testShallowFallsAreFree();
	void testDeepFallsFollowTheCurve();
	void testDamageIsBounded();
	void testGroundAndArmourCount();
	void testPushCounts();
	void testJumpCurveShape();
	void testJumpStaysUnderItsCeiling();
	void testHangingBreaksTheCeiling();
	void testRisingBreaksItAtOnce();
};

static TestPlayerFooting g_test_instance;

void TestPlayerFooting::runTests(ICrateDef *cratedef)
{
	TEST(testShallowFallsAreFree);
	TEST(testDeepFallsFollowTheCurve);
	TEST(testDamageIsBounded);
	TEST(testGroundAndArmourCount);
	TEST(testPushCounts);
	TEST(testJumpCurveShape);
	TEST(testJumpStaysUnderItsCeiling);
	TEST(testHangingBreaksTheCeiling);
	TEST(testRisingBreaksItAtOnce);
}

//--- Урон от падения ---------------------------------------------------------

void TestPlayerFooting::testShallowFallsAreFree()
{
	// Ниже порога падение не стоит ничего. Десять нод — примерно там, где
	// четырнадцать нод в секунду и набираются, поэтому граница проверяется с
	// обеих сторон, а не на глаз.
	for (const f32 drop : { 0.0f, 0.5f, 1.0f, 3.0f, 5.0f, 8.0f, 9.5f }) {
		const u16 got = fallDamageFromDrop(drop, GRAVITY, 1.0f, 0.0f, HP_MAX);
		UTEST(got == 0, "падение %.1f ноды стоило %d, а должно ничего",
				(double)drop, (int)got);
	}

	// И отрицательная глубина — это не падение, а ошибка вызова.
	UASSERT(fallDamageFromDrop(-5.0f, GRAVITY, 1.0f, 0.0f, HP_MAX) == 0);
}

void TestPlayerFooting::testDeepFallsFollowTheCurve()
{
	// Сравнение идёт с формулой, а не с прошлым ответом реализации.
	for (const f32 drop : { 11.0f, 12.0f, 15.0f, 20.0f, 30.0f, 50.0f, 80.0f }) {
		const int want = expectedDamage(drop);
		const u16 got = fallDamageFromDrop(drop, GRAVITY, 1.0f, 0.0f, HP_MAX);
		UTEST((int)got == want, "падение %.0f нод: урон %d, ожидалось %d",
				(double)drop, (int)got, want);
	}

	// Ровно та ошибка, которая уже случалась: тяжесть, взятая в единицах
	// движка (умноженная на BS), дала бы втрое больший урон. Проверяется, что
	// на глубине, где урона быть не должно, его и нет при правильных числах —
	// и что при десятикратной тяжести он появляется. Это не подгонка, а
	// напоминание, что единица здесь — нода, а не BS.
	UASSERT(fallDamageFromDrop(8.0f, GRAVITY, 1.0f, 0.0f, HP_MAX) == 0);
	UASSERT(fallDamageFromDrop(8.0f, GRAVITY * 10.0f, 1.0f, 0.0f, HP_MAX) > 0);
}

void TestPlayerFooting::testDamageIsBounded()
{
	// С какой бы высоты ни падали, отнять больше, чем есть, нельзя: поле u16,
	// и падение не должно становиться способом передать серверу любое число.
	for (const f32 drop : { 100.0f, 1000.0f, 31000.0f }) {
		const u16 got = fallDamageFromDrop(drop, GRAVITY, 1.0f, 0.0f, HP_MAX);
		UTEST(got == HP_MAX, "падение %.0f нод дало %d при пределе %d",
				(double)drop, (int)got, (int)HP_MAX);
	}
}

void TestPlayerFooting::testGroundAndArmourCount()
{
	const f32 drop = 20.0f;

	// Пружинящая нода (fall_damage_add_percent = -100) даёт множитель ноль, и
	// падение на неё не стоит ничего с любой высоты.
	UASSERT(fallDamageFromDrop(drop, GRAVITY, 0.0f, 0.0f, HP_MAX) == 0);
	UASSERT(fallDamageFromDrop(1000.0f, GRAVITY, 0.0f, 0.0f, HP_MAX) == 0);

	// Половинный множитель — половина скорости, а не половина урона: порог
	// вычитается после умножения, как и у клиента.
	const u16 half = fallDamageFromDrop(drop, GRAVITY, 0.5f, 0.0f, HP_MAX);
	UTEST((int)half == expectedDamage(drop, 0.5f),
			"половинный множитель дал %d, ожидалось %d",
			(int)half, expectedDamage(drop, 0.5f));

	// И удвоенный множитель делает больно там, где просто так не больно.
	UASSERT(fallDamageFromDrop(8.0f, GRAVITY, 2.0f, 0.0f, HP_MAX) > 0);
}

void TestPlayerFooting::testPushCounts()
{
	// Толчок вниз, который дал сам сервер, прибавляется к скорости: игрок его
	// не выдумывал, и падение с ним быстрее.
	const f32 drop = 12.0f;
	const u16 plain = fallDamageFromDrop(drop, GRAVITY, 1.0f, 0.0f, HP_MAX);
	const u16 pushed = fallDamageFromDrop(drop, GRAVITY, 1.0f, 6.0f, HP_MAX);
	UTEST(pushed > plain, "с толчком урон %d, без толчка %d",
			(int)pushed, (int)plain);

	// Знак толчка не важен: вниз считается по величине.
	UASSERT(fallDamageFromDrop(drop, GRAVITY, 1.0f, -6.0f, HP_MAX) == pushed);
}

//--- Потолок прыжка ----------------------------------------------------------

void TestPlayerFooting::testJumpCurveShape()
{
	// Три точки, известные заранее: старт, вершина и возвращение.
	UASSERT(std::fabs(jumpReachAfter(JUMP, GRAVITY, 0.0f)) < 1e-6f);

	const f32 apex_t = JUMP / GRAVITY;
	const f32 apex_h = JUMP * JUMP / (2.0f * GRAVITY);
	UTEST(std::fabs(jumpReachAfter(JUMP, GRAVITY, apex_t) - apex_h) < 1e-3f,
			"вершина вышла %f, а должна %f",
			(double)jumpReachAfter(JUMP, GRAVITY, apex_t), (double)apex_h);

	// Через два времени подъёма тело обязано вернуться туда, откуда начало.
	UASSERT(std::fabs(jumpReachAfter(JUMP, GRAVITY, 2.0f * apex_t)) < 1e-3f);

	// И дальше уходит ниже: считать это «ещё прыжком» нельзя.
	UASSERT(jumpReachAfter(JUMP, GRAVITY, 2.5f * apex_t) < 0.0f);
}

void TestPlayerFooting::testJumpStaysUnderItsCeiling()
{
	// Настоящий прыжок против потолка с запасом: потолок считается с той же
	// тяжестью, но с запасом по начальной скорости, поэтому обязан лежать
	// выше во всякий момент полёта.
	const f32 slack = 1.3f;
	for (f32 t = 0.05f; t < 1.4f; t += 0.05f) {
		const f32 real = jumpReachAfter(JUMP, GRAVITY, t);
		const f32 ceiling = jumpReachAfter(JUMP * slack, GRAVITY, t);
		UTEST(real < ceiling, "на %.2f с прыжок %.2f, потолок %.2f",
				(double)t, (double)real, (double)ceiling);
	}
}

void TestPlayerFooting::testHangingBreaksTheCeiling()
{
	// Зависший: поднялся как все — по настоящей кривой прыжка — и на вершине
	// остановился.
	//
	// Именно так, а не «сразу на высоте вершины»: тело не может оказаться там
	// в тот же миг, как оторвалось от земли, и проверять надо то, что бывает.
	// Заодно это проверяет, что сам подъём под потолок не попадает.
	const f32 slack = 1.3f;
	const f32 apex_t = JUMP / GRAVITY;
	const f32 apex_h = JUMP * JUMP / (2.0f * GRAVITY);

	f32 caught_at = -1.0f;
	for (f32 t = 0.0f; t < 5.0f; t += 0.01f) {
		const f32 where = t < apex_t ? jumpReachAfter(JUMP, GRAVITY, t) : apex_h;
		if (where > jumpReachAfter(JUMP * slack, GRAVITY, t)) {
			caught_at = t;
			break;
		}
	}

	UASSERT(caught_at > 0.0f);
	UTEST(caught_at > apex_t, "поймали ещё на подъёме, на %.2f с при вершине "
			"в %.2f с — обычный прыжок так не пройдёт",
			(double)caught_at, (double)apex_t);
	UTEST(caught_at < 2.5f, "висящего поймали только через %.2f с",
			(double)caught_at);
}

void TestPlayerFooting::testRisingBreaksItAtOnce()
{
	// Поднимающийся ровно вверх — а это и есть полёт — обязан выйти за потолок
	// быстрее, чем успел бы прыгнуть.
	const f32 slack = 1.3f;
	const f32 climb = 10.0f; // ноды в секунду, вверх и не останавливаясь

	f32 caught_at = -1.0f;
	for (f32 t = 0.01f; t < 5.0f; t += 0.01f) {
		if (climb * t > jumpReachAfter(JUMP * slack, GRAVITY, t)) {
			caught_at = t;
			break;
		}
	}

	UASSERT(caught_at > 0.0f);
	UTEST(caught_at < 1.0f, "поднимающегося поймали только через %.2f с",
			(double)caught_at);
}
