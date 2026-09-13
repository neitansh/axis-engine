// Axis
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 the Axis contributors

#include "net.h"

#include "convert_json.h"
#include <memory>

namespace menu
{

Net::Net() : m_caller(httpfetch_caller_alloc())
{
}

Net::~Net()
{
	httpfetch_caller_free(m_caller);
}

void Net::get(const std::string &url, const std::vector<std::string> &headers,
		long timeout_ms, Callback callback)
{
	HTTPFetchRequest request;
	request.url = url;
	request.method = HTTP_GET;
	request.extra_headers = headers;
	request.timeout = timeout_ms;
	send(request, std::move(callback));
}

void Net::post(const std::string &url, const Json::Value &body,
		const std::vector<std::string> &headers, long timeout_ms, Callback callback)
{
	HTTPFetchRequest request;
	request.url = url;
	request.method = HTTP_POST;
	request.raw_data = fastWriteJson(body);
	request.extra_headers = headers;
	request.extra_headers.emplace_back("Content-Type: application/json");
	request.timeout = timeout_ms;
	send(request, std::move(callback));
}

void Net::fire(const std::string &url, const Json::Value &body,
		const std::vector<std::string> &headers, long timeout_ms)
{
	HTTPFetchRequest request;
	request.url = url;
	request.method = HTTP_POST;
	request.raw_data = fastWriteJson(body);
	request.extra_headers = headers;
	request.extra_headers.emplace_back("Content-Type: application/json");
	request.timeout = timeout_ms;
	request.quiet = true;
	httpfetch_async(request);
}

void Net::send(HTTPFetchRequest &request, Callback callback)
{
	request.caller = m_caller;
	request.request_id = m_next_id++;
	request.connect_timeout = std::min<long>(request.timeout, 3000);
	request.quiet = true;
	m_pending.push_back({request.request_id, std::move(callback)});
	httpfetch_async(request);
}

void Net::poll()
{
	HTTPFetchResult result;
	while (httpfetch_async_get(m_caller, result)) {
		Callback callback;
		for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
			if (it->id == result.request_id) {
				callback = std::move(it->callback);
				m_pending.erase(it);
				break;
			}
		}
		if (!callback)
			continue;

		Answer answer;
		answer.reached = result.succeeded;
		answer.code = result.response_code;
		if (!result.data.empty()) {
			Json::CharReaderBuilder builder;
			const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
			std::string errors;
			if (!reader->parse(result.data.data(), result.data.data() + result.data.size(),
					&answer.body, &errors))
				answer.body = Json::Value();
		}
		callback(answer);
	}
}

}
