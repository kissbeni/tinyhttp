
#include "http.hpp"
#include <mutex>
#include <set>
#include <iomanip>
#include <home.html.hpp>

// use SHA1 from the websocket implementation
extern void hash_sha1(const void* dataptr, const size_t size, uint8_t* outBuffer);

std::mutex gUserControlMutex, gSessionControlMutex;

struct User {
    size_t id;
    std::string username;
    std::string displayName;
    uint8_t password[20];

    void setPassword(const std::string& pass) {
        hash_sha1(pass.data(), pass.size(), password);
    }

    bool checkPassword(const std::string& pass) {
        uint8_t hash[20];
        hash_sha1(pass.data(), pass.size(), hash);
        return memcmp(hash, password, 20) == 0;
    }
};

std::set<std::string> gUserNames;
std::vector<User> gUsers;
std::map<std::string, size_t> gUserNameIndex;

User& addUser(std::string username, std::string password, std::string displayName) {
    gUserControlMutex.lock();
    gUserNames.insert(username);

    User u = {
        .id             = gUsers.size(),
        .username       = std::move(username),
        .displayName    = std::move(displayName)
    };
    
    u.setPassword(password);
    gUserNameIndex.insert({u.username, u.id});
    gUsers.push_back(std::move(u));
    
    User& res = gUsers.back();
    gUserControlMutex.unlock();
    return res;
}

std::map<std::string, size_t> gUserSessions;

std::string createSession(size_t userId) {
    std::stringstream ss;

    auto& hexStream = ss << std::hex << std::setfill('0');

    for (size_t i = 0; i < 20; ++i) 
        hexStream << std::setw(2) << static_cast<unsigned>(rand() % 256);

    std::string token = ss.str();

    gSessionControlMutex.lock();
    gUserSessions.insert({token, userId});
    gSessionControlMutex.unlock();

    std::cout << "New session:     <" << token << '>' << std::endl;

    return token;
}

struct ChatSocketHandler : public WebsockClientHandler {
    void onConnect() override {
        puts("Connect!");
    }

    void onTextMessage(const std::string& message) override {
        puts("Text message!");
        std::cout << "  -> " << message << std::endl;
        sendJson(miniJson::Json::_object { { "Hello", "World" } });
    }

    void onBinaryMessage(const uint8_t* message, const size_t len) override {
        puts("Binary message!");
    }

    void onDisconnect() override {
        puts("Disconnect!");
    }
};

static HttpResponse handleRegister(const HttpRequest& req) {
    miniJson::Json::_object res;
    auto body = req.json();
    
    if (!body.isObject()) {
        res["error"] = "INVALID_BODY";
        return {200, res};
    }

    const auto& j_username      = body["username"];
    const auto& j_password1     = body["password1"];
    const auto& j_password2     = body["password2"];
    const auto& j_displayName   = body["displayname"];

    if (!j_username.isString() || !j_password1.isString() || !j_password2.isString() || !j_displayName.isString()) {
        res["error"] = "INVALID_BODY";
        return {200, res};
    }

    std::string username    = j_username.toString();
    std::string password1   = j_password1.toString();
    std::string password2   = j_password2.toString();
    std::string displayName = j_displayName.toString();

    if (username.length() < 3) {
        res["error"] = "USERNAME_TOO_SHORT";
        return {200, res};
    }

    if (password1.length() < 8) {
        res["error"] = "PASSWORD_TOO_SHORT";
        return {200, res};
    }

    if (password2 != password2) {
        res["error"] = "PASSWORD_MISSMATCH";
        return {200, res};
    }

    if (displayName.empty()) {
        res["error"] = "DISPLAY_NAME_EMPTY";
        return {200, res};
    }

    // possible race condition here, you may be able to create two users with the same username
    if (gUserSessions.find(username) != gUserSessions.end()) {
        res["error"] = "ALREADY_EXISTS";
        return {200, res};
    }

    std::cout << "Register:        <" << username << '>' << std::endl;

    auto newUser = addUser(std::move(username), std::move(password1), std::move(displayName));
    
    res["error"] = nullptr;
    
    HttpResponse response{200, res};
    response["Set-Cookie"] = "tinyhttpChatSess=" + createSession(newUser.id) + "; Max-Age=2592000; path=/; HttpOnly; SameSite=Strict";
    return response;
}

static HttpResponse handleLogin(const HttpRequest& req) {
    miniJson::Json::_object res;
    auto body = req.json();
    
    if (!body.isObject()) {
        res["error"] = "INVALID_BODY";
        return {200, res};
    }

    const auto& j_username  = body["username"];
    const auto& j_password  = body["password"];

    if (!j_username.isString() || !j_password.isString()) {
        res["error"] = "INVALID_BODY";
        return {200, res};
    }

    std::string username    = j_username.toString();
    std::string password    = j_password.toString();

    auto key = gUserNameIndex.find(username);

    if (key == gUserNameIndex.end()) {
        res["error"] = "NO_USER";
        return {200, res};
    }

    size_t userId = key->second;

    if (gUsers[userId].checkPassword(password)) {
        res["error"] = "BAD_PASSWORD";
        return {200, res};
    }

    res["error"] = nullptr;

    std::cout << "Login:           <" << username << '>' << std::endl;
    
    HttpResponse response{200, res};
    response["Set-Cookie"] = "tinyhttpChatSess=" + createSession(userId) + "; Max-Age=2592000; path=/; HttpOnly; SameSite=Strict";
    return response;
}

std::string parseSessionCookie(std::string cookieString) {
    ssize_t pos = cookieString.find("tinyhttpChatSess=");

    if (pos < 0)
        return {};
    
    std::string croppedCookie = cookieString.substr(pos);

    if (croppedCookie.length() <= strlen("tinyhttpChatSess="))
        return {};

    croppedCookie = croppedCookie.substr(croppedCookie.find('=') + 1);
    croppedCookie = croppedCookie.substr(0, croppedCookie.find(';'));
    return croppedCookie;
}

int main(int argc, char const *argv[])
{
    srand(time(NULL));

    HttpServer s;

    s.when("/")->serveFile("index.html");
    s.whenMatching("/static/[^/]+")->serveFromFolder("static");

    s.when("/register")->posted(handleRegister);
    s.when("/login")->posted(handleLogin);

    s.when("/dump-users")->requested([](const HttpRequest& req) -> HttpResponse {
        miniJson::Json::_object res;

        miniJson::Json::_array userNames;

        for (const auto& e : gUserNames)
            userNames.push_back(e);

        miniJson::Json::_array users;

        for (const auto& e : gUsers) {
            miniJson::Json::_object user;
            user["username"] = e.username;
            user["displayName"] = e.displayName;
            user["id"] = static_cast<int>(e.id);
            users.push_back(user);
        }

        res["userNamesInUse"] = userNames;
        res["activeUsers"] = users;

        return {200, res};
    });

    s.when("/home")->requested([](const HttpRequest& req) -> HttpResponse {
        std::string cookie = parseSessionCookie(req["Cookie"]);

        auto sess = gUserSessions.find(cookie);

        if (sess == gUserSessions.end())
            goto bad_session;
        
        return {200, templates::HomeTemplate(gUsers[sess->second].displayName)};

        bad_session:
        HttpResponse response{403};
        response["Location"] = "/";
        response["Set-Cookie"] = "tinyhttpChatSess=removed; Max-Age=-1; path=/; HttpOnly";
        return response;
    });

    s.when("/logout")->requested([](const HttpRequest& req) -> HttpResponse {
        std::string cookie = parseSessionCookie(req["Cookie"]);

        if (!cookie.empty()) {
            std::cout << "Destroy session <" << cookie << '>' << std::endl;
            gSessionControlMutex.lock();
            gUserSessions.erase(cookie);
            gSessionControlMutex.unlock();
        }

        HttpResponse response{403};
        response["Location"] = "/";
        response["Set-Cookie"] = "tinyhttpChatSess=removed; Max-Age=-1; path=/; HttpOnly";
        return response;
    });

    s.websocket("/ws")->handleWith<ChatSocketHandler>();

    s.startListening(8088);
    return 0;
}
