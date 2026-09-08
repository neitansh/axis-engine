// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "log_internal.h"

#include "threading/mutex_auto_lock.h"
#include "gettime.h"
#include "porting.h"
#include "exceptions.h"
#include "filesys.h"
#include "serialization.h"
#include "util/string.h"
#include "util/numeric.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

#if !defined(_WIN32)
#include <unistd.h> // isatty
#endif

#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>

class LevelTarget : public LogTarget {
public:
	LevelTarget(Logger &logger, LogLevel level, bool raw = false) :
		m_logger(logger),
		m_level(level),
		m_raw(raw)
	{}

	virtual bool hasOutput() override {
		return m_logger.hasOutput(m_level);
	}

	virtual void log(std::string_view buf) override {
		if (!m_raw) {
			m_logger.log(m_level, buf);
		} else {
			m_logger.logRaw(m_level, buf);
		}
	}

private:
	Logger &m_logger;
	LogLevel m_level;
	bool m_raw;
};

////
//// Globals
////

Logger g_logger;

#ifdef __ANDROID__
AndroidLogOutput stdout_output;
AndroidLogOutput stderr_output;
#else
StreamLogOutput stdout_output(std::cout);
StreamLogOutput stderr_output(std::cerr);
#endif

LevelTarget none_target_raw(g_logger, LL_NONE, true);
LevelTarget none_target(g_logger, LL_NONE);
LevelTarget error_target(g_logger, LL_ERROR);
LevelTarget warning_target(g_logger, LL_WARNING);
LevelTarget action_target(g_logger, LL_ACTION);
LevelTarget info_target(g_logger, LL_INFO);
LevelTarget verbose_target(g_logger, LL_VERBOSE);
LevelTarget trace_target(g_logger, LL_TRACE);

thread_local LogStream dstream(none_target);
thread_local LogStream rawstream(none_target_raw);
thread_local LogStream errorstream(error_target);
thread_local LogStream warningstream(warning_target);
thread_local LogStream actionstream(action_target);
thread_local LogStream infostream(info_target);
thread_local LogStream verbosestream(verbose_target);
thread_local LogStream tracestream(trace_target);
thread_local LogStream derr_con(verbose_target);
thread_local LogStream dout_con(trace_target);

// Android
#ifdef __ANDROID__

constexpr static unsigned int g_level_to_android[] = {
	ANDROID_LOG_INFO,     // LL_NONE
	ANDROID_LOG_ERROR,    // LL_ERROR
	ANDROID_LOG_WARN,     // LL_WARNING
	ANDROID_LOG_INFO,     // LL_ACTION
	ANDROID_LOG_DEBUG,    // LL_INFO
	ANDROID_LOG_VERBOSE,  // LL_VERBOSE
	ANDROID_LOG_VERBOSE,  // LL_TRACE
};

void AndroidLogOutput::logRaw(LogLevel lev, std::string_view line)
{
	static_assert(ARRLEN(g_level_to_android) == LL_MAX,
		"mismatch between android and internal loglevels");
	__android_log_print(g_level_to_android[lev], PROJECT_NAME_C, "%.*s",
		static_cast<int>(line.size()), line.data());
}
#endif

///////////////////////////////////////////////////////////////////////////////


////
//// Logger
////

LogLevel Logger::stringToLevel(std::string_view name)
{
	if (name == "none")
		return LL_NONE;
	else if (name == "error")
		return LL_ERROR;
	else if (name == "warning")
		return LL_WARNING;
	else if (name == "action")
		return LL_ACTION;
	else if (name == "info")
		return LL_INFO;
	else if (name == "verbose")
		return LL_VERBOSE;
	else if (name == "trace")
		return LL_TRACE;
	else
		return LL_MAX;
}

void Logger::addOutput(ILogOutput *out)
{
	addOutputMaxLevel(out, (LogLevel)(LL_MAX - 1));
}

void Logger::addOutput(ILogOutput *out, LogLevel lev)
{
	addOutputMasked(out, LOGLEVEL_TO_MASKLEVEL(lev));
}

void Logger::addOutputMasked(ILogOutput *out, LogLevelMask mask)
{
	MutexAutoLock lock(m_mutex);
	for (size_t i = 0; i < LL_MAX; i++) {
		if (mask & LOGLEVEL_TO_MASKLEVEL(i)) {
			m_outputs[i].push_back(out);
			m_has_outputs[i] = true;
		}
	}
}

void Logger::addOutputMaxLevel(ILogOutput *out, LogLevel lev)
{
	MutexAutoLock lock(m_mutex);
	assert(lev < LL_MAX);
	for (size_t i = 0; i <= lev; i++) {
		m_outputs[i].push_back(out);
		m_has_outputs[i] = true;
	}
}

LogLevelMask Logger::removeOutput(ILogOutput *out)
{
	MutexAutoLock lock(m_mutex);
	LogLevelMask ret_mask = 0;
	for (size_t i = 0; i < LL_MAX; i++) {
		auto it = std::find(m_outputs[i].begin(), m_outputs[i].end(), out);
		if (it != m_outputs[i].end()) {
			ret_mask |= LOGLEVEL_TO_MASKLEVEL(i);
			m_outputs[i].erase(it);
			m_has_outputs[i] = !m_outputs[i].empty();
		}
	}
	return ret_mask;
}

void Logger::setLevelSilenced(LogLevel lev, bool silenced)
{
	m_silenced_levels[lev] = silenced;
}

void Logger::registerThread(std::string_view name)
{
	std::thread::id id = std::this_thread::get_id();
	MutexAutoLock lock(m_mutex);
	m_thread_names[id] = name;
}

void Logger::deregisterThread()
{
	std::thread::id id = std::this_thread::get_id();
	MutexAutoLock lock(m_mutex);
	m_thread_names.erase(id);
}

const char *Logger::getLevelLabel(LogLevel lev)
{
	static const char *names[] = {
		"",
		"ERROR",
		"WARNING",
		"ACTION",
		"INFO",
		"VERBOSE",
		"TRACE",
	};
	static_assert(ARRLEN(names) == LL_MAX,
		"mismatch between loglevel names and enum");
	assert(lev < LL_MAX && lev >= 0);
	return names[lev];
}

LogColor Logger::color_mode = LOG_COLOR_AUTO;

inline const std::string &Logger::getThreadName()
{
	std::thread::id id = std::this_thread::get_id();

	auto it = m_thread_names.find(id);
	if (it != m_thread_names.end())
		return it->second;

	thread_local std::string fallback_name;
	if (fallback_name.empty()) {
		std::ostringstream os;
		os << "#0x" << std::hex << id;
		fallback_name = os.str();
	}
	return fallback_name;
}

LogTimestamp Logger::timestamp_mode = LOG_TIMESTAMP_WALL;

inline std::string Logger::getLogTimestamp()
{
	const static u64 begin_of_time = porting::getTimeMs();
	if (timestamp_mode == LOG_TIMESTAMP_NONE)
		return std::string();
	if (timestamp_mode == LOG_TIMESTAMP_WALL)
		return getTimestamp();
	// LOG_TIMESTAMP_RELATIVE
	float rel = (porting::getTimeMs() - begin_of_time) / 1000.0f;
	char s[24];
	snprintf(s, sizeof(s), "[% 8.2f]", rel);
	return s;
}

void Logger::log(LogLevel lev, std::string_view text)
{
	if (isLevelSilenced(lev))
		return;

	const std::string &thread_name = getThreadName();
	const char *label = getLevelLabel(lev);
	const std::string timestamp = getLogTimestamp();

	std::string line = timestamp;
	if (!line.empty())
		line.append(": ");
	line.append(label).append("[").append(thread_name)
		.append("]: ").append(text);

	logToOutputs(lev, line, timestamp, thread_name, text);
}

void Logger::logRaw(LogLevel lev, std::string_view text)
{
	if (isLevelSilenced(lev))
		return;

	logToOutputsRaw(lev, text);
}

void Logger::logToOutputsRaw(LogLevel lev, std::string_view line)
{
	MutexAutoLock lock(m_mutex);
	for (size_t i = 0; i != m_outputs[lev].size(); i++)
		m_outputs[lev][i]->logRaw(lev, line);
}

void Logger::logToOutputs(LogLevel lev, const std::string &combined,
	const std::string &time, const std::string &thread_name,
	std::string_view payload_text)
{
	MutexAutoLock lock(m_mutex);
	for (size_t i = 0; i != m_outputs[lev].size(); i++)
		m_outputs[lev][i]->log(lev, combined, time, thread_name, payload_text);
}

////
//// *LogOutput methods
////

namespace {

/// Сколько прошлых журналов держим. Столько же, сколько у лаунчера: две недели
/// игры по паре запусков в день — и на диске меньше мегабайта.
constexpr size_t LOGS_KEPT = 30;

/// Всё здесь молчит нарочно.
///
/// Архив снимается и под замком журнала (когда файл дорос до предела), а
/// файловые помощники движка при неудаче пишут в `errorstream` — то есть в тот
/// же журнал, чей замок уже взят. Обычный `std::mutex` такого не прощает:
/// игрок получил бы не сообщение об ошибке, а повисший клиент.

std::string dayOf(std::time_t when)
{
	std::tm broken{};
#ifdef _WIN32
	localtime_s(&broken, &when);
#else
	localtime_r(&when, &broken);
#endif
	char day[16];
	std::snprintf(day, sizeof(day), "%04d-%02d-%02d",
		broken.tm_year + 1900, broken.tm_mon + 1, broken.tm_mday);
	return day;
}

std::string folderOf(const std::string &path)
{
	size_t cut = path.find_last_of(DIR_DELIM_CHAR);
	return cut == std::string::npos ? std::string(".") : path.substr(0, cut);
}

/// Прошлый запуск — в `ГГГГ-ММ-ДД-N.log.gz` рядом. Возвращает имя архива.
///
/// Дата берётся у самого файла, а не у сегодняшнего дня: в игру могли не
/// заходить неделю, и та неделя должна остаться под своим числом. Номер нужен
/// потому, что за день запусков бывает много.
std::string archive(const std::string &path)
{
	std::string raw;
	if (!fs::ReadFile(path, raw))
		return "";
	if (raw.empty()) {
		fs::DeleteSingleFileOrEmptyDirectory(path);
		return "";
	}

	const std::string folder = folderOf(path);
	const std::string day = dayOf(fs::ModifiedAt(path));
	std::string target;
	for (int ordinal = 1;; ordinal++) {
		target = folder + DIR_DELIM + day + "-" + itos(ordinal) + ".log.gz";
		if (!fs::PathExists(target))
			break;
	}

	std::ostringstream packed(std::ios::binary);
	try {
		compressGzip(raw, packed);
	} catch (const SerializationError &) {
		return "";
	}

	// Своим потоком, а не `fs::safeWriteToFile`: тот при неудаче пишет в журнал.
	std::ofstream out(target, std::ios::binary | std::ios::trunc);
	const std::string body = packed.str();
	out.write(body.data(), body.size());
	if (!out.good())
		return "";
	out.close();

	fs::DeleteSingleFileOrEmptyDirectory(path);
	return target;
}

/// Убрать всё, что осталось от прошлых запусков сверх счёта.
///
/// Зовётся только при заведении журнала, когда замок ещё никем не взят:
/// перечисление каталога — единственное здесь, что при неудаче говорит вслух.
///
/// По времени файла, а не по имени: `…-2` и `…-10` по алфавиту идут не по
/// порядку, и выброшено оказалось бы не то.
void pruneArchives(const std::string &folder)
{
	std::vector<std::pair<std::time_t, std::string>> kept;
	for (const fs::DirListNode &node : fs::GetDirListing(folder)) {
		if (node.dir || !str_ends_with(node.name, std::string(".log.gz")))
			continue;
		const std::string path = folder + DIR_DELIM + node.name;
		kept.emplace_back(fs::ModifiedAt(path), path);
	}
	if (kept.size() <= LOGS_KEPT)
		return;

	std::sort(kept.begin(), kept.end());
	for (size_t i = 0; i + LOGS_KEPT < kept.size(); i++)
		fs::DeleteSingleFileOrEmptyDirectory(kept[i].second);
}

}

void FileLogOutput::setFile(const std::string &filename, s64 file_size_max)
{
	m_path = filename;
	m_size_max = file_size_max;
	m_written = 0;
	m_mine.clear();

	fs::CreateAllDirs(folderOf(filename));
	archive(filename);
	pruneArchives(folderOf(filename));

	// Intentionally not using open_ofstream() to keep the text mode
	if (!fs::OpenStream(*m_stream.rdbuf(), filename.c_str(), std::ios::out | std::ios::trunc, true, false))
		throw FileNotGoodException("Failed to open log file");
}

void FileLogOutput::logRaw(LogLevel lev, std::string_view line)
{
	m_stream << line << std::endl;

	// Предел нужен серверу: он не перезапускается неделями, и одного файла на
	// запуск ему мало. Считаем написанное, а не спрашиваем размер у системы:
	// строк за минуту тысячи, а обращений к диску это не стоит ни одного.
	if (m_size_max <= 0)
		return;
	m_written += static_cast<s64>(line.size()) + 1;
	if (m_written >= m_size_max)
		roll();
}

void FileLogOutput::roll()
{
	m_stream.close();
	const std::string packed = archive(m_path);
	m_written = 0;

	// Каталог здесь не перечисляем — он говорит вслух, а замок журнала уже
	// взят. Считаем только своё: то, что осталось от прошлых запусков, убрано
	// при заведении журнала.
	if (!packed.empty()) {
		m_mine.push_back(packed);
		while (m_mine.size() > LOGS_KEPT) {
			fs::DeleteSingleFileOrEmptyDirectory(m_mine.front());
			m_mine.pop_front();
		}
	}

	if (!fs::OpenStream(*m_stream.rdbuf(), m_path.c_str(), std::ios::out | std::ios::trunc, true, false))
		throw FileNotGoodException("Failed to reopen log file");
}

StreamLogOutput::StreamLogOutput(std::ostream &stream) :
	m_stream(stream)
{
#if !defined(_WIN32)
	if (&stream == &std::cout)
		is_tty = isatty(STDOUT_FILENO);
	else if (&stream == &std::cerr)
		is_tty = isatty(STDERR_FILENO);
#endif
}

void StreamLogOutput::logRaw(LogLevel lev, std::string_view line)
{
	bool colored_message = (Logger::color_mode == LOG_COLOR_ALWAYS) ||
		(Logger::color_mode == LOG_COLOR_AUTO && is_tty);
	if (colored_message) {
		switch (lev) {
		case LL_ERROR:
			// error is red
			m_stream << "\033[91m";
			break;
		case LL_WARNING:
			// warning is yellow
			m_stream << "\033[93m";
			break;
		case LL_INFO:
			// info is a bit dark
			m_stream << "\033[37m";
			break;
		case LL_VERBOSE:
		case LL_TRACE:
			// verbose is darker than info
			m_stream << "\033[2m";
			break;
		default:
			// action is white
			colored_message = false;
		}
	}

	m_stream << line << std::endl;

	if (colored_message) {
		// reset to white color
		m_stream << "\033[0m";
	}
}

void StreamProxy::fix_stream_state(std::ostream &os)
{
	std::ios::iostate state = os.rdstate();
	// clear error state so the stream works again
	os.clear();
	if (state & std::ios::eofbit)
		os << "(ostream:eofbit)";
	if (state & std::ios::badbit)
		os << "(ostream:badbit)";
	if (state & std::ios::failbit)
		os << "(ostream:failbit)";
}
