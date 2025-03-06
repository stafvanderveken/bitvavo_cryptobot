#ifndef API_HANDLING_H
#define API_HANDLING_H


#include <nlohmann/json.hpp>
using json = nlohmann::json;

// **Function to read environment variables from .env file**
std::string get_env(const std::string& key);

// **CURL callback to handle response data**
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output);

// **Generate HMAC-SHA256 signature for API authentication**
std::string generateSignature(const std::string& secret, const std::string& message);

// **CURL callback to handle header data (rate limits)**
size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata);

// **API request function with retry logic**
json apiRequest(const std::string& endpoint, const std::string& method = "GET", const std::string& body = "");

#endif // !API_HANDLING_H
