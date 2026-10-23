/**
 * @file stock_ui.h
 *
 * User file: the editor creates it once and never overwrites it. The generated API
 * (fonts, styles, constants, create functions) is in stock_ui_gen.h, written by Ctrl+B.
 */

#ifndef STOCK_UI_H
#define STOCK_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "stock_ui_gen.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL VARIABLES
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize the UI library: fonts, global styles and anything added in stock_ui.c.
 * Call it once, after lv_init() and the display driver, before any *_create() function.
 * @param asset_path    prefix for file-based assets; "" is fine, all assets here are compiled in
 */
void stock_ui_init(const char * asset_path);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*STOCK_UI_H*/
