#include <libavutil/log.h>
#include "log.h"

void open_log(int interface, int log) {
	if (log) {
		static char ngtc[10] = "ngtc";
		int fac = LOG_USER;

		sprintf(ngtc, "ngtc%d", interface);

		if (interface == 0)
			fac = LOG_LOCAL0;
		else if (interface == 1)
			fac = LOG_LOCAL1;
		else if (interface == 2)
			fac = LOG_LOCAL2;
		else if (interface == 3)
			fac = LOG_LOCAL3;

		openlog(ngtc, LOG_PID, fac);

		av_log_set_callback(ngtc_log_callback);
	}

	return;
}

void ngtc_log_callback(void* avcl, int level, const char* fmt, va_list vl) {
	// Send to syslog 
	if (level <=  av_log_get_level()) {
		int priority = level;

		if (level == AV_LOG_DEBUG)
			priority = LOG_DEBUG;
		else if (level == AV_LOG_INFO)
			priority = LOG_INFO;
		else if (level == AV_LOG_VERBOSE)
			priority = LOG_NOTICE;
		else if (level == AV_LOG_WARNING)
			priority = LOG_WARNING;
		else if (level == AV_LOG_ERROR)
			priority = LOG_ERR;
		else if (level == AV_LOG_FATAL)
			priority = LOG_CRIT;
		else
			priority = LOG_DEBUG;

		vsyslog(priority, fmt, vl);
	}	

	return;
}

void ngtc_log(void *avcl, int level, const char *fmt, ...) {
	AVClass* avc= avcl ? *(AVClass**)avcl : NULL;
	va_list vl;

	va_start(vl, fmt);
	if(avc && avc->version >= (50<<16 | 15<<8 | 2) && avc->log_level_offset_offset && level>=AV_LOG_FATAL)
		level += *(int*)(((uint8_t*)avcl) + avc->log_level_offset_offset);

	// Syslog
	ngtc_log_callback(avcl, level, fmt, vl);

	// LibAV Logging output
	av_log_default_callback(avcl, level, fmt, vl);

	va_end(vl);

	return;
}
