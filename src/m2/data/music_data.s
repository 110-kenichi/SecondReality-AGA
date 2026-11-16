/* 音楽データ (tools/s3m_convert.py が生成した .srm) を実行ファイルに埋め込む */
    .section .rodata
    .align 2
    .globl music0_data
music0_data:
    .incbin "data/music0.srm"
    .globl music0_end
music0_end:
