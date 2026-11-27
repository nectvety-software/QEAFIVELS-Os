#ifndef TOOLHUB_CATALOG_H
#define TOOLHUB_CATALOG_H

#include <stdint.h>

#define TOOLHUB_CATEGORY_COUNT 17u
#define TOOLHUB_TOOL_COUNT 34u

enum {
    TOOLHUB_UNAVAILABLE = 0,
    TOOLHUB_INFORMATION = 1,
    TOOLHUB_LOCAL = 2
};

const char *toolhub_category_name(uint8_t index);
const char *toolhub_category_description(uint8_t index);
uint8_t toolhub_category_first_tool(uint8_t index);
uint8_t toolhub_category_tool_count(uint8_t index);
const char *toolhub_tool_name(uint8_t index);
const char *toolhub_tool_description(uint8_t index);
const char *toolhub_tool_detail(uint8_t index);
const char *toolhub_tool_example(uint8_t index);
uint8_t toolhub_tool_category(uint8_t index);
uint8_t toolhub_tool_status(uint8_t index);

#endif
