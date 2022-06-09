#ifndef UrlParser_hpp
#define UrlParser_hpp

#include "Arduino.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <variant>

#include "../mongoose.h"
#include "ArduinoJson.h"


class UrlParser {
public:
    struct Endpoint;
    struct Response;
    struct Result;
    struct Request;

    using params_t = std::map<std::string, std::string>;
    using callback_t = std::function<Result(const Request &, Response &)>;

    UrlParser(const std::multiset<Endpoint> &endpoints);

    void addEndpoint(
        const mg_str &method, const mg_str &pattern, int priority,
        callback_t callback
    );
    void addEndpoint(const Endpoint &endpoint);
    void clearEndpoints();
    Result match(const mg_http_message &request, Response &response);

private:
    static bool
        matchUrl(std::string_view url, std::string_view pattern, params_t &params);

    std::multiset<Endpoint> m_endpoints;
};

struct UrlParser::Result {
    Result(int code) :
        code(code), success(true), error() {};
    Result(int code, std::string error) :
        code(code), success(false), error(error) {};
    
    int code;
    bool success;
    std::string error;
};

struct UrlParser::Endpoint {
    int priority;
    std::string method;
    std::string pattern;
    callback_t callback;

    Result operator()(const Request &request, Response &response) const
    {
        return callback(request, response);
    };

    int operator<(const Endpoint &other) const
    {
        return priority < other.priority;
    }
};

struct UrlParser::Response {
    JsonVariant data;
    std::string headers;
};

struct UrlParser::Request {
    const mg_http_message &rawMessage;
    const params_t &urlParams;
    JsonVariant data;
};

#endif  // #ifdef UrlParser_hpp