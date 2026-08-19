// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "test.h"

#include "client/camera_fx.h"
#include "irrMath.h"

#include <cmath>
#include <vector>

/*
 * Толчки камеры: отдача, удар взрывной волны, дрожь.
 *
 * Проверяется здесь ровно то, ради чего этот слой и заведён, — три обещания,
 * которые на глаз не проверить никак:
 *
 *   1. Частота кадров ни на что не влияет. Одна и та же отдача при 30 и при
 *      240 кадрах должна давать одну и ту же кривую, а не «на слабой машине
 *      мягче». Считать пружину прямо по времени кадра — самый простой способ
 *      это обещание нарушить, и именно поэтому шаг интегрирования постоянный.
 *
 *   2. Отдача возвращается в ноль сама и не перелетает его. Оружие, после
 *      очереди оставляющее прицел в небе, — это не отдача, а поломка.
 *
 *   3. Несколько толчков складываются, а не перебивают друг друга. Выстрел
 *      во время близкого взрыва должен дать сумму, иначе слои бессмысленны.
 *
 * Ничего случайного в этих ответах нет и быть не должно: дрожь считается
 * замкнутой формулой от времени события, поэтому её тоже можно сравнивать
 * числом, а не глазом.
 */

namespace
{

// Время всюду считается в шагах интегрирования (1/240 с), целыми числами.
// Плавающая сумма кадров дала бы дрейф, и тест мерил бы его, а не пружину.
constexpr f32 UNIT = 1.0f / 240.0f;

struct Sample
{
	int units; // сколько прошло от начала
	v3f rotation;
	v3f offset;
};

// Прогнать слой кадрами заданной длины (в тех же единицах) и вернуть значение
// после каждого кадра. Длины кадров повторяются по кругу, поэтому одним
// вызовом задаётся и ровная частота, и рваная.
std::vector<Sample> run(CameraFx &fx, const std::vector<int> &pattern, int total)
{
	std::vector<Sample> out;
	int elapsed = 0;
	size_t i = 0;
	while (elapsed < total) {
		const int frame = pattern[i++ % pattern.size()];
		CameraFx::Result r = fx.step(frame * UNIT);
		elapsed += frame;
		out.push_back({ elapsed, r.rotation, r.offset });
	}
	return out;
}

std::vector<Sample> run(CameraFx &fx, int frame, int total)
{
	return run(fx, std::vector<int>{ frame }, total);
}

// Значение в заданный момент. Момент обязан попасть на границу кадра —
// сравнивать разные мгновения бессмысленно.
v3f at(const std::vector<Sample> &samples, int units)
{
	for (const Sample &s : samples) {
		if (s.units == units)
			return s.rotation;
	}
	FATAL_ERROR("в прогоне нет кадра, кончающегося в нужный момент");
}

f32 peakPitch(const std::vector<Sample> &samples)
{
	f32 peak = 0.0f;
	for (const Sample &s : samples)
		peak = std::min(peak, s.rotation.X);
	return peak;
}

} // namespace

class TestCameraFx : public TestBase
{
public:
	TestCameraFx() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestCameraFx"; }

	void runTests(IPlaceDef *placedef);

	void testFramerateIndependence();
	void testJitteryFrames();
	void testRecoilRecovers();
	void testRecoilAccumulates();
	void testLayersAdd();
	void testShakeIsDeterministic();
	void testShakeEnds();
	void testReset();
};

static TestCameraFx g_test_instance;

void TestCameraFx::runTests(IPlaceDef *placedef)
{
	TEST(testFramerateIndependence);
	TEST(testJitteryFrames);
	TEST(testRecoilRecovers);
	TEST(testRecoilAccumulates);
	TEST(testLayersAdd);
	TEST(testShakeIsDeterministic);
	TEST(testShakeEnds);
	TEST(testReset);
}

void TestCameraFx::testFramerateIndependence()
{
	// Один и тот же выстрел при 30, 60, 120 и 240 кадрах. Длины кадров в шагах
	// интегрирования: 8, 4, 2 и 1 — все они кратны шагу, поэтому в каждый
	// момент, кратный 1/30, все четыре прогона прожили одно и то же время и
	// сделали одно и то же число шагов. Совпадать они обязаны не «примерно»,
	// а с точностью счёта.
	const int frames[] = { 8, 4, 2, 1 };
	std::vector<std::vector<Sample>> runs;

	for (int frame : frames) {
		CameraFx fx;
		fx.addRecoil(v3f(-0.45f, 0.12f, 0.0f), 95.0f, 20.0f);
		runs.push_back(run(fx, frame, 240));
	}

	for (int units = 8; units <= 232; units += 8) {
		const v3f reference = at(runs[3], units);
		for (size_t i = 0; i < 3; i++) {
			const f32 diff = (at(runs[i], units) - reference).getLength();
			UTEST(diff < 1e-3f,
					"на %.3f с при %d кадрах в секунду отдача разошлась на %f°",
					(double)(units * UNIT), 240 / frames[i], (double)diff);
		}
	}

	// И вершина кривой — то, что игрок собственно и чувствует.
	const f32 reference_peak = peakPitch(runs[3]);
	for (size_t i = 0; i < 3; i++) {
		const f32 peak = peakPitch(runs[i]);
		UTEST(std::abs(peak - reference_peak) < 0.02f,
				"вершина отдачи при %d кадрах: %f° против %f°",
				240 / frames[i], (double)peak, (double)reference_peak);
	}
}

void TestCameraFx::testJitteryFrames()
{
	// Настоящая частота кадров не ровная: она плавает, проседает и скачет.
	// Остаток кадра, не уложившийся в целое число шагов, переносится в
	// следующий, поэтому к любому общему моменту оба прогона успевают сделать
	// одинаковое число шагов — расходиться им попросту негде.
	CameraFx steady, jittery;
	steady.addRecoil(v3f(-0.45f, 0.12f, 0.0f), 95.0f, 20.0f);
	jittery.addRecoil(v3f(-0.45f, 0.12f, 0.0f), 95.0f, 20.0f);

	// Рваная последовательность подобрана так, чтобы её период складывался в
	// те же 8 шагов: иначе сравнивать было бы нечего.
	const auto a = run(steady, 8, 240);
	const auto b = run(jittery, std::vector<int>{ 3, 1, 4 }, 240);

	for (int units = 8; units <= 232; units += 8) {
		const f32 diff = (at(a, units) - at(b, units)).getLength();
		UTEST(diff < 1e-3f, "на %.3f с рваные кадры разошлись с ровными на %f°",
				(double)(units * UNIT), (double)diff);
	}
}

void TestCameraFx::testRecoilRecovers()
{
	CameraFx fx;
	fx.addRecoil(v3f(-0.45f, 0.12f, 0.05f), 95.0f, 20.0f);
	const auto samples = run(fx, 4, 288);

	// Подъём быстрый: вершина проходится в первые полсекунды.
	const f32 peak = peakPitch(samples);
	UTEST(peak < -0.1f, "отдачи почти нет: вершина %f°", (double)peak);

	// К концу — ноль, и ноль настоящий, а не «почти».
	const v3f settled = samples.back().rotation;
	UTEST(settled.getLength() < 0.01f,
			"отдача не вернулась: осталось %f°", (double)settled.getLength());

	// Через ноль не перелетает: 2*sqrt(95) ≈ 19.5, вязкость 20 — чуть выше
	// критической, качания быть не должно.
	f32 overshoot = 0.0f;
	bool past_peak = false;
	for (const Sample &s : samples) {
		if (s.rotation.X <= peak + 1e-4f)
			past_peak = true;
		if (past_peak)
			overshoot = std::max(overshoot, s.rotation.X);
	}
	UTEST(overshoot < 0.02f, "отдача перелетела ноль на %f°", (double)overshoot);
}

void TestCameraFx::testRecoilAccumulates()
{
	// Очередь: десять выстрелов в секунду, как у винтовки на 600 в минуту.
	CameraFx fx;
	f32 highest = 0.0f;
	for (int shot = 0; shot < 10; shot++) {
		fx.addRecoil(v3f(-0.45f, 0.0f, 0.0f), 95.0f, 20.0f);
		const auto samples = run(fx, 4, 24);
		highest = std::min(highest, peakPitch(samples));
	}

	// Один выстрел уводит примерно на градус; очередь обязана увести заметно
	// дальше — иначе сдерживать её незачем.
	CameraFx single;
	single.addRecoil(v3f(-0.45f, 0.0f, 0.0f), 95.0f, 20.0f);
	const f32 one = peakPitch(run(single, 4, 120));

	UTEST(highest < one * 2.5f,
			"очередь почти не накапливается: %f° против %f° за выстрел",
			(double)highest, (double)one);

	// Но и не бесконечно: у пружины есть установившееся значение, и очередь
	// не должна уводить прицел за спину.
	UTEST(highest > -30.0f, "очередь увела прицел на %f°", (double)highest);
}

void TestCameraFx::testLayersAdd()
{
	// Выстрел и взрыв в один момент. Каждый слой считается своей пружиной,
	// поэтому итог обязан быть суммой, а не последним из двух.
	CameraFx recoil_only;
	recoil_only.addRecoil(v3f(-0.45f, 0.0f, 0.0f), 95.0f, 20.0f);
	const v3f a = at(run(recoil_only, 4, 120), 24);

	CameraFx blast_only;
	blast_only.addBlast(v3f(-0.3f, 0.8f, 0.4f), v3f(0.2f, 0.1f, 0.0f), 55.0f, 12.0f);
	const v3f b = at(run(blast_only, 4, 120), 24);

	CameraFx both;
	both.addRecoil(v3f(-0.45f, 0.0f, 0.0f), 95.0f, 20.0f);
	both.addBlast(v3f(-0.3f, 0.8f, 0.4f), v3f(0.2f, 0.1f, 0.0f), 55.0f, 12.0f);
	const v3f sum = at(run(both, 4, 120), 24);

	const f32 diff = (sum - (a + b)).getLength();
	UTEST(diff < 1e-3f, "слои не сложились: разница %f°", (double)diff);

	// Линейный толчок при этом остался только у взрыва: отдача его не трогает.
	CameraFx push;
	push.addBlast(v3f(), v3f(0.5f, 0.0f, 0.0f), 55.0f, 12.0f);
	const auto samples = run(push, 4, 240);
	f32 peak = 0.0f;
	for (const Sample &s : samples)
		peak = std::max(peak, s.offset.X);
	UTEST(peak > 0.001f, "линейного толчка нет вовсе: %f", (double)peak);
	UTEST(samples.back().offset.getLength() < 1e-3f,
			"линейный толчок не вернулся: осталось %f",
			(double)samples.back().offset.getLength());
}

void TestCameraFx::testShakeIsDeterministic()
{
	// Дважды одна и та же дрожь — дважды один и тот же след. Случайное число
	// за кадр сделало бы это невозможным, и в этом вся разница.
	CameraFx first, second;
	first.addShake(0.05f, 12.0f, 7.0f, 0.75f);
	second.addShake(0.05f, 12.0f, 7.0f, 0.75f);

	const auto a = run(first, 4, 192);
	const auto b = run(second, 4, 192);
	UASSERT(a.size() == b.size());
	for (size_t i = 0; i < a.size(); i++) {
		UTEST((a[i].rotation - b[i].rotation).getLength() < 1e-6f,
				"дрожь разошлась сама с собой на шаге %d", (int)i);
	}

	// И она именно колеблется, а не уползает в одну сторону.
	int sign_changes = 0;
	for (size_t i = 1; i < a.size(); i++) {
		if (a[i].rotation.X * a[i - 1].rotation.X < 0)
			sign_changes++;
	}
	UTEST(sign_changes >= 8, "дрожь не колеблется: смен знака %d", sign_changes);
}

void TestCameraFx::testShakeEnds()
{
	CameraFx fx;
	fx.addShake(0.05f, 12.0f, 7.0f, 0.5f);
	const auto samples = run(fx, 4, 240);

	// Затухает: вторая половина заметно тише первой.
	f32 early = 0.0f, late = 0.0f;
	for (const Sample &s : samples) {
		const f32 size = std::abs(s.rotation.X);
		if (s.units * UNIT < 0.2f)
			early = std::max(early, size);
		if (s.units * UNIT > 0.35f)
			late = std::max(late, size);
	}
	UTEST(early > late * 3.0f, "дрожь не затухает: %f против %f",
			(double)early, (double)late);

	// И кончается ровно, без щелчка на последнем кадре.
	UTEST(samples.back().rotation.getLength() < 1e-6f,
			"дрожь не кончилась: осталось %f",
			(double)samples.back().rotation.getLength());
}

void TestCameraFx::testReset()
{
	CameraFx fx;
	fx.addRecoil(v3f(-0.45f, 0.1f, 0.0f), 95.0f, 20.0f);
	fx.addBlast(v3f(-0.3f, 0.8f, 0.4f), v3f(0.2f, 0.1f, 0.0f), 55.0f, 12.0f);
	fx.addShake(0.05f, 12.0f, 7.0f, 0.75f);
	run(fx, 4, 24);

	fx.reset();
	const CameraFx::Result r = fx.step(1.0f / 60);
	UTEST(r.rotation.getLength() < 1e-6f && r.offset.getLength() < 1e-6f,
			"после сброса осталось %f° и %f",
			(double)r.rotation.getLength(), (double)r.offset.getLength());
}
