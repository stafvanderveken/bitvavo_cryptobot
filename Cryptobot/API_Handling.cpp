#include "API_Handling.h"
#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>


using namespace std;


// **Function to read environment variables from .env file**
string get_env(const string& key) {
    ifstream env_file(".env");
    string line;
    while (getline(env_file, line)) {
        size_t equals_pos = line.find('=');
        if (equals_pos != string::npos && line.substr(0, equals_pos) == key) {
            return line.substr(equals_pos + 1);
        }
    }
    return "";
}

// **CURL callback to handle response data**
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    output->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// **Generate HMAC-SHA256 signature for API authentication**
string generateSignature(const string& secret, const string& message) {
    unsigned char* digest = HMAC(EVP_sha256(), secret.c_str(), secret.length(),
        (unsigned char*)message.c_str(), message.length(), NULL, NULL);
    stringstream ss;
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << hex << setw(2) << setfill('0') << (int)digest[i];
    }
    return ss.str();
}

// **CURL callback to handle header data (rate limits)**
size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t totalSize = size * nitems;
    string header(buffer, totalSize);
    long long* rateLimitData = static_cast<long long*>(userdata);
    long long* localRemaining = &rateLimitData[0];
    long long* localResetAt = &rateLimitData[1];
    auto extractValue = [](const string& headerLine, const string& key) -> long long {
        size_t pos = headerLine.find(key);
        if (pos != string::npos) {
            size_t start = headerLine.find(":", pos);
            if (start != string::npos) {
                string valueStr = headerLine.substr(start + 1);
                try {
                    return stoll(valueStr);
                }
                catch (const std::exception&) {
                    cerr << "Warning: Invalid value for " << key << " in header: " << valueStr << endl;
                }
            }
        }
        return -1;
        };
    if (header.find("bitvavo-ratelimit-remaining:") != string::npos) {
        long long val = extractValue(header, "bitvavo-ratelimit-remaining");
        *localRemaining = val;
        g_rateLimitRemaining = val;
    }
    if (header.find("bitvavo-ratelimit-resetat:") != string::npos) {
        long long val = extractValue(header, "bitvavo-ratelimit-resetat");
        *localResetAt = val;
        g_rateLimitResetAt = val;
    }
    return totalSize;
}

// **API request function with retry logic**
json apiRequest(const std::string& endpoint, const std::string& method, const std::string& body) {
    const int maxRetries = 5;
    int attempt = 0;
    int delaySeconds = 1;
    json parsedResponse;
    while (attempt < maxRetries) {
        attempt++;
        CURL* curl = curl_easy_init();
        if (!curl) {
            cerr << "Failed to initialize CURL" << endl;
            break;
        }
        string url = BASE_URL + endpoint;
        string response;
        string timestamp = to_string(chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count());
        string message = timestamp + method + "/v2/" + endpoint + body;
        string signature = generateSignature(API_SECRET, message);
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, ("Bitvavo-Access-Key: " + API_KEY).c_str());
        headers = curl_slist_append(headers, ("Bitvavo-Access-Timestamp: " + timestamp).c_str());
        headers = curl_slist_append(headers, ("Bitvavo-Access-Signature: " + signature).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        long long rateLimitData[2] = { -1, -1 };
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, rateLimitData);
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        }
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res)
                << ". Attempt " << attempt << " of " << maxRetries << endl;
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            this_thread::sleep_for(chrono::seconds(delaySeconds));
            delaySeconds *= 2;
            continue;
        }
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        if (http_code == 429) {
            cerr << "HTTP 429 Too Many Requests. Attempt " << attempt
                << " of " << maxRetries << ". Retrying after " << delaySeconds << " seconds." << endl;
            this_thread::sleep_for(chrono::seconds(delaySeconds));
            delaySeconds *= 2;
            continue;
        }
        else if (http_code == 401 || http_code == 403) {
            cerr << "Fatal HTTP error " << http_code << ". Not retrying." << endl;
            break;
        }
        else if (http_code != 200 && http_code != 201) {
            cerr << "HTTP request failed with code: " << http_code << ". Attempt " << attempt
                << " of " << maxRetries << ". Response: " << response << endl;
            this_thread::sleep_for(chrono::seconds(delaySeconds));
            delaySeconds *= 2;
            continue;
        }
        try {
            parsedResponse = json::parse(response);
            return parsedResponse;
        }
        catch (const json::parse_error& e) {
            cerr << "JSON parse error: " << e.what()
                << ". Attempt " << attempt << " of " << maxRetries << ". Response: " << response << endl;
            this_thread::sleep_for(chrono::seconds(delaySeconds));
            delaySeconds *= 2;
            continue;
        }
    }
    cerr << "Max retries reached. Returning empty JSON." << endl;
    return json{};
}