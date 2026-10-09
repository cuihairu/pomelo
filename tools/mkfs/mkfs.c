/* mkfs: a plain Linux program that lays out a pomelo disk image.
 * Same format as servers/fs/format.h (kept as a tiny local copy: host
 * tools do not share code with the kernel by design). */

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SECTOR 512
#define FS_MAGIC 0x31504D50u
#define NINODES 32
#define INODE_START 1
#define INODE_SECTORS 4
#define DATA_START 5
#define NAMELEN 12
#define NDIRECT 6
#define MAXFILE (NDIRECT * SECTOR)
#define NDATA 1024

struct superblock {
    uint32_t magic, n_inodes, data_start, n_data;
};

struct inode {
    uint32_t used;
    char name[NAMELEN];
    uint32_t size;
    uint32_t direct[NDIRECT];
    uint8_t pad[64 - 4 - NAMELEN - 4 - NDIRECT * 4];
};

static uint8_t disk[(DATA_START + NDATA) * SECTOR];

static void put_sector(uint32_t lba, const void *buf) {
    memcpy(disk + lba * SECTOR, buf, SECTOR);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: mkfs <image> <seed-dir>\n");
        return 1;
    }

    struct superblock sb = { FS_MAGIC, NINODES, DATA_START, NDATA };
    memcpy(disk, &sb, sizeof sb);

    static struct inode inodes[NINODES];
    uint32_t next_block = 0;

    DIR *d = opendir(argv[2]);
    if (!d) { perror(argv[2]); return 1; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        char path[512];
        snprintf(path, sizeof path, "%s/%s", argv[2], e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size > MAXFILE) size = MAXFILE;

        int slot = -1;
        for (int i = 0; i < NINODES; i++)
            if (!inodes[i].used) { slot = i; break; }
        if (slot < 0) { fprintf(stderr, "mkfs: too many files\n"); break; }

        inodes[slot].used = 1;
        snprintf(inodes[slot].name, sizeof inodes[slot].name, "%.11s", e->d_name);
        inodes[slot].size = (uint32_t)size;
        for (int b = 0; b * SECTOR < size; b++) {
            fseek(f, b * SECTOR, SEEK_SET);
            uint8_t buf[SECTOR] = {0};
            if (fread(buf, 1, SECTOR, f) == 0) break;
            uint32_t lba = DATA_START + next_block++;
            put_sector(lba, buf);
            inodes[slot].direct[b] = lba;
        }
        fclose(f);
        printf("seeded /%s (%ld bytes)\n", e->d_name, size);
    }
    closedir(d);

    for (int s = 0; s < INODE_SECTORS; s++)
        put_sector(INODE_START + s, (uint8_t *)inodes + s * SECTOR);

    FILE *out = fopen(argv[1], "wb");
    if (!out) { perror(argv[1]); return 1; }
    fwrite(disk, 1, sizeof disk, out);
    fclose(out);
    printf("wrote %s (%zu sectors)\n", argv[1], sizeof disk / SECTOR);
    return 0;
}
