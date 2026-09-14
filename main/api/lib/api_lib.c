#include "string.h"
#include "esp_log.h"
#include "esp_http_server.h"

const char *get_mime_type(const char *filename)
{
    if (filename == NULL)
        return HTTPD_TYPE_OCTET;

    const char *ext = strrchr(filename, '.');
    if (!ext)
        return "text/plain"; // No extension found

    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
        return HTTPD_TYPE_TEXT;
    if (strcasecmp(ext, ".txt") == 0)
        return "text/plain";
    if (strcasecmp(ext, ".css") == 0)
        return "text/css";
    if (strcasecmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcasecmp(ext, ".json") == 0)
        return HTTPD_TYPE_JSON;
    if (strcasecmp(ext, ".png") == 0)
        return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, ".ico") == 0)
        return "image/x-icon";
    if (strcasecmp(ext, ".svg") == 0)
        return "image/svg+xml";
    if (strcasecmp(ext, ".webmanifest") == 0)
        return "application/manifest+json";

    return HTTPD_TYPE_OCTET; // Default fallback
}