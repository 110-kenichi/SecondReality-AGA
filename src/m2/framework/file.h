#ifndef FRAMEWORK_FILE_H
#define FRAMEWORK_FILE_H

#include "framework.h"

/* サイズ既知のファイルを Fast RAM にロードする。失敗時は 0 を返す。
 * size_out にロードしたサイズ (バイト) を返す。 */
void *file_load_fast(const char *name, u32 expected_size, u32 *size_out);

#endif
