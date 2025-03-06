#ifndef config_h
#define config_h

#include <string>

extern const std::string API_KEY;
extern const std::string API_SECRET;
extern const std::string BASE_URL;

extern long long g_rateLimitRemaining;
extern long long g_rateLimitResetAt;

#endif // !config_h
