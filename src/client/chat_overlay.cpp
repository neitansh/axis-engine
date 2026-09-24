// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "chat_overlay.h"

#include "client/client.h"
#include "client/inputhandler.h"
#include "client/localplayer.h"
#include "client/sound.h"
#include "sound_spec.h"
#include "filesys.h"
#include "gettext.h"
#include "gui/mainmenumanager.h"
#include "log.h"
#include "porting.h"
#include "settings.h"
#include "util/string.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

// Сколько реплика висит свёрнутой и за сколько гаснет (сам переход — в
// chat.rcss, тут его длина); сколько строк помнит открытый чат.
static const f32 LINE_LIFETIME = 10.0f;
static const f32 LINE_FADE = 0.8f;
static const size_t HISTORY_LINES = 200;

static const char *kindName(ChatKind kind)
{
	switch (kind) {
	case ChatKind::Player: return "player";
	case ChatKind::Own: return "own";
	case ChatKind::Announce: return "announce";
	case ChatKind::Command: return "command";
	case ChatKind::Log: return "log";
	default: return "system";
	}
}

static void appendEscaped(std::string &out, const std::wstring &text)
{
	for (char c : wide_to_utf8(text)) {
		switch (c) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		default: out += c;
		}
	}
}

// Ссылка в тексте: от http(s):// до пробела; замыкающая точка или скобка
// — знак препинания, не ссылка.
static size_t linkEnd(const std::wstring &chars, size_t from)
{
	size_t end = from;
	while (end < chars.size() && !iswspace(chars[end]))
		++end;
	while (end > from && wcschr(L".,;:!?)]}\"'", chars[end - 1]))
		--end;
	return end;
}

static bool linkAt(const std::wstring &chars, size_t i)
{
	return chars.compare(i, 7, L"http://") == 0 || chars.compare(i, 8, L"https://") == 0;
}

// Цвета из кодов чата (\x1b(c@#rgb)) — в span'ы: ряд одного цвета — один
// span, цвет по умолчанию — без него, у него цвет темы. Ссылки — span
// класса link с адресом в атрибуте: RmlUi сам их не открывает.
static std::string toRml(const EnrichedString &text)
{
	const std::wstring &chars = text.getString();
	const std::vector<video::SColor> &colors = text.getColors();
	auto color_at = [&](size_t i) {
		return i < colors.size() ? colors[i] : text.getDefaultColor();
	};
	std::string out;
	size_t i = 0;
	while (i < chars.size()) {
		if (linkAt(chars, i)) {
			const size_t end = linkEnd(chars, i);
			const std::wstring url = chars.substr(i, end - i);
			out += "<span class=\"link\" data-url=\"";
			appendEscaped(out, url);
			out += "\">";
			appendEscaped(out, url);
			out += "</span>";
			i = end;
			continue;
		}
		const video::SColor color = color_at(i);
		size_t j = i;
		while (j < chars.size() && color_at(j) == color && !linkAt(chars, j))
			++j;
		const bool plain = color == text.getDefaultColor();
		if (!plain) {
			char buf[48];
			snprintf(buf, sizeof(buf), "<span style=\"color: #%02x%02x%02x\">",
					color.getRed(), color.getGreen(), color.getBlue());
			out += buf;
		}
		appendEscaped(out, chars.substr(i, j - i));
		if (!plain)
			out += "</span>";
		i = j;
	}
	return out;
}

// «/td night start [<сложность>] | dawn | speed <N>»: у каждой формы слово
// на месте набираемого аргумента — span.current.
static std::string usageRml(const ChatPrompt::Suggestions &offer)
{
	std::string out = "/";
	appendEscaped(out, offer.command);
	const std::wstring &params = offer.params;
	size_t form_start = 0;
	bool first_form = true;
	while (form_start <= params.size()) {
		size_t form_end = params.find(L'|', form_start);
		if (form_end == std::wstring::npos)
			form_end = params.size();
		const std::wstring form = params.substr(form_start, form_end - form_start);
		form_start = form_end + 1;

		// Пробел — внутри span: текст из одних пробелов между тегами парсер
		// RmlUi выбрасывает.
		if (!first_form)
			out += "<span class=\"or\"> |</span>";
		first_form = false;

		u32 index = 0;
		size_t start = 0;
		while (start < form.size()) {
			while (start < form.size() && form[start] == L' ')
				++start;
			size_t end = form.find(L' ', start);
			if (end == std::wstring::npos)
				end = form.size();
			if (start < end) {
				const bool current = ++index == offer.argument;
				out += current ? "<span class=\"current\"> " : " ";
				appendEscaped(out, form.substr(start, end - start));
				if (current)
					out += "</span>";
			}
			start = end + 1;
		}
	}
	return out;
}

void ChatOverlay::LinkListener::ProcessEvent(Rml::Event &event)
{
	Rml::Element *target = event.GetTargetElement();
	if (!target || !target->HasAttribute("data-url"))
		return;
	if (!event.GetParameter<bool>("ctrl_key", false))
		return;
	porting::open_url(target->GetAttribute<Rml::String>("data-url", ""));
}

ChatOverlay::ChatOverlay(ui::Host &host, MyEventReceiver *receiver, Client *client,
		ChatBackend *backend) :
	m_host(host),
	m_receiver(receiver),
	m_client(client),
	m_backend(backend)
{
	m_theme_dirs.push_back(porting::path_share + DIR_DELIM "client" DIR_DELIM "ui"
			DIR_DELIM "menu");
	if (!m_host.ok())
		return;
	m_context = m_host.createContext("chat");
	if (!m_context)
		return;
	m_placeholder = strgettext("Write a message…");
	loadDocument();
	m_backend->setChatSink([this](const ChatLine &line) { m_incoming.push_back(line); });
}

ChatOverlay::~ChatOverlay()
{
	m_backend->setChatSink(nullptr);
	close();
	if (m_document)
		m_document->Close();
	if (m_context)
		m_host.removeContext("chat");
}

std::string ChatOverlay::themeFile(const std::string &name) const
{
	std::string path;
	for (const std::string &dir : m_theme_dirs) {
		path = dir;
		path.append(DIR_DELIM).append(name);
		if (fs::IsFile(path))
			return path;
	}
	errorstream << "ChatOverlay: theme file \"" << name << "\" is missing" << std::endl;
	return path;
}

void ChatOverlay::loadDocument()
{
	Rml::DataModelConstructor model = m_context->CreateDataModel("chat");
	if (!model) {
		errorstream << "ChatOverlay: data model could not be created" << std::endl;
		return;
	}
	if (auto line = model.RegisterStruct<Line>()) {
		line.RegisterMember("kind", &Line::kind);
		line.RegisterMember("name", &Line::name);
		line.RegisterMember("html", &Line::html);
		line.RegisterMember("count", &Line::count);
		line.RegisterMember("old", &Line::old);
		line.RegisterMember("gone", &Line::gone);
	}
	model.RegisterArray<std::vector<Line>>();
	if (auto option = model.RegisterStruct<Option>()) {
		option.RegisterMember("text", &Option::text);
		option.RegisterMember("detail", &Option::detail);
		option.RegisterMember("chosen", &Option::chosen);
	}
	model.RegisterArray<std::vector<Option>>();
	model.Bind("options", &m_options);
	model.Bind("more", &m_more);
	model.Bind("ghost", &m_ghost);
	model.Bind("ghost_fits", &m_ghost_fits);
	model.Bind("usage", &m_usage);
	model.Bind("about", &m_about);
	model.Bind("lines", &m_lines);
	model.Bind("draft", &m_draft);
	model.Bind("placeholder", &m_placeholder);
	model.Bind("open", &m_open);
	model.Bind("empty", &m_empty);
	model.Bind("none", &m_none);
	m_model = model.GetModelHandle();

	m_document = ui::Host::loadDocument(*m_context, themeFile("chat.rml"));
	if (!m_document) {
		errorstream << "ChatOverlay: chat.rml failed to load" << std::endl;
		return;
	}
	m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	m_document->AddEventListener(Rml::EventId::Click, &m_links);
}

void ChatOverlay::open(const std::wstring &initial)
{
	if (!m_document || m_open)
		return;
	// Пока камеру ведёт сервер (сцена, поимка), игрок не хозяин себе: чат
	// не открывается, но приходящее видно.
	LocalPlayer *player = m_client->getEnv().getLocalPlayer();
	if (player && player->look_locked)
		return;
	m_open = true;
	g_rml_menu_open = true;
	m_receiver->setUiReceiver(this);
	m_document->SetClass("open", true);
	setDraft(initial);
	m_focus_pending = true;
	m_scroll_pending = true;
	m_model.DirtyVariable("open");
}

void ChatOverlay::close()
{
	if (!m_open)
		return;
	m_open = false;
	g_rml_menu_open = false;
	m_receiver->setUiReceiver(nullptr);
	m_draft.clear();
	m_model.DirtyVariable("draft");
	if (m_document) {
		m_document->SetClass("open", false);
		if (Rml::Element *draft = draftElement())
			draft->Blur();
	}
	m_focus_pending = false;
	m_model.DirtyVariable("open");
}

void ChatOverlay::setVisible(bool visible)
{
	m_visible = visible;
	if (m_document)
		m_document->SetClass("hidden", !visible);
}

void ChatOverlay::addLine(const ChatLine &line)
{
	Line out;
	out.kind = kindName(line.kind);
	out.name = wide_to_utf8(line.name.getString());
	out.html = toRml(line.text);
	// Одно и то же подряд — одна строка со счётчиком, а не столбик.
	if (!m_lines.empty()) {
		Line &last = m_lines.back();
		if (last.kind == out.kind && last.name == out.name && last.html == out.html) {
			++last.count;
			last.age = 0.0f;
			m_lines_dirty = true;
			m_scroll_pending = true;
			return;
		}
	}
	// Чужая реплика — со звуком, если игра положила chat_message; без него
	// тихо (звук по имени, которого нет, — не ошибка).
	if (line.kind == ChatKind::Player && m_client->sound())
		m_client->sound()->playSound(0, SoundSpec("chat_message", 1.0f));
	m_lines.push_back(std::move(out));
	while (m_lines.size() > HISTORY_LINES)
		m_lines.erase(m_lines.begin());
	m_lines_dirty = true;
	m_scroll_pending = true;
}

void ChatOverlay::step(f32 dtime)
{
	if (!m_document)
		return;
	for (const ChatLine &line : m_incoming)
		addLine(line);
	m_incoming.clear();

	// Свёрнутым виден хвост: столько строк, сколько просит настройка, и
	// каждая — недолго. Старые гаснут, погасшие остаются в истории для
	// открытого чата.
	const size_t shown = rangelim(g_settings->getU32("recent_chat_messages"), 2, 20);
	size_t fresh = 0;
	for (size_t i = m_lines.size(); i-- > 0;) {
		Line &line = m_lines[i];
		line.age += dtime;
		const bool old = line.age > LINE_LIFETIME || fresh >= shown;
		const bool gone = line.age > LINE_LIFETIME + LINE_FADE || fresh >= shown;
		if (!old)
			++fresh;
		if (old != line.old || gone != line.gone) {
			line.old = old;
			line.gone = gone;
			m_lines_dirty = true;
		}
	}
	if (m_lines_dirty) {
		m_model.DirtyVariable("lines");
		m_lines_dirty = false;
	}
	const bool empty = fresh == 0;
	if (empty != m_empty) {
		m_empty = empty;
		m_model.DirtyVariable("empty");
	}
	const bool none = m_lines.empty();
	if (none != m_none) {
		m_none = none;
		m_model.DirtyVariable("none");
	}

	refreshSuggestions();

	m_host.update(*m_context);

	// Серое дописывание лежит поверх поля и не знает его прокрутки: когда
	// набранное длиннее поля, оно легло бы не туда.
	if (m_open) {
		Rml::Element *draft = draftElement();
		Rml::Element *typed = m_document->GetElementById("ghost-typed");
		const bool fits = !draft || !typed ||
				typed->GetOffsetWidth() + 24.0f < draft->GetClientWidth();
		if (fits != m_ghost_fits) {
			m_ghost_fits = fits;
			m_model.DirtyVariable("ghost_fits");
		}
	}

	if (m_focus_pending) {
		if (Rml::Element *draft = draftElement()) {
			draft->Focus();
			if (auto *input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(draft)) {
				const int end = static_cast<int>(m_draft.size());
				input->SetSelectionRange(end, end);
			}
		}
		m_focus_pending = false;
	}
	if (m_scroll_pending) {
		scrollToBottom();
		m_scroll_pending = false;
	}
}

void ChatOverlay::render()
{
	if (m_context && m_document && m_visible)
		m_host.render(*m_context);
}

Rml::Element *ChatOverlay::draftElement() const
{
	return m_document ? m_document->GetElementById("draft") : nullptr;
}

Rml::Element *ChatOverlay::historyElement() const
{
	return m_document ? m_document->GetElementById("history") : nullptr;
}

void ChatOverlay::scrollToBottom()
{
	if (Rml::Element *history = historyElement())
		history->SetScrollTop(history->GetScrollHeight());
}

void ChatOverlay::scrollHistory(float rows)
{
	Rml::Element *history = historyElement();
	if (!history)
		return;
	const float line = m_document->ResolveLength(Rml::NumericValue(1.4f * 15.0f, Rml::Unit::DP));
	history->SetScrollTop(history->GetScrollTop() + rows * line);
}

void ChatOverlay::setDraft(const std::wstring &text)
{
	m_draft = wide_to_utf8(text);
	m_model.DirtyVariable("draft");
	m_focus_pending = true;
}

void ChatOverlay::send()
{
	const std::wstring line = utf8_to_wide(m_draft);
	close();
	if (line.empty())
		return;
	m_backend->getPrompt().addToHistory(line);
	m_client->typeChatMessage(line);
}

std::vector<ChatPrompt::CommandInfo> ChatOverlay::commandInfos() const
{
	// Подписи встроенных команд приходят с кодами перевода (S() в builtin).
	std::vector<ChatPrompt::CommandInfo> commands;
	for (const auto &command : m_client->getChatCommands()) {
		commands.push_back({utf8_to_wide(command.name),
				unescape_translate(utf8_to_wide(command.params)),
				unescape_translate(utf8_to_wide(command.description))});
	}
	return commands;
}

// Подсказки считаются от конца строки: курсор в середине — правка уже
// набранного, предлагать там нечего.
bool ChatOverlay::cursorAtEnd() const
{
	auto *input = rmlui_dynamic_cast<Rml::ElementFormControlInput *>(draftElement());
	if (!input)
		return true;
	int start = 0, end = 0;
	input->GetSelection(&start, &end, nullptr);
	return start == end && static_cast<size_t>(end) >= input->GetValue().size();
}

void ChatOverlay::refreshSuggestions()
{
	std::vector<Option> options;
	int more = 0;
	Rml::String ghost, usage, about;

	if (m_open && !m_draft.empty() && cursorAtEnd()) {
		ChatPrompt &prompt = m_backend->getPrompt();
		prompt.replace(utf8_to_wide(m_draft));
		prompt.updateSuggestions(commandInfos(), m_client->getConnectedPlayerNames());
		const ChatPrompt::Suggestions &offer = prompt.getSuggestions();

		// Окно списка идёт за выбором, как в консоли.
		const size_t shown_max = 6;
		const size_t count = offer.options.size();
		const size_t shown = std::min(shown_max, count);
		size_t first = offer.chosen >= shown ? offer.chosen - shown + 1 : 0;
		if (first + shown > count)
			first = count - shown;
		if (count > 1 || (count == 1 && !str_equal(offer.options[0], offer.typed, true))) {
			for (size_t i = first; i < first + shown; i++) {
				Option option;
				std::wstring text = offer.options[i];
				if (!text.empty() && text[0] == L'/')
					text.erase(0, 1);
				option.text = wide_to_utf8(text);
				if (i < offer.details.size())
					option.detail = wide_to_utf8(offer.details[i]);
				option.chosen = i == offer.chosen;
				options.push_back(std::move(option));
			}
			more = static_cast<int>(count - shown);
		}

		std::wstring rest;
		if (count > 0) {
			const std::wstring &chosen = offer.options[offer.chosen];
			if (chosen.size() > offer.typed.size() &&
					str_starts_with(chosen, offer.typed, true))
				rest = chosen.substr(offer.typed.size());
		} else if (offer.typed.empty()) {
			rest = offer.hint;
		}
		ghost = wide_to_utf8(rest);

		if (!offer.command.empty()) {
			usage = usageRml(offer);
			about = wide_to_utf8(offer.description);
		} else if (count > 0 && offer.chosen < offer.details.size() &&
				m_draft[0] == '/' && m_draft.find(' ') == Rml::String::npos) {
			// Ещё набирается сама команда: её форма — у выбранной.
			ChatPrompt::Suggestions chosen;
			chosen.command = offer.options[offer.chosen].substr(1);
			chosen.params = offer.details[offer.chosen];
			usage = usageRml(chosen);
			for (const ChatPrompt::CommandInfo &command : commandInfos()) {
				if (command.name == chosen.command) {
					about = wide_to_utf8(command.description);
					break;
				}
			}
		}
	}

	if (!(options == m_options)) {
		m_options = std::move(options);
		m_model.DirtyVariable("options");
	}
	if (more != m_more) {
		m_more = more;
		m_model.DirtyVariable("more");
	}
	if (ghost != m_ghost) {
		m_ghost = ghost;
		m_model.DirtyVariable("ghost");
	}
	if (usage != m_usage) {
		m_usage = usage;
		m_model.DirtyVariable("usage");
	}
	if (about != m_about) {
		m_about = about;
		m_model.DirtyVariable("about");
	}
}

// Дополнение — то же, что в консоли: сперва то, что предложено, иначе
// команда или её аргумент, иначе имя игрока. Дописанное слово в конце
// строки получает пробел: сразу видно, что ждёт следующий аргумент.
void ChatOverlay::completeDraft()
{
	ChatPrompt &prompt = m_backend->getPrompt();
	const std::vector<ChatPrompt::CommandInfo> commands = commandInfos();
	const auto &names = m_client->getConnectedPlayerNames();
	prompt.replace(utf8_to_wide(m_draft));
	prompt.updateSuggestions(commands, names);
	if (prompt.applySuggestion()) {
		const std::wstring line = prompt.getLine();
		if (!line.empty() && line.back() != L' ' && line[0] == L'/')
			prompt.input(L' ');
	} else if (!prompt.commandCompletion(commands, names)) {
		prompt.nickCompletion(names);
	}
	setDraft(prompt.getLine());
}

bool ChatOverlay::OnEvent(const SEvent &event)
{
	if (!m_open || !m_context)
		return false;
	switch (event.EventType) {
	case EET_KEY_INPUT_EVENT: {
		if (!event.KeyInput.PressedDown)
			break;
		ChatPrompt &prompt = m_backend->getPrompt();
		switch (event.KeyInput.Key) {
		case KEY_ESCAPE:
			close();
			return true;
		case KEY_RETURN:
			send();
			return true;
		case KEY_UP:
		case KEY_DOWN: {
			const bool up = event.KeyInput.Key == KEY_UP;
			if (m_options.size() > 1) {
				prompt.cycleSuggestion(up ? -1 : 1);
				return true;
			}
			prompt.replace(utf8_to_wide(m_draft));
			if (up)
				prompt.historyPrev();
			else
				prompt.historyNext();
			setDraft(prompt.getLine());
			return true;
		}
		case KEY_TAB:
			completeDraft();
			return true;
		case KEY_PRIOR:
			scrollHistory(-6);
			return true;
		case KEY_NEXT:
			scrollHistory(6);
			return true;
		default:
			break;
		}
		break;
	}
	case EET_MOUSE_INPUT_EVENT:
	case EET_STRING_INPUT_EVENT:
		break;
	default:
		return false;
	}
	m_host.feedEvent(*m_context, event);
	// Пока чат открыт, клавиши — его: игре ход не достаётся.
	return true;
}
