
#include "http.hpp"

#ifdef TINYHTTP_WS

const char WEBSCOK_MAGIC_UID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const size_t WEBSOCK_MAGIC_UID_LEN = sizeof(WEBSCOK_MAGIC_UID) - 1;

#ifndef   SHA1_DIGEST_LENGTH
#  define SHA1_DIGEST_LENGTH 20
#endif

static inline constexpr uint32_t leftRotate(const uint32_t n, const uint32_t d) noexcept {
     return (n << d) | (n >> (32-d));
}

static inline void hash_sha1(const void* dataptr, const size_t size, uint8_t* outBuffer) {
    const uint8_t*  ptr     = reinterpret_cast<const uint8_t*>(dataptr);
    std::vector<uint8_t> data(ptr, ptr+size);

    uint32_t h0 = 0x67452301,
             h1 = 0xEFCDAB89,
             h2 = 0x98BADCFE,
             h3 = 0x10325476,
             h4 = 0xC3D2E1F0;

    size_t ml = size * 8;

    data.push_back(0x80);
    ml += 1;

    while ((ml % 512) != 448) {
        if ((ml % 8) == 0)
            data.push_back(0);
        ml++;
    }

    uint64_t _ml = static_cast<uint64_t>(size * 8);
    data.push_back((_ml >> 56) & 0xFF);
    data.push_back((_ml >> 48) & 0xFF);
    data.push_back((_ml >> 40) & 0xFF);
    data.push_back((_ml >> 32) & 0xFF);
    data.push_back((_ml >> 24) & 0xFF);
    data.push_back((_ml >> 16) & 0xFF);
    data.push_back((_ml >>  8) & 0xFF);
    data.push_back((_ml >>  0) & 0xFF);

    ml += 64;

    printf("data size: %lu, ml: %lu\n", data.size(), ml);

    size_t numChunks = ml / 512;

    uint32_t words[80], a, b, c, d, e, f, k, temp;

    for (size_t i = 0; i < numChunks; i++) {
        for (int j = 0; j < 16; j++) {
            words[j]  = static_cast<uint32_t>(data[i*64 + j*4 + 0]) << 24;
            words[j] |= static_cast<uint32_t>(data[i*64 + j*4 + 1]) << 16;
            words[j] |= static_cast<uint32_t>(data[i*64 + j*4 + 2]) <<  8;
            words[j] |= static_cast<uint32_t>(data[i*64 + j*4 + 3]) <<  0;
        }

        for (int j = 16; j < 80; j++)
            words[j] = leftRotate(words[j-3] ^ words[j-8] ^ words[j-14] ^ words[j-16], 1);

        a = h0, b = h1, c = h2, d = h3, e = h4;

        for (int j = 0; j < 80; j++) {
            if (j < 20)
                f = (b & c) | ((~b) & d), k = 0x5A827999;
            else if (j < 40)
                f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (j < 60)
                f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else if (j < 80)
                f = b ^ c ^ d, k = 0xCA62C1D6;

            temp = leftRotate(a, 5) + f + e + k + words[j];
            e = d;
            d = c;
            c = leftRotate(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    outBuffer[0*4+0] = (h0 >> 24) & 0xFF;
    outBuffer[0*4+1] = (h0 >> 16) & 0xFF;
    outBuffer[0*4+2] = (h0 >>  8) & 0xFF;
    outBuffer[0*4+3] = (h0 >>  0) & 0xFF;

    outBuffer[1*4+0] = (h1 >> 24) & 0xFF;
    outBuffer[1*4+1] = (h1 >> 16) & 0xFF;
    outBuffer[1*4+2] = (h1 >>  8) & 0xFF;
    outBuffer[1*4+3] = (h1 >>  0) & 0xFF;

    outBuffer[2*4+0] = (h2 >> 24) & 0xFF;
    outBuffer[2*4+1] = (h2 >> 16) & 0xFF;
    outBuffer[2*4+2] = (h2 >>  8) & 0xFF;
    outBuffer[2*4+3] = (h2 >>  0) & 0xFF;

    outBuffer[3*4+0] = (h3 >> 24) & 0xFF;
    outBuffer[3*4+1] = (h3 >> 16) & 0xFF;
    outBuffer[3*4+2] = (h3 >>  8) & 0xFF;
    outBuffer[3*4+3] = (h3 >>  0) & 0xFF;

    outBuffer[4*4+0] = (h4 >> 24) & 0xFF;
    outBuffer[4*4+1] = (h4 >> 16) & 0xFF;
    outBuffer[4*4+2] = (h4 >>  8) & 0xFF;
    outBuffer[4*4+3] = (h4 >>  0) & 0xFF;
}

// This was a fun afternoon... but it works nicely :)
namespace base64 {
    static inline constexpr char getBase64Char_Impl(const uint8_t idx) noexcept {
        return  idx < 26 ? 'A' + idx         : (
                idx < 52 ? 'a' + idx - 26    : (
                idx < 62 ? '0' + idx - 52    : (
                idx == 62 ? '+' : '/'
        )));
    }

    static inline constexpr char getBase64Char(const uint8_t idx) noexcept {
        return getBase64Char_Impl(idx & 0x3F);
    }

    static inline constexpr uint8_t getBase64Index(char ch) noexcept {
        return  ch == '+' ? 62 : (
                ch == '/' ? 63 : (
                ch <= '9' ? (ch - '0') + 52 : (
                ch == '=' ? 0 : (
                ch <= 'Z' ? (ch - 'A') : (ch - 'a') + 26
        ))));
    }

    static inline std::string encode(const void* dataptr, const size_t size) {
        const uint8_t*  ptr     = reinterpret_cast<const uint8_t*>(dataptr);
        size_t          i;
        std::string     result;

        result.reserve(4 * (size / 3));

        for (i = 0; i < size - (size % 3); i += 3) {
            const int c1 = ptr[i+0],
                      c2 = ptr[i+1],
                      c3 = ptr[i+2];

            result += getBase64Char(c1 >> 2);
            result += getBase64Char(((c1 & 3) << 4) | (c2 >> 4));
            result += getBase64Char(((c2 & 15) << 2) | (c3 >> 6));
            result += getBase64Char(c3);
        }

        auto mod = (size-i) % 3;
        switch (mod) {
            case 1:
                result += getBase64Char(ptr[i] >> 2);
                result += getBase64Char((ptr[i] & 3) << 4);
                result += "==";
                break;
            case 2:
                result += getBase64Char(ptr[i] >> 2);
                result += getBase64Char(((ptr[i] & 3) << 4) | (ptr[i+1] >> 4));
                result += getBase64Char((ptr[i+1] & 15) << 2);
                result += '=';
                break;
            default:
                break;
        }

        return result;
    }

    static inline std::string encode(const std::string& s) {
        return encode(s.data(), s.length());
    }

    static inline std::string decode(const std::string& input) {
        auto            size    = input.length();

        if ((size % 4) != 0)
            return decode(input + "=");

        std::string     result;

        result.reserve((size * 3) / 4);

        for (size_t i = 0; i < size; i += 4) {
            const uint8_t i1 = getBase64Index(input[i+0]),
                          i2 = getBase64Index(input[i+1]),
                          i3 = getBase64Index(input[i+2]),
                          i4 = getBase64Index(input[i+3]);

            result += static_cast<char>((i1 << 2) | (i2 >> 4));
            if (input[i+2] == '=') break;

            result += static_cast<char>((i2 << 4) | (i3 >> 2));
            if (input[i+3] == '=') break;

            result += static_cast<char>((i3 << 6) | i4);
        }

        return result;
    }
}

#endif

bool HttpRequest::parse(const char* raw, const ssize_t len) {
    std::string line;
    std::stringstream ss(std::string(raw, len));

    getline(ss, line);
    std::istringstream iss(line);
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

    while (getline(ss, line)) {
        if (line[line.length() - 1] == '\r') line = line.substr(0, line.length()-1);
        if (line.empty()) break;

        ssize_t sep = line.find(": ");
        if (sep <= 0)
            return false;

        std::string key = line.substr(0, sep), val = line.substr(sep+2);
        (*this)[key] = val;
        //std::cout << "HEADER: <" << key << "> set to <" << val << ">" << std::endl;
    }

    ssize_t readlen = len - ss.tellg();

    if (readlen > 0) {
        char* tmp = new char[readlen];
        bzero(tmp, readlen);
        ss.read(tmp, readlen);

        mContent = std::string(tmp, readlen);

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

std::unique_ptr<HttpResponse> WebsockHandlerBuilder::process(const HttpRequest& req) {
    if (req["Connection"].find("Upgrade") != std::string::npos) {
        std::string upgrade = req["Upgrade"];
        if (upgrade != "websocket") {
            fprintf(stderr, "Received connection upgrade with unknown upgrade type: '%s'\n", upgrade.c_str());
            return std::make_unique<HttpResponse>(400); // Send "400 Bad request"
        }

        HttpResponse res{101};
        res["Upgrade"] = "WebSocket";
        res["Connection"] = "Upgrade";

        auto clientKey = req["Sec-WebSocket-Key"];
        if (!clientKey.empty()) {
            std::string accept = clientKey + WEBSCOK_MAGIC_UID;
            unsigned char hash[SHA1_DIGEST_LENGTH];
            hash_sha1(accept.data(), accept.length(), hash);
            res["Sec-WebSocket-Accept"] = base64::encode(hash, SHA1_DIGEST_LENGTH);
        }

        res.requestProtocolHandover(this);
        return std::make_unique<HttpResponse>(res);
    }

    return HandlerBuilder::process(req);
}

void WebsockHandlerBuilder::acceptHandover(short& serverSock, short& clientSock) {
    char buffer[512];
    int len;

    auto theClient = mFactory->makeInstance();
    theClient->onConnect();

    while (serverSock > 0 && clientSock > 0) {
        if ((len = recv(clientSock, buffer, 2, 0)) < 0) {
            printf("recv failed");
            break;
        }

        if (len == 0)
            break;

        uint8_t first  = buffer[0];
        uint8_t second = buffer[1];

        bool fin = !!(first & 0x80);
        bool msk = !!(second & 0x80);
        uint8_t opc = first & 0x0F;

        if (opc == 0x08 /* connection close */)
            break;

        size_t payloadLength = second & 0x7F;
        if (payloadLength > 125) {
            // TODO:
            fprintf(stderr, "I was too lazy, sorry\n");
            break;
        }

        uint32_t key;
        if (msk) {
            if ((len = recv(clientSock, &key, 4, 0)) < 0) {
                printf("recv failed");
                break;
            }

            key = ntohl(key);
        }

        if ((len = recv(clientSock, buffer, payloadLength, 0)) < 0) {
            printf("recv failed");
            break;
        }

        if (msk) {
            for (size_t i = 0, o = 0; i < payloadLength; i++, o = i % 4) {
                uint8_t shift = (3 - o) << 3;
                buffer[i] ^= (shift == 0 ? key : (key >> shift)) & 0xFF;
            }
        }

        if (opc == 1)
            theClient->onTextMessage(std::string(reinterpret_cast<char*>(buffer), payloadLength));
        else if (opc == 2)
            theClient->onBinaryMessage(reinterpret_cast<uint8_t*>(buffer), payloadLength);
    }

    theClient->onDisconnect();
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
        mMimeDatabase.insert({std::string("js"), std::string("application/javascript")});
        mMimeDatabase.insert({std::string("pdf"), std::string("application/pdf")});
        mMimeDatabase.insert({std::string("gz"), std::string("application/gzip")});
        mMimeDatabase.insert({std::string("xml"), std::string("application/xml")});
        mMimeDatabase.insert({std::string("html"), std::string("text/html")});
        mMimeDatabase.insert({std::string("htm"), std::string("text/html")});
        mMimeDatabase.insert({std::string("css"), std::string("text/css")});
        mMimeDatabase.insert({std::string("txt"), std::string("text/plain")});
        mMimeDatabase.insert({std::string("png"), std::string("image/png")});
        mMimeDatabase.insert({std::string("jpg"), std::string("image/jpeg")});
        mMimeDatabase.insert({std::string("jpeg"), std::string("image/jpeg")});
        mMimeDatabase.insert({std::string("json"), std::string("application/json")});
    }

    ssize_t pos = name.rfind(".");
    if (pos < 0)
        return "application/octet-stream";

    auto f = mMimeDatabase.find(name.substr(pos+1));
    if (f == mMimeDatabase.end())
        return "application/octet-stream";

    return f->second;
}

HttpServer::HttpServer() {
    mDefault404Message = HttpResponse{404, "text/plain", "404 not found"}.buildMessage();
    mDefault400Message = HttpResponse{400, "text/plain", "400 bad request"}.buildMessage();
}

void HttpServer::startListening(uint16_t port) {
    mSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (mSocket == -1)
        throw std::runtime_error("Could not create socket");

    struct sockaddr_in remote = {0};

    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = htonl(INADDR_ANY);
    remote.sin_port = htons(port);
    int iRetval;

    while (true) {
        iRetval = bind(mSocket, reinterpret_cast<struct sockaddr*>(&remote), sizeof(remote));

        if (iRetval < 0) {
            usleep(100000);
            //throw std::runtime_error("Failed to bind socket");
        } else break;
    }

    listen(mSocket, 3);

    printf("Waiting for incoming connections...\n");
    while (mSocket != -1) {
        const size_t clientLen = sizeof(struct sockaddr_in);
        struct sockaddr_in client;

        short _sock = accept(mSocket, reinterpret_cast<struct sockaddr*>(&client), const_cast<socklen_t*>(reinterpret_cast<const socklen_t*>(&clientLen)));

        if (_sock < 0) {
            perror("accept failed");
            continue;
        }

        std::thread([_sock,this]() {
            short sock = _sock;
            char msg[16384];
            ssize_t len;
            ICanRequestProtocolHandover* handover = nullptr;

            while (mSocket != -1) {
                HttpRequest req;

                if ((len = recv(sock, msg, 16384, 0)) < 0) {
                    printf("recv failed");
                    break;
                }

                if (req.parse(msg, len) && len < 16383) {
                    auto res = processRequest(req.getPath(), req);
                    if (res) {
                        if (res) {
                            auto builtMessage = res->buildMessage();
                            if ((len = send(sock, builtMessage.data(), builtMessage.size(), 0)) < 0)
                                printf("Send failed");

                            if (res->acceptProtocolHandover(&handover))
                                break;
                            goto keep_alive_check;
                        }
                    }

                    if (send(sock, mDefault404Message.data(), mDefault404Message.size(), 0) < 0) {
                        printf("Send failed");
                        break;
                    }

                    keep_alive_check:
                    if (req["Connection"] != "keep-alive")
                        break;
                } else {
                    if (send(sock, mDefault400Message.data(), mDefault400Message.size(), 0) < 0)
                        printf("Send failed");
                    break;
                }
            }

            if (handover) handover->acceptHandover(mSocket, sock);
            puts("Connection close");
            close(sock);
        }).detach();
    }

    puts("Listen loop shut down");
}

void HttpServer::shutdown() {
    close(mSocket);
    mSocket = -1;
}
