#include "plugin-support.h"

const char *PLUGIN_NAME = "OBSLink";
const char *PLUGIN_VERSION = OBSLINK_VERSION;

void obs_log(int log_level, const char *format, ...)
{
	size_t length = 4 + strlen(PLUGIN_NAME) + strlen(format);
	char *tmpl = (char *)malloc(length + 1);
	snprintf(tmpl, length, "[%s] %s", PLUGIN_NAME, format);
	va_list args;
	va_start(args, format);
	blogva(log_level, tmpl, args);
	va_end(args);
	free(tmpl);
}
