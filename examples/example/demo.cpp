
#include "http.hpp"
#include <view.html.hpp>

static std::vector<std::string> messages;

struct MyWebsockHandler : public WebsockClientHandler {
    void onConnect() override {
        puts("Connect!");
    }

    void onTextMessage(const std::string& message) override {
        puts("Text message!");
        std::cout << "  -> " << message << std::endl;
    }

    void onBinaryMessage(const uint8_t* message, const size_t len) override {
        puts("Binary message!");
    }

    void onDisconnect() override {
        puts("Disconnect!");
    }
};

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

    s.when("/messages/static")
        ->requested([](const HttpRequest&) -> HttpResponse {
            return {200, templates::ViewTemplate(messages)};
        });

    s.websocket("/ws")->handleWith<MyWebsockHandler>();

    s.startListening(8088);
    return 0;
}
