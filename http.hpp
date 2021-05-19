#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

// json support (Currently uses MiniJson)
#define TINYHTTP_JSON

// websocket support
#define TINYHTTP_WS

#include <cstdint>
#include <string>
#include <memory>
#include <stdexcept>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <regex>
#include <map>
#include <iostream>
#include <fstream>

#ifdef TINYHTTP_JSON
#  include <json.h>
#endif

class MessageBuilder : public std::vector<uint8_t> {
    public:
        template<typename T>
        void write(T s) {
            write(&s, sizeof(T));
        }

        void write(std::string s) {
            write(s.data(), s.size());
        }

        void write(const char* s) {
            write(s, strlen(s));
        }

        void writeCRLF() { write("\r\n", 2); }

        void write(const void* data, const size_t len) {
            size_t ogsize = size();
            resize(ogsize + len);
            memcpy(this->data() + ogsize, data, len);
        }
};

class HttpMessageCommon {
    protected:
        std::map<std::string, std::string> mHeaders;
        std::string mContent;

    public:
        std::string& operator[](std::string i) {
            std::transform(i.begin(), i.end(), i.begin(), [](unsigned char c){ return std::tolower(c); });

            auto f = mHeaders.find(i);
            if (f == mHeaders.end())
                mHeaders.insert(std::pair<std::string,std::string>(i, ""));

            return mHeaders.find(i)->second;
        }

        std::string operator[](std::string i) const {
            std::transform(i.begin(), i.end(), i.begin(), [](unsigned char c){ return std::tolower(c); });

            auto f = mHeaders.find(i);
            if (f == mHeaders.end())
                return "";

            return mHeaders.find(i)->second;
        }

        void setContent(std::string content) {
            mContent = std::move(content);
            (*this)["Content-Length"] = std::to_string(mContent.size());
        }

        const auto& content() const noexcept { return mContent; }
};

enum class HttpRequestMethod { GET,POST,PUT,DELETE,OPTIONS,UNKNOWN };

class HttpRequest : public HttpMessageCommon {
    HttpRequestMethod mMethod = HttpRequestMethod::UNKNOWN;
    std::string path, query;

    #ifdef TINYHTTP_JSON
    miniJson::Json mContentJson;
    #endif

    public:
        bool parse(const char* raw, const ssize_t len);

        const HttpRequestMethod& getMethod() const noexcept { return mMethod; }
        const std::string& getPath() const noexcept { return path; }
        const std::string& getQuery() const noexcept { return query; }

        #ifdef TINYHTTP_JSON
        const miniJson::Json& json() const noexcept { return mContentJson; }
        #endif
};

#ifdef TINYHTTP_WS
struct ICanRequestProtocolHandover {
    virtual ~ICanRequestProtocolHandover() = default;
    virtual void acceptHandover(short& serverSock, short& clientSock) = 0;
};
#endif

class HttpResponse : public HttpMessageCommon {
    unsigned mStatusCode = 400;
    ICanRequestProtocolHandover* mHandover = nullptr;

    public:
        HttpResponse(const unsigned statusCode) : mStatusCode{statusCode} {
            (*this)["Server"] = "tinyHTTP_0.1";

            if (statusCode >= 200)
                (*this)["Content-Length"] = "0";
        }

        HttpResponse(const unsigned statusCode, std::string contentType, std::string content)
            : HttpResponse{statusCode} {
            (*this)["Content-Type"] = contentType;
            setContent(content);
        }

        void requestProtocolHandover(ICanRequestProtocolHandover* newOwner) {
            mHandover = newOwner;
        }

        bool acceptProtocolHandover(ICanRequestProtocolHandover** outTarget) {
            if (outTarget && mHandover)
            {
                *outTarget = mHandover;
                return true;
            }

            return false;
        }

        #ifdef TINYHTTP_JSON
        HttpResponse(const unsigned statusCode, const miniJson::Json& json)
            : HttpResponse{statusCode, "application/json", json.serialize()} {}
        #endif

        MessageBuilder buildMessage() {
            MessageBuilder b;

            b.write("HTTP/1.1 " + std::to_string(mStatusCode));
            b.writeCRLF();

            for (auto& h : mHeaders)
                if (!h.second.empty())
                    b.write(h.first + ": " + h.second + "\r\n");

            b.writeCRLF();
            b.write(mContent);

            return b;
        }
};

struct HandlerBuilder {
    virtual ~HandlerBuilder() = default;

    virtual std::unique_ptr<HttpResponse> process(const HttpRequest& req) {
        return nullptr;
    }
};

class WebsockClientHandler {
    public:
        virtual void onConnect() {}
        virtual void onDisconnect() {}
        virtual void onTextMessage(const std::string& message) {}
        virtual void onBinaryMessage(const uint8_t* message, const size_t len) {}
};

class WebsockHandlerBuilder : public HandlerBuilder, public ICanRequestProtocolHandover {
    struct Factory {
		virtual ~Factory() = default;
        virtual WebsockClientHandler* makeInstance() = 0;
    };

    template<typename T>
    struct FactoryT : Factory {
        virtual WebsockClientHandler* makeInstance() {
            return new T();
        }
    };

    std::unique_ptr<Factory> mFactory;

    public:
        WebsockHandlerBuilder()
            : mFactory{new FactoryT<WebsockClientHandler>} {}

        template<typename T>
        void handleWith() {
            mFactory = std::unique_ptr<Factory>(new FactoryT<T>);
        }

        virtual std::unique_ptr<HttpResponse> process(const HttpRequest& req) override;

        void acceptHandover(short& serverSock, short& clientSock) override;
};

class HttpHandlerBuilder : public HandlerBuilder {
    typedef std::function<HttpResponse(const HttpRequest&)> HandlerFunc;

    std::map<HttpRequestMethod, HandlerFunc> mHandlers;

    static bool isSafeFilename(const std::string& name, bool allowSlash);
    static std::string getMimeType(std::string name);

    public:
        HttpHandlerBuilder* posted(HandlerFunc h) {
            mHandlers.insert(std::pair<HttpRequestMethod, HandlerFunc>(HttpRequestMethod::POST, std::move(h)));
            return this;
        }

        HttpHandlerBuilder* requested(HandlerFunc h) {
            mHandlers.insert(std::pair<HttpRequestMethod, HandlerFunc>(HttpRequestMethod::GET, std::move(h)));
            return this;
        }

        HttpHandlerBuilder* serveFile(std::string name) {
            return requested([name](const HttpRequest&) {
                std::ifstream t(name);
                if (t.is_open())
                    return HttpResponse{200, getMimeType(name), std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>())};
                else {
                    std::cerr << "Could not locate file: " << name << std::endl;
                    return HttpResponse{404, "text/plain", "The requested file is missing from the server"};
                }
            });
        }

        HttpHandlerBuilder* serveFromFolder(std::string dir) {
            return requested([dir](const HttpRequest&q) {
                std::string fname = q.getPath();
                fname = fname.substr(fname.rfind('/')+1);

                if (isSafeFilename(fname, false)) {
                    fname = dir + "/" + fname;

                    std::ifstream t(fname);
                    if (t.is_open())
                        return HttpResponse{200, getMimeType(fname), std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>())};
                    else
                        std::cerr << "Could not locate file: " << fname << std::endl;
                }

                return HttpResponse{404, "text/plain", "The requested file is missing from the server"};
            });
        }

        template<typename T>
        HttpHandlerBuilder* posted(T x) { return posted(HandlerFunc(x)); }

        template<typename T>
        HttpHandlerBuilder* requested(T x) { return requested(HandlerFunc(x)); }

        std::unique_ptr<HttpResponse> process(const HttpRequest& req) override {
            auto h = mHandlers.find(req.getMethod());

            if (h == mHandlers.end())
                return std::make_unique<HttpResponse>(405, "text/plain", "405 method not allowed");
            else
                return std::make_unique<HttpResponse>(h->second(req));
        }
};

class HttpServer {
    std::vector<std::pair<std::string, std::shared_ptr<HandlerBuilder>>> mHandlers;
    std::vector<std::pair<std::regex, std::shared_ptr<HandlerBuilder>>> mReHandlers;
    MessageBuilder mDefault404Message, mDefault400Message;
    short mSocket = -1;

    std::shared_ptr<HttpResponse> processRequest(std::string key, const HttpRequest& req) {

        try {
            for (auto& x : mHandlers)
                if (x.first == key) {
                    auto res = x.second->process(req);
                    if (res) return res;
                }

            for (auto x : mReHandlers)
                if (std::regex_match(key, x.first)) {
                    auto res = x.second->process(req);
                    if (res) return res;
                }
        } catch (std::exception& e) {
            std::cerr << "Exception while handling request (" << key << "): " << e.what() << std::endl;
            return std::make_shared<HttpResponse>(500, "text/plain", "500 exception while processing");
        }

        return nullptr;
    }
public:
    HttpServer();

    std::shared_ptr<WebsockHandlerBuilder> websocket(std::string path) {
        auto h = std::make_shared<WebsockHandlerBuilder>();
        mHandlers.insert(mHandlers.begin(), std::pair<std::string, std::shared_ptr<WebsockHandlerBuilder>>(path, h));
        return h;
    }

    std::shared_ptr<HttpHandlerBuilder> when(std::string path) {
        auto h = std::make_shared<HttpHandlerBuilder>();
        mHandlers.push_back(std::pair<std::string, std::shared_ptr<HttpHandlerBuilder>>(path, h));
        return h;
    }

    std::shared_ptr<HttpHandlerBuilder> whenMatching(std::string path) {
        auto h = std::make_shared<HttpHandlerBuilder>();
        mReHandlers.push_back(std::pair<std::regex, std::shared_ptr<HttpHandlerBuilder>>(std::regex(path), h));
        return h;
    }

    void startListening(uint16_t port);
    void shutdown();
};

#endif
