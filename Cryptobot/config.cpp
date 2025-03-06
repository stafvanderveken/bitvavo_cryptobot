#include "config.h"
#include "API_Handling.h"
#include <string>


const std::string API_KEY = get_env("API_KEY");
const std::string API_SECRET = get_env("API_SECRET");
const std::string BASE_URL = "https://api.bitvavo.com/v2/";

// Rate limit globals, if they are meant to be accessed only within this file
long long g_rateLimitRemaining = -1;
long long g_rateLimitResetAt = -1;