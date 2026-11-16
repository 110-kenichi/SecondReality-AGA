#include "file.h"
#include "mem.h"
#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/memory.h>

extern struct DosLibrary *DOSBase;

/* サイズ既知のファイルを Fast RAM にロードする。
 * 注意:
 *  - TakeSystem() の後は割り込みが止まっておりフロッピー I/O がハングする。
 *    必ず TakeSystem の前に呼ぶこと。
 *  - AROS では 4KB ずつの読み込みにする (一括 Read はハングする)。
 *  - ファイルサイズは呼び出し側が知っている前提 (Seek を使わない)。
 */
void *file_load_fast(const char *name, u32 expected_size, u32 *size_out) {
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    if (!fh)
        return 0;

    u8 *buf = (u8 *)mem_alloc(expected_size, MEMF_FAST | MEMF_CLEAR);
    if (!buf) {
        Close(fh);
        return 0;
    }

    u32 done = 0;
    while (done < expected_size) {
        u32 chunk = expected_size - done;
        if (chunk > 4096)
            chunk = 4096;
        LONG got = Read(fh, buf + done, (LONG)chunk);
        if (got < 0) {
            Close(fh);
            mem_free(buf, expected_size);
            return 0;
        }
        if (got == 0)
            break;              /* EOF。要求より短いファイルは許容する */
        done += (u32)got;
    }
    Close(fh);

    if (size_out)
        *size_out = done;
    return buf;
}
