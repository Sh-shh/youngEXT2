#ifndef _TYPES_H_
#define _TYPES_H_

/**********************************************************
 * SECTION: Typedef
 **********************************************************/
typedef int          boolean;

typedef enum file_type {
    NEWFS_REG_FILE,       // 普通文件
    NEWFS_DIR,            // 目录文件
    NEWFS_SYM_LINK
} FILE_TYPE;

/**********************************************************
 * SECTION: Macro
 **********************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NEWFS_MAGIC_NUM         0x52415453       // 幻数，用于识别文件系统，可自行定义
#define NEWFS_SUPER_OFS         0               // 文件系统中超级块偏移量
#define NEWFS_ROOT_INO          0

#define MAX_NAME_LEN            128     
#define NEWFS_ERROR_NONE          0
#define NEWFS_ERROR_ACCESS        EACCES
#define NEWFS_ERROR_SEEK          ESPIPE     
#define NEWFS_ERROR_ISDIR         EISDIR
#define NEWFS_ERROR_NOSPACE       ENOSPC
#define NEWFS_ERROR_EXISTS        EEXIST
#define NEWFS_ERROR_NOTFOUND      ENOENT
#define NEWFS_ERROR_UNSUPPORTED   ENXIO
#define NEWFS_ERROR_IO            EIO     /* Error Input/Output */
#define NEWFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define MAX_FILE_NAME           128
#define NEWFS_DATA_PER_FILE       4
#define NEWFS_DEFAULT_PERM        0777

#define NEWFS_IOC_MAGIC           'S'
#define NEWFS_IOC_SEEK            _IO(NEWFS_IOC_MAGIC, 0)

#define NEWFS_FLAG_BUF_DIRTY      0x1
#define NEWFS_FLAG_BUF_OCCUPY     0x2


#define NEWFS_SUPER_BLKS          1     // 超级块
#define NEWFS_MAP_DATA_BLKS       1     // 最多只有4096个数据块，位图大小为 4096/8 = 512B，所以data位图只需要1块
#define NEWFS_MAP_INODE_BLKS      1     // 一个inode可对应一个或多个数据块，因此inode位图只需要1块
#define NEWFS_INODE_NUM           256
#define NEWFS_DATA_NUM            2048

/**********************************************************
 * SECTION: Macro Function
 **********************************************************/
#define NEWFS_IO_SZ()                     (newfs_super.sz_io)           // IO单位大小，512KB
#define NEWFS_DISK_SZ()                   (newfs_super.sz_disk)         // 磁盘大小，4MB
#define NEWFS_DRIVER()                    (newfs_super.fd)             
#define NEWFS_BLK_SZ()                    (newfs_super.sz_blk)     
#define NEWFS_INODE_SZ()                  (sizeof(struct newfs_inode_d))    // 一个inode_d的大小

#define NEWFS_ROUND_DOWN(value, round)    (value % round == 0 ? value : (value / round) * round)
#define NEWFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + 1) * round)

#define NEWFS_BLKS_SZ(blks)               (blks * NEWFS_BLK_SZ())
#define NEWFS_ASSIGN_FNAME(pnfs_dentry, _fname) memcpy(pnfs_dentry->name, _fname, strlen(_fname))

#define NEWFS_INO_OFS(ino)                (newfs_super.inode_offset + ino * NEWFS_BLK_SZ())
#define NEWFS_DATA_OFS(ino)               (newfs_super.data_offset + ino * NEWFS_BLK_SZ())

#define NEWFS_IS_DIR(pinode)              (pinode->dentry->ftype == NEWFS_DIR)
#define NEWFS_IS_FILE(pinode)              (pinode->dentry->ftype == NEWFS_REG_FILE)
// #define NEWFS_IS_SYM_LINK(pinode)         (pinode->dentry->ftype == SYM_LINK)


struct custom_options {
	const char*        device;
};

struct newfs_super {
    uint32_t magic;
    int      fd;
    /* TODO: Define yourself */
    int                max_ino;                     // 最多支持的文件数
    int                max_data;                    // 最大数据块数

    struct newfs_dentry*     root_dentry;           // 根目录dentry
    boolean            is_mounted;

    int                sz_io;                       // 512KB
    int                sz_disk;                     // 4MB
    int                sz_blk;                      // 1024KB
    int                sz_usage;                    // 用于存储ioctl相关信息

    uint8_t*           map_inode;                   // inode位图
    int                map_inode_blks;              // inode位图占用的块数
    int                map_inode_offset;            // inode位图在磁盘上的偏移
    
    uint8_t*           map_data;                    // data位图
    int                map_data_blks;               // data位图占用的块数
    int                map_data_offset;             // data位图在磁盘上的偏移

    int                inode_offset;          // 第一个索引节点在磁盘上的偏移
    int                data_offset;           // 第一个数据块在磁盘上的偏移


};

struct newfs_inode {
    uint32_t ino;                                               // 在inode位图中的下标
    /* TODO: Define yourself */
    int                 size;                                   // 文件已占用空间
    int                 dir_cnt;                                // 目录项数量
    struct newfs_dentry*dentry;                                 // 指向该inode的dentry
    struct newfs_dentry*dentrys;                                // 所有目录项  
    int                 block_pointer[NEWFS_DATA_PER_FILE];     // 数据块指针
};

struct newfs_dentry {
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    /* TODO: Define yourself */
    struct newfs_dentry*      parent;           // 父亲Inode的dentry
    struct newfs_dentry*      brother;          // 兄弟
    struct newfs_inode* inode;                  // 指向inode    
    FILE_TYPE           ftype;
};

static inline struct newfs_dentry* new_dentry(char * fname, FILE_TYPE ftype) {
    struct newfs_dentry * dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry)); /* dentry 在内存空间也是随机分配 */
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NEWFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;
    return dentry;                                            
}



/**********************************************************
 * SECTION: FS Specific Structure - Disk structure
 **********************************************************/
struct newfs_super_d {
    uint32_t           magic_num;                   // 幻数，用于识别文件系统
    int                sz_usage;

    int                map_inode_blks;              // inode位图占用的块数
    int                map_inode_offset;            // inode位图在磁盘上的偏移

    int                map_data_blks;               // data位图占用的块数
    int                map_data_offset;             // data位图在磁盘上的偏移

    int                inode_offset;                // 索引节点在磁盘上的偏移
    int                data_offset;                 // 数据块在磁盘上的偏移


};

struct newfs_inode_d {  //索引节点
    int                ino;                                 // 在inode位图中的下标
    int                size;                                // 文件已占用空间
    int                link;                                // 链接数
    FILE_TYPE          ftype;                               // 文件类型（目录类型、普通文件类型）
    int                dir_cnt;                             // 如果是目录类型文件，下面有几个目录项
    int                block_pointer[NEWFS_DATA_PER_FILE];  // 数据块指针（可固定分配）
};  

struct newfs_dentry_d  /*目录项*/
{
    char               fname[MAX_FILE_NAME];          // 指向的ino文件名
    FILE_TYPE          ftype;                         // 指向的ino文件类型
    int                ino;                           // 指向的ino号
};  




#endif /* _TYPES_H_ */