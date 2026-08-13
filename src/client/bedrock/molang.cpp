// Copyright (C) 2026 the-axis
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "molang.h"
#include "log.h"
#include "irrMath.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bedrock
{

struct Molang::Node
{
	enum class Kind : u8 {
		NUMBER,     ///< число
		ANIM_TIME,  ///< query.anim_time
		NEG,        ///< унарный минус
		ADD, SUB, MUL, DIV,
		CALL,       ///< вызов функции Math.*
	};

	enum class Func : u8 {
		SIN, COS, TAN, ABS, FLOOR, CEIL, ROUND, SQRT, EXP, LN,
		POW, MIN, MAX, MOD, CLAMP, LERP,
	};

	Kind kind = Kind::NUMBER;
	Func func = Func::SIN;
	f32 number = 0.0f;
	std::vector<std::shared_ptr<Node>> args;
};

namespace
{

using Node = Molang::Node;
using NodePtr = std::shared_ptr<Node>;

NodePtr makeNumber(f32 v)
{
	auto n = std::make_shared<Node>();
	n->kind = Node::Kind::NUMBER;
	n->number = v;
	return n;
}

/// Разбор рекурсивным спуском. Грамматика ровно та, что нужна выражениям из
/// анимаций: сумма → произведение → множитель.
class Parser
{
public:
	Parser(const std::string &src) : m_src(src) {}

	NodePtr parse()
	{
		NodePtr expr = sum();
		skipSpace();
		// Молang разрешает завершать выражение точкой с запятой.
		if (m_pos < m_src.size() && m_src[m_pos] == ';')
			++m_pos;
		skipSpace();
		if (m_pos < m_src.size())
			m_failed = true;
		return expr;
	}

	bool failed() const { return m_failed; }

private:
	const std::string &m_src;
	size_t m_pos = 0;
	bool m_failed = false;

	void skipSpace()
	{
		while (m_pos < m_src.size() && std::isspace(static_cast<unsigned char>(m_src[m_pos])))
			++m_pos;
	}

	bool eat(char c)
	{
		skipSpace();
		if (m_pos < m_src.size() && m_src[m_pos] == c) {
			++m_pos;
			return true;
		}
		return false;
	}

	/// Слово с точками: Math.sin, query.anim_time, variable.foo.
	std::string word()
	{
		skipSpace();
		size_t start = m_pos;
		while (m_pos < m_src.size()) {
			char c = m_src[m_pos];
			if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')
				++m_pos;
			else
				break;
		}
		std::string out = m_src.substr(start, m_pos - start);
		for (char &c : out) {
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}
		return out;
	}

	NodePtr sum()
	{
		NodePtr left = product();
		for (;;) {
			skipSpace();
			if (eat('+')) {
				auto n = std::make_shared<Node>();
				n->kind = Node::Kind::ADD;
				n->args = {left, product()};
				left = n;
			} else if (eat('-')) {
				auto n = std::make_shared<Node>();
				n->kind = Node::Kind::SUB;
				n->args = {left, product()};
				left = n;
			} else {
				return left;
			}
		}
	}

	NodePtr product()
	{
		NodePtr left = factor();
		for (;;) {
			skipSpace();
			if (eat('*')) {
				auto n = std::make_shared<Node>();
				n->kind = Node::Kind::MUL;
				n->args = {left, factor()};
				left = n;
			} else if (eat('/')) {
				auto n = std::make_shared<Node>();
				n->kind = Node::Kind::DIV;
				n->args = {left, factor()};
				left = n;
			} else {
				return left;
			}
		}
	}

	NodePtr factor()
	{
		skipSpace();
		if (m_pos >= m_src.size()) {
			m_failed = true;
			return makeNumber(0.0f);
		}

		if (eat('-')) {
			auto n = std::make_shared<Node>();
			n->kind = Node::Kind::NEG;
			n->args = {factor()};
			return n;
		}
		if (eat('+'))
			return factor();

		if (eat('(')) {
			NodePtr inner = sum();
			if (!eat(')'))
				m_failed = true;
			return inner;
		}

		char c = m_src[m_pos];
		if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
			size_t start = m_pos;
			while (m_pos < m_src.size() &&
					(std::isdigit(static_cast<unsigned char>(m_src[m_pos]))
					|| m_src[m_pos] == '.'))
				++m_pos;
			return makeNumber(static_cast<f32>(
					std::atof(m_src.substr(start, m_pos - start).c_str())));
		}

		return named();
	}

	/// Имя: либо запрос (значение), либо вызов функции со скобками.
	NodePtr named()
	{
		const std::string name = word();
		if (name.empty()) {
			m_failed = true;
			return makeNumber(0.0f);
		}

		skipSpace();
		const bool call = m_pos < m_src.size() && m_src[m_pos] == '(';
		if (!call) {
			if (name == "query.anim_time" || name == "q.anim_time"
					|| name == "anim_time") {
				auto n = std::make_shared<Node>();
				n->kind = Node::Kind::ANIM_TIME;
				return n;
			}
			// Всё прочее — состояние мира, которого у анимации нет.
			m_failed = true;
			return makeNumber(0.0f);
		}

		auto n = std::make_shared<Node>();
		n->kind = Node::Kind::CALL;
		if (!funcByName(name, n->func))
			m_failed = true;

		eat('(');
		skipSpace();
		if (!eat(')')) {
			for (;;) {
				n->args.push_back(sum());
				skipSpace();
				if (eat(','))
					continue;
				if (!eat(')'))
					m_failed = true;
				break;
			}
		}
		return n;
	}

	static bool funcByName(const std::string &name, Node::Func &out)
	{
		struct Entry { const char *name; Node::Func func; };
		static const Entry table[] = {
			{"math.sin", Node::Func::SIN},     {"math.cos", Node::Func::COS},
			{"math.tan", Node::Func::TAN},     {"math.abs", Node::Func::ABS},
			{"math.floor", Node::Func::FLOOR}, {"math.ceil", Node::Func::CEIL},
			{"math.round", Node::Func::ROUND}, {"math.sqrt", Node::Func::SQRT},
			{"math.exp", Node::Func::EXP},     {"math.ln", Node::Func::LN},
			{"math.pow", Node::Func::POW},     {"math.min", Node::Func::MIN},
			{"math.max", Node::Func::MAX},     {"math.mod", Node::Func::MOD},
			{"math.clamp", Node::Func::CLAMP}, {"math.lerp", Node::Func::LERP},
		};
		for (const auto &entry : table) {
			if (name == entry.name) {
				out = entry.func;
				return true;
			}
		}
		return false;
	}
};

f32 evalNode(const Node &node, f32 anim_time)
{
	auto arg = [&](size_t i) -> f32 {
		return i < node.args.size() ? evalNode(*node.args[i], anim_time) : 0.0f;
	};

	switch (node.kind) {
	case Node::Kind::NUMBER:    return node.number;
	case Node::Kind::ANIM_TIME: return anim_time;
	case Node::Kind::NEG:       return -arg(0);
	case Node::Kind::ADD:       return arg(0) + arg(1);
	case Node::Kind::SUB:       return arg(0) - arg(1);
	case Node::Kind::MUL:       return arg(0) * arg(1);
	case Node::Kind::DIV: {
		const f32 d = arg(1);
		return d == 0.0f ? 0.0f : arg(0) / d;
	}
	case Node::Kind::CALL:
		switch (node.func) {
		// Углы в Bedrock везде в градусах, в том числе здесь.
		case Node::Func::SIN:   return std::sin(arg(0) * core::DEGTORAD);
		case Node::Func::COS:   return std::cos(arg(0) * core::DEGTORAD);
		case Node::Func::TAN:   return std::tan(arg(0) * core::DEGTORAD);
		case Node::Func::ABS:   return std::abs(arg(0));
		case Node::Func::FLOOR: return std::floor(arg(0));
		case Node::Func::CEIL:  return std::ceil(arg(0));
		case Node::Func::ROUND: return std::round(arg(0));
		case Node::Func::SQRT:  return std::sqrt(std::max(0.0f, arg(0)));
		case Node::Func::EXP:   return std::exp(arg(0));
		case Node::Func::LN: {
			const f32 v = arg(0);
			return v > 0.0f ? std::log(v) : 0.0f;
		}
		case Node::Func::POW:   return std::pow(arg(0), arg(1));
		case Node::Func::MIN:   return std::min(arg(0), arg(1));
		case Node::Func::MAX:   return std::max(arg(0), arg(1));
		case Node::Func::MOD: {
			const f32 d = arg(1);
			return d == 0.0f ? 0.0f : std::fmod(arg(0), d);
		}
		case Node::Func::CLAMP: return core::clamp(arg(0), arg(1), arg(2));
		case Node::Func::LERP: {
			const f32 a = arg(0), b = arg(1), t = core::clamp(arg(2), 0.0f, 1.0f);
			return a + (b - a) * t;
		}
		}
		return 0.0f;
	}
	return 0.0f;
}

/// Зависит ли поддерево от времени.
bool dependsOnTime(const Node &node)
{
	if (node.kind == Node::Kind::ANIM_TIME)
		return true;
	for (const auto &arg : node.args) {
		if (dependsOnTime(*arg))
			return true;
	}
	return false;
}

} // namespace

Molang Molang::constant(f32 value)
{
	Molang out;
	out.m_constant = true;
	out.m_value = value;
	return out;
}

Molang Molang::parse(const std::string &source)
{
	Parser parser(source);
	NodePtr root = parser.parse();

	if (parser.failed()) {
		warningstream << "Bedrock: не понято выражение \"" << source
				<< "\", беру ноль" << std::endl;
		return constant(0.0f);
	}

	Molang out;
	if (!dependsOnTime(*root))
		return constant(evalNode(*root, 0.0f));

	out.m_root = root;
	out.m_constant = false;
	return out;
}

f32 Molang::eval(f32 anim_time) const
{
	if (m_constant)
		return m_value;
	return evalNode(*m_root, anim_time);
}

} // namespace bedrock
