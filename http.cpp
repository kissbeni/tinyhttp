
#include "http.hpp"

#include <vector>
#include <iterator>

/*static*/ TCPClientStream TCPClientStream::acceptFrom(short listener) {
    struct sockaddr_in client;
    const size_t clientLen = sizeof(client);

    short sock = accept(
        listener,
        reinterpret_cast<struct sockaddr*>(&client),
        const_cast<socklen_t*>(reinterpret_cast<const socklen_t*>(&clientLen))
    );

    if (sock < 0) {
        perror("accept failed");
        return {-1};
    }

    return {sock};
}

void TCPClientStream::send(const void* what, size_t size) {
    if (::send(mSocket, what, size, MSG_NOSIGNAL) < 0)
        throw std::runtime_error("TCP send failed");
}

size_t TCPClientStream::receive(void* target, size_t max) {
    ssize_t len;

    if ((len = recv(mSocket, target, max, MSG_NOSIGNAL)) < 0)
        throw std::runtime_error("TCP receive failed");

    return static_cast<size_t>(len);
}

std::string TCPClientStream::receiveLine(bool asciiOnly, size_t max) {
    std::string res;
    char ch;

    while (res.size() < max) {
        if (recv(mSocket, &ch, 1, MSG_NOSIGNAL) != 1)
            throw std::runtime_error("TCP receive failed");

        if (ch == '\r') continue;
        if (ch == '\n') break;

        if (asciiOnly && !isascii(ch))
            throw std::runtime_error("Only ASCII characters were allowed");

        res.push_back(ch);
    }

    return res;
}

void TCPClientStream::close() {
    if (mSocket < 0) return;
    ::shutdown(mSocket, SHUT_RDWR);
    ::close(mSocket);
    mSocket = -1;
}

bool HttpRequest::parse(std::shared_ptr<IClientStream> stream) {
    std::istringstream iss(stream->receiveLine());
    std::vector<std::string> results(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>());

    if (results.size() < 2)
        return false;

    std::string methodString = results[0];
         if (methodString == "GET"    ) { mMethod = HttpRequestMethod::GET;     }
    else if (methodString == "POST"   ) { mMethod = HttpRequestMethod::POST;    }
    else if (methodString == "PUT"    ) { mMethod = HttpRequestMethod::PUT;     }
    else if (methodString == "DELETE" ) { mMethod = HttpRequestMethod::DELETE;  }
    else if (methodString == "OPTIONS") { mMethod = HttpRequestMethod::OPTIONS; }
    else return false;

    path = results[1];

    ssize_t question = path.find("?");
    if (question > 0) {
        query = path.substr(question);
        path = path.substr(0, question);
    }

    if (query.empty())
        std::cout << methodString << " " << path << std::endl;
    else
        std::cout << methodString << " " << path << " (Query: " << query << ")" << std::endl;

    while (true) {
        std::string line = stream->receiveLine();

        if (line.empty()) break;

        ssize_t sep = line.find(": ");
        if (sep <= 0)
            return false;

        std::string key = line.substr(0, sep), val = line.substr(sep+2);
        (*this)[key] = val;
        //std::cout << "HEADER: <" << key << "> set to <" << val << ">" << std::endl;
    }

    std::string contentLength = (*this)["Content-Length"];
    ssize_t cl = std::atoll(contentLength.c_str());

    if (cl > MAX_HTTP_CONTENT_SIZE)
        throw std::runtime_error("request too large");

    if (cl > 0) {
        char* tmp = new char[cl];
        bzero(tmp, cl);
        stream->receive(tmp, cl);

        mContent = std::string(tmp, cl);
        delete[] tmp;

        #ifdef TINYHTTP_JSON
        if (    (*this)["Content-Type"] == "application/json"
            ||  (*this)["Content-Type"].rfind("application/json;",0) == 0 // some clients gives us extra data like charset
        ) {
            std::string error;
            mContentJson = miniJson::Json::parse(mContent, error);
            if (!error.empty())
                std::cerr << "Content type was JSON but we couldn't parse it! " << error << std::endl;
        }
        #endif
    }

    return true;
}

/*static*/ bool HttpHandlerBuilder::isSafeFilename(const std::string& name, bool allowSlash) {
    static const char allowedChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-+@";
    for (auto x : name) {
        if (x == '/' && !allowSlash)
            return false;

        bool ok = false;
        for (size_t i = 0; allowedChars[i] && !ok; i++)
            ok = allowedChars[i] == x;

        if (!ok) return false;
    }

    return true;
}

/*static*/ std::string HttpHandlerBuilder::getMimeType(std::string name) {
    static std::map<std::string, std::string> mMimeDatabase;

    if (mMimeDatabase.empty()) {
        mMimeDatabase.insert({"js",   "application/javascript"});
        mMimeDatabase.insert({"pdf",  "application/pdf"});
        mMimeDatabase.insert({"gz",   "application/gzip"});
        mMimeDatabase.insert({"xml",  "application/xml"});
        mMimeDatabase.insert({"html", "text/html"});
        mMimeDatabase.insert({"htm",  "text/html"});
        mMimeDatabase.insert({"css",  "text/css"});
        mMimeDatabase.insert({"txt",  "text/plain"});
        mMimeDatabase.insert({"png",  "image/png"});
        mMimeDatabase.insert({"jpg",  "image/jpeg"});
        mMimeDatabase.insert({"jpeg", "image/jpeg"});
        mMimeDatabase.insert({"json", "application/json"});
    }

    ssize_t pos = name.rfind(".");
    if (pos < 0)
        return "application/octet-stream";

    auto f = mMimeDatabase.find(name.substr(pos+1));
    if (f == mMimeDatabase.end())
        return "application/octet-stream";

    return f->second;
}

HttpServer::Processor::Processor(std::shared_ptr<IClientStream> stream, HttpServer& owner)
    : mClientStream{std::move(stream)}, mOwner{owner}, mLastActive{std::chrono::system_clock::now()},
      mIsAlive{true}, mHasHandover{false} { }

/* static */ void HttpServer::Processor::clientThreadProc(std::shared_ptr<Processor> self) {
    ICanRequestProtocolHandover* handover = nullptr;
    std::unique_ptr<HttpRequest> handoverRequest;

    try {
        while (self->mClientStream->isOpen() && self->isAlive()) {
            HttpRequest req;

            try {
                if (!req.parse(self->mClientStream)) {
                    self->mClientStream->send(self->mOwner.mDefault400Message);
                    self->mClientStream->close();
                    continue;
                }
            } catch (...) {
                self->mClientStream->send(self->mOwner.mDefault400Message);
                self->mClientStream->close();
                continue;
            }

            auto res = self->mOwner.processRequest(req.getPath(), req);
            if (res) {
                auto builtMessage = res->buildMessage();
                self->mClientStream->send(builtMessage);

                if (res->acceptProtocolHandover(&handover)) {
                    handoverRequest = std::make_unique<HttpRequest>(req);
                    break;
                }

                goto keep_alive_check;
            }

            self->mClientStream->send(self->mOwner.mDefault404Message);

            keep_alive_check:
            self->mLastActive = std::chrono::system_clock::now();

            if (req["Connection"] != "keep-alive")
                break;
        }

        if (handover) {
            puts("Doing handover");
            self->mHasHandover = true;
            handover->acceptHandover(self->mOwner.mSocket, *self->mClientStream.get(), std::move(handoverRequest));
            puts("Handover proc exited");
        }
    } catch (std::exception& e) {
        // Don't print the exception when we are getting shut down, it's expected to be raised
        if (self->isAlive()) {
            std::cerr << "Exception in HTTP client handler (" << e.what() << ")\n";
        }
    }

    self->mClientStream->close();
    self->mIsAlive = false;
}

bool HttpServer::Processor::isTimedOut() const noexcept {
    if constexpr (TINYHTTP_CLIENT_TIMEOUT <= 0) {
        return false;
    }

    if (mHasHandover) {
        return false;
    }

    auto duration = std::chrono::system_clock::now() - mLastActive;
    return duration > std::chrono::seconds(TINYHTTP_CLIENT_TIMEOUT);
}

void HttpServer::Processor::shutdown() {
    std::unique_lock{mShutdownMutex};
    mIsAlive = false;
    
    if (mClientStream && mClientStream->isOpen())
        mClientStream->close();

    if (mWorkThread && mWorkThread->joinable())
        mWorkThread->detach();
}

void HttpServer::Processor::startThread() {
    auto self_ptr = shared_from_this();
    mWorkThread.reset(new std::thread{[self_ptr]() {
        clientThreadProc(self_ptr);
    }});
}

void HttpServer::cleanupThreadProc() {
    while (!mCleanupThreadShutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (mSocket == -1)
            continue;
        
        mRequestProcessorListMutex.lock();
        for (auto it = mRequestProcessors.begin(); it != mRequestProcessors.end(); ++it) {
            auto& processor = *it;

            if (processor->isTimedOut()) {
                processor->shutdown();
            }

            if (!processor->isAlive()) {
                mRequestProcessors.erase(it);
                it = mRequestProcessors.begin();
            }
        }
        mRequestProcessorListMutex.unlock();
    }
}

HttpServer::HttpServer() {
    mDefault404Message = HttpResponse{404, "text/plain", "404 not found"}.buildMessage();
    mDefault400Message = HttpResponse{400, "text/plain", "400 bad request"}.buildMessage();

    mCleanupThread.reset(new std::thread{[this]() { this->cleanupThreadProc(); }});
}

void HttpServer::startListening(uint16_t port) {
    if (mSocket != -1)
        throw std::runtime_error("Server is already running");

    mSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (mSocket == -1)
        throw std::runtime_error("Could not create socket");

    int opt = 1;
    if (setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        throw std::runtime_error("Could not set SO_REUSEADDR option");
    }

    struct sockaddr_in remote = {0};

    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = htonl(INADDR_ANY);
    remote.sin_port = htons(port);
    int iRetval;

    while (true) {
        iRetval = bind(mSocket, reinterpret_cast<struct sockaddr*>(&remote), sizeof(remote));

        if (iRetval < 0) {
            perror("Failed to bind socket, retrying in 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } else break;
    }

    iRetval = ::listen(mSocket, 3);
    if (iRetval < 0)
        throw std::runtime_error("listen() failed");

    printf("Waiting for incoming connections...\n");
    while (mSocket != -1) {
        auto processor = std::make_shared<Processor>(
            std::make_shared<TCPClientStream>(TCPClientStream::acceptFrom(mSocket)),
            *this
        );

        processor->startThread();

        mRequestProcessorListMutex.lock();
        mRequestProcessors.push_back(std::move(processor));
        mRequestProcessorListMutex.unlock();
    }

    puts("Listen loop shut down");
}

void HttpServer::shutdown() {
    int sock = mSocket;

    if (mSocket < 0) {
        return;
    }
    
    mSocket = -1;

    puts("Shutting down server");
    ::shutdown(sock, SHUT_RDWR);

    puts("Shutting down request handlers");
    mRequestProcessorListMutex.lock();
    mRequestProcessors.clear();
    mRequestProcessorListMutex.unlock();
    close(sock);
}
