# tinyhttp
A tiny HTTP server library written in modern C++ with REST and WS support, used mostly in my embedded projects.

## How to use

If you want an environment to tinker around a bit, check out the example folder :)

### Creating a server

```c++
HttpServer server;

// Setup your handlers here...

server.startListening(80); // Start listening on port 80
```

*Currently specifying listen address is not supported.*

### Serving static files

```c++

// Setup root document
server.when("/")
    // Answer with a specific file
    ->serveFile("/path/to/index.html");

// Setup folder for static files
server.whenMatching("/static/[^/]+")
    // Answer with a file from a folder
    ->serveFromFolder("/path/to/static/files/");
```

As of now `serveFromFolder` does not support subdirectories for security reasons.

*Currently specifying custom MIME types is not supported. The MIME type is guessed from the file extension.*

### Custom handlers

Here we define an endpoint (`/mynumber`) that stores an integer.

```c++
static int myNumber = 0;

server.when("/mynumber")
    // Handle when data is posted here (POST)
    ->posted([](const HttpRequest& req) {
        // Set the number by parsing the raw request body
        myNumber = atoi(req.content().c_str());
        return HttpResponse{200};
    })
    // Handle when data is requested from here (GET)
    ->requested([](const HttpRequest& req) {
        // Reply with a plain text response containing the stored number
        return HttpResponse{200, "text/plain", std::to_string(myNumber)};
    });
```

### Working with json

I used [MiniJson](https://github.com/zsmj2017/MiniJson) because it was tiny and easy-to use. Here is an implementation of the same functionality as in the previous example, but with JSON.

```c++
static int myNumber = 0;

server.when("/mynumber/json")
    // Handle when data is posted here (POST)
    ->posted([](const HttpRequest& req) {
        const auto& data = req.json();

        // If the parsed data is not an object, ignore
        if (!data.isObject())
            return HttpResponse{400, "text/plain", "Invalid JSON"};

        const auto& jNumber = data["value"];

        // Check if the data with the key "value" is a number
        if (!jNumber.isNumber())
            return HttpResponse{400, "text/plain", "Value is not a number"};
        
        // Set the number by parsing the raw request body
        myNumber = static_cast<int>(jNumber.toDouble());
        return HttpResponse{200};
    })
    // Handle when data is requested from here (GET)
    ->requested([](const HttpRequest& req) {
        // Create a new JSON object
        miniJson::Json::_object obj;

        // Add a new key "value" to it, set to myNumber
        obj["value"] = myNumber;

        // Respond with json
        return HttpResponse{200, obj};
    });
```

### Websockets

(TODO)
