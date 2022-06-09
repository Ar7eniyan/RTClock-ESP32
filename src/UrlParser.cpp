#include "UrlParser.hpp"


UrlParser::UrlParser(const std::multiset<Endpoint> &endpoints) :
m_endpoints(endpoints)
{}

void UrlParser::addEndpoint(
    const mg_str &method, const mg_str &pattern, int priority, callback_t callback
)
{
    m_endpoints.insert(
        {priority, std::string(method.ptr, method.len),
         std::string(pattern.ptr, pattern.len), callback}
    );
}

void UrlParser::addEndpoint(const Endpoint &endpoint)
{
    m_endpoints.insert(endpoint);
}

void UrlParser::clearEndpoints()
{
    m_endpoints.clear();
}

UrlParser::Result UrlParser::match(const mg_http_message &request, Response &response)
{
    params_t params;

    for (auto endpoint : m_endpoints) {
        if (mg_vcmp(&request.method, endpoint.method.c_str()) == 0
            && matchUrl(
                std::string_view(request.uri.ptr, request.uri.len),
                endpoint.pattern, params
            )) {
            
            StaticJsonDocument<1024> requestDoc;
            deserializeJson(requestDoc, request.body.ptr, request.body.len);
            JsonVariant requestJson = requestDoc.as<JsonVariant>();

            Result result = endpoint(
                Request{request, params, requestJson},
                response
            );
            
            if (!result.success) {
                response.data["error"] = result.error;
            }

            return result;
        }
        params.clear();
    }

    return Result(404, "Not Found");
}

// parses url and returns true if it matches pattern
// pattern can contain wildcards: {name} to specify any number of characters
// until the next '/' or end of string
bool UrlParser::matchUrl(
    std::string_view url, std::string_view pattern, params_t &params
)
{
    // remove leading and trailing slash from both url and pattern
    url.remove_prefix(url.front() == '/' ? 1 : 0);
    url.remove_suffix(url.back() == '/' ? 1 : 0);
    pattern.remove_prefix(pattern.front() == '/' ? 1 : 0);
    pattern.remove_suffix(pattern.back() == '/' ? 1 : 0);

    std::string_view::size_type i = 0;
    std::string_view::size_type j = 0;

    while (i < url.length() && j < pattern.length()) {
        if (pattern[j] == '{') {
            // wildcard
            int wildcardEnd = pattern.find('}', j);
            if (wildcardEnd == std::string::npos) {
                return false;
            }

            int urlEnd = url.find('/', i);

            std::string_view name = pattern.substr(j + 1, wildcardEnd - j - 1);
            std::string_view value = url.substr(i, urlEnd - i);
            params.emplace(name, value);
            // if urlEnd is npos, substr seeks to end of string

            if (urlEnd == std::string::npos) {
                // if url ends with a wildcard match, check if pattern ends with the wildcard
                return wildcardEnd == pattern.length() - 1;
            }

            i = urlEnd;
            j = wildcardEnd + 1;  // skip the '}'
        } else if (pattern[j] == url[i]) {
            i++;
            j++;
        } else {
            return false;
        }
    }

    // if i and j reached ends of url and pattern, it's a match
    return i >= url.length() && j >= pattern.length();
}