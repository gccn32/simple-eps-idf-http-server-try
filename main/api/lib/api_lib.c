#include "string.h"
#include "esp_log.h"
#include "esp_http_server.h"

const char *get_mime_type(const char *filename)
{
    if (filename == NULL)
        return "application/octet-stream";

    const char *ext = strrchr(filename, '.');
    if (!ext)
        return "text/plain"; // No extension found

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0 || strcmp(ext, ".txt") == 0)
        return HTTPD_TYPE_TEXT;
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    if (strcmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcmp(ext, ".json") == 0)
        return HTTPD_TYPE_JSON;
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, ".ico") == 0)
        return "image/x-icon";
    if (strcmp(ext, ".svg") == 0)
        return "image/svg+xml";
    if (strcmp(ext, ".webmanifest") == 0)
        return "application/manifest+json";

    return HTTPD_TYPE_OCTET; // Default fallback
}