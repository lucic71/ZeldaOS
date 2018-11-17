/*
 * Copyright (c) 2018 Jie Zheng
 */
#include <lib/include/string.h>
#include <kernel/include/printk.h>
#include <filesystem/include/devfs.h>
#include <device/include/pseudo_terminal.h>

#define VIDEO_MEMORY_BASE 0xb8000

static uint16_t (*video_ptr)[PTTY_COLUMNS] = (void *)VIDEO_MEMORY_BASE;

int32_t
ptty_init(struct pseudo_terminal_master * ptm)
{
    memset(ptm, 0x0, sizeof(struct pseudo_terminal_master));
    return OK;
}

int32_t
ptty_enqueue_byte(struct pseudo_terminal_master * ptm, uint8_t value)
{
    int idx;
    int32_t last_row_index = ptm->row_index;
    if (value != '\n')
        ptm->buffer[ptm->row_index][ptm->col_index] = value;
    ptm->col_index++;
    if (ptm->col_index == PTTY_COLUMNS || value == '\n') {
        ptm->col_index = 0;
        ptm->row_index = (ptm->row_index + 1) % (PTTY_ROWS + 1);
        if (((ptm->row_index + 1) % (PTTY_ROWS + 1)) == ptm->row_front) {
            for (idx = 0; idx < PTTY_COLUMNS; idx++)
                ptm->buffer[ptm->row_front][idx] = 0;
            ptm->row_front = (ptm->row_front + 1) % (PTTY_ROWS + 1);
        }
    }
    ptm->need_scroll |= last_row_index != ptm->row_index; 
    return OK;
}

int32_t
ptty_flush_terminal(struct pseudo_terminal_master * ptm)
{
    int idx_row = 0;
    int idx_col = 0;
    int32_t video_row_idx = 0;
    if (!ptm->need_scroll) {
        video_row_idx = (ptm->row_index + PTTY_ROWS + 1 -
            ptm->row_front) % (PTTY_ROWS + 1);
        ASSERT(video_row_idx < PTTY_ROWS);
        for (idx_col = 0; idx_col < PTTY_COLUMNS; idx_col++)
            video_ptr[video_row_idx][idx_col] =
                (video_ptr[video_row_idx][idx_col] & 0xff00) |
                ptm->buffer[ptm->row_index][idx_col];
    } else {
        for (idx_row = ptm->row_front;
            idx_row != ((ptm->row_index + 1) % (PTTY_ROWS + 1));
            idx_row = (idx_row + 1) % (PTTY_ROWS + 1), video_row_idx++) {
            for (idx_col = 0; idx_col < PTTY_COLUMNS; idx_col++) {
                video_ptr[video_row_idx][idx_col] =
                    (video_ptr[video_row_idx][idx_col] & 0xff00) |
                    ptm->buffer[idx_row][idx_col];
            }
        }
    }
    ptm->need_scroll = 0;
    return OK;
}

static int32_t
ptty_dev_write(struct file * file, uint32_t offset, void * buffer, int size)
{
    int idx = 0;
    uint8_t * ptr = (uint8_t *)buffer;
    struct pseudo_terminal_master * ptm = file->priv;
    ASSERT(ptm);
    for (idx = 0; idx < size; idx++) {
        ptty_enqueue_byte(ptm, ptr[idx]);
    }
    ptty_flush_terminal(ptm);
    return size;
}
static struct file_operation ptty_dev_ops = {
        .size = NULL,
        .stat = NULL,
        .read = NULL,
        .write = ptty_dev_write,
        .truncate = NULL,
        .ioctl = NULL
};
struct pseudo_terminal_master ptties[MAX_TERMINALS];

void
ptty_post_init(void)
{
    int idx = 0;
    uint8_t node_name[64];
    struct file_system * devfs = get_dev_filesystem();
    for (idx = 0; idx < MAX_TERMINALS; idx++) {
        ptty_init(&ptties[idx]);
        memset(node_name, 0x0, sizeof(node_name));
        sprintf((char *)node_name, "/ptm%d", idx);
        ASSERT(register_dev_node(devfs,
            node_name,
            0x0,
            &ptty_dev_ops,
            &ptties[idx]));
    }
}
