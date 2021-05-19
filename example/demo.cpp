
#include "http.hpp"

static std::vector<std::string> messages;

int main(int argc, char const *argv[])
{
    HttpServer s;

    messages.push_back("Test message");

    s.when("/")->serveFile("index.html");
    s.whenMatching("/static/[^/]+")->serveFromFolder("static");

    s.when("/messages")
        ->posted([](const HttpRequest& req) {
            auto msg = req.json().toObject();
            messages.push_back(msg["message"].toString());
            return HttpResponse{201};
        })
        ->requested([](const HttpRequest& req) {
            miniJson::Json::_object res;
            res["messages"] = messages;
            return HttpResponse{200, res};
        });

    s.startListening(8088);
    return 0;
}
