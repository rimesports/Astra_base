#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void json_cmd_process_line(const char *line);
void json_cmd_publish_telemetry(void);

#ifdef __cplusplus
}
#endif
