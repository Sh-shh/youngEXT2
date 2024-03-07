#include "../include/newfs.h"

extern struct newfs_super      newfs_super; 
extern struct custom_options   newfs_options;

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    // 查找字符在指定字符串中从右面开始的第一次出现的位置
    // 如果成功，返回该字符以及其后面的字符
    // 如果失败，则返回 NULL
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int newfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(DRIVER(), cur, IO_SZ());
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);//复制
    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 为一个inode分配dentry的bro，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return NEWfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor; 
    int bit_cursor; 
    int ino_cursor   = 0;
    boolean is_find_free_entry = FALSE;
    boolean is_findall_data_blks = FALSE;
    int data_blk_cnt = 0;
    int bp_cursor   = 0;

    // 从索引节点位图中取空闲inode
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks); 
         byte_cursor++)
    {
        // 限制了一个块中inode位图只占8位
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor); // inode位图占位
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }
    //无空闲inode
    if (!is_find_free_entry || ino_cursor == newfs_super.max_ino)
        return -NEWFS_ERROR_NOSPACE;

    // 先分配一个内存 inode
    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;

    // 再从数据位图中取空闲，要取出的数据块数量为每个inode中指向对应数据块的指针的数量
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks); 
         byte_cursor++)
    {   
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前data_block位置空闲 */
                newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor); // 数据块位图占位
                // 将找到的数据块号记入inode中，并判断是否找完inode对应的所有的数据块
                if(data_blk_cnt >= (NEWFS_DATA_PER_FILE -1)){
                    is_findall_data_blks = TRUE;
                    break;
                } else{
                    inode->block_pointer[data_blk_cnt] = bp_cursor;
                    data_blk_cnt++;
                }
            }
            bp_cursor++;
        }

        // printf("map_data: %d", &newfs_super.map_data[byte_cursor]);

        if (is_findall_data_blks) {
            break;
        }
    }

    if (!is_findall_data_blks || bp_cursor == newfs_super.max_data)
        return -NEWFS_ERROR_NOSPACE;


    dentry->inode = inode;
    dentry->ino   = inode->ino;
    
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    

    if (NEWFS_IS_FILE(inode)) {
        int bp_cnt = 0;
        for(bp_cnt = 0; bp_cnt < NEWFS_DATA_PER_FILE; bp_cnt++){
            inode->block_pointer[bp_cnt] = (uint8_t *)malloc(NEWFS_BLK_SZ());
        }
    }

    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    // 将数据块指针的值刷回磁盘
    for(int blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++)
        inode_d.block_pointer[blk_cnt] = inode->block_pointer[blk_cnt]; 

    int offset, offset_pmax;
    
    // 先将当前inode刷回磁盘
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }
                                                      /* Cycle 1: 写 INODE */
                                                      /* Cycle 2: 写 数据 */
    if (NEWFS_IS_DIR(inode)) {      
        int blk_cnt = 0;                    
        dentry_cursor = inode->dentrys;

        while(blk_cnt < NEWFS_DATA_PER_FILE){
            offset = NEWFS_DATA_OFS(inode->block_pointer[blk_cnt]);
            offset_pmax = NEWFS_DATA_OFS(inode->block_pointer[blk_cnt] + 1);
            while (dentry_cursor != NULL)
            {
                memcpy(dentry_d.fname, dentry_cursor->name, MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                //写回磁盘
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return -NEWFS_ERROR_IO;                     
                }
                // 将子目录项指向的inode递归刷回磁盘
                if (dentry_cursor->inode != NULL) {
                    newfs_sync_inode(dentry_cursor->inode);
                }
                // 指向下一个目录项继续刷回
                dentry_cursor = dentry_cursor->brother;
                if(offset + sizeof(struct newfs_dentry_d) > offset_pmax){
                    break;
                }
                offset += sizeof(struct newfs_dentry_d);
            }
            blk_cnt++;
        }
    } else if (NEWFS_IS_FILE(inode)) { // 对于文件类型的文件，直接将数据刷回磁盘
        // 将指针数组对应的数据块一一刷回
        for(int blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++){
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->block_pointer[blk_cnt]), 
                    (uint8_t *)&inode->block_pointer[blk_cnt], NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
        }
    }
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 该函数实现的功能是读取dentry指向的编号为ino的索引节点
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;       // 介质inode(驱动读取)
    struct newfs_dentry* sub_dentry;    // 子目录项的中间变量
    struct newfs_dentry_d dentry_d;     // 介质dentry(驱动读取)
    int    blk_cnt = 0;
    int    dir_cnt = 0;

    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for(blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++)
        inode->block_pointer[blk_cnt] = inode_d.block_pointer[blk_cnt];
    
   
    if (NEWFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;//目录项数目
        for (int i = 0; i < dir_cnt; i++)
        {
            if (newfs_driver_read(NEWFS_DATA_OFS(ino) + i * sizeof(struct newfs_dentry_d), 
                                (uint8_t *)&dentry_d, 
                                sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            newfs_alloc_dentry(inode, sub_dentry);
        }
    }
    else if (NEWFS_IS_FILE(inode)) {
        for(blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++){
            inode->block_pointer[blk_cnt] = (uint8_t *)malloc(NEWFS_BLK_SZ()); // 只分配一个块
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->block_pointer[blk_cnt]), inode->block_pointer[blk_cnt], 
                                NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
        }
    }
    return inode;
}
/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}
/**
 * @brief 
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct newfs_inode* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);//复制地址

    if (total_lvl == 0) {
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }

    fname = strtok(path_cpy, "/"); //分解字符串
    while (fname)
    {
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NEWFS_IS_FILE(inode) && lvl < total_lvl) {
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }

        if (NEWFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0) {//比较
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;     // 遍历子目录
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}
/**
 * @brief 挂载sfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map| Data Map | Data |
 * 
 * BLK_SZ = 2 * IO_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    int                     ret = NEWFS_ERROR_NONE;
    int                     driver_fd;
    struct newfs_super_d    newfs_super_d; 
    struct newfs_dentry*    root_dentry;
    struct newfs_inode*     root_inode;

    int                     inode_num ;
    int                     map_inode_blks;
    int                     data_num;
    int                     map_data_blks;

    int                     super_blks;
    boolean                 is_init = FALSE;

    newfs_super.is_mounted = FALSE;

    // driver_fd = open(options.device, O_RDWR);
     driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    newfs_super.fd = driver_fd;
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &newfs_super.sz_disk); //磁盘大小
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);  //IO单位大小

    // 数据块大小为两个IO单位（1024KB）
    newfs_super.sz_blk = 2 * newfs_super.sz_io;
    

    root_dentry = new_dentry("/", NEWFS_DIR);


    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d), 
                        sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }   

                                                         /* 读取super */
    if (newfs_super_d.magic_num != NEWFS_MAGIC_NUM) {       /* 幻数无 */

                                    
        super_blks = NEWFS_SUPER_BLKS;
        map_inode_blks = NEWFS_MAP_INODE_BLKS;
        map_data_blks = NEWFS_MAP_DATA_BLKS;
        inode_num  = NEWFS_INODE_NUM;
        data_num = NEWFS_DATA_NUM;

                                                      /* 布局layout */
        newfs_super.max_ino = inode_num;
        newfs_super.max_data = data_num; 

        // inode位图和数据位图偏移
        newfs_super_d.map_inode_offset = NEWFS_SUPER_OFS + NEWFS_BLKS_SZ(super_blks);
        newfs_super_d.map_data_offset = newfs_super_d.map_inode_offset + NEWFS_BLKS_SZ(map_inode_blks);

        // inode和数据块
        newfs_super_d.inode_offset = newfs_super_d.map_data_offset + NEWFS_BLKS_SZ(map_data_blks);
        newfs_super_d.data_offset = newfs_super_d.inode_offset + NEWFS_BLKS_SZ(NEWFS_ROUND_UP(
                                    inode_num * NEWFS_INODE_SZ(), NEWFS_BLK_SZ()) / NEWFS_BLK_SZ());

        // inode位图、数据位图所占块大小
        newfs_super_d.map_inode_blks  = map_inode_blks;
        newfs_super_d.map_data_blks = map_data_blks;

        newfs_super_d.magic_num = NEWFS_MAGIC_NUM;
        newfs_super_d.sz_usage    = 0;
        // NEWFS_DBG("inode map blocks: %d\n", map_inode_blks);
        is_init = TRUE;
    }
    newfs_super.sz_usage   = newfs_super_d.sz_usage;      /* 建立 in-memory 结构 */
    
    newfs_super.map_inode = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks));
    newfs_super.map_inode_blks = newfs_super_d.map_inode_blks;
    newfs_super.map_inode_offset = newfs_super_d.map_inode_offset;

    //数据块位图的in-memory结构
    newfs_super.map_data = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_data_blks));
    newfs_super.map_data_blks = newfs_super_d.map_data_blks;
    newfs_super.map_data_offset = newfs_super_d.map_data_offset;

    newfs_super.data_offset = newfs_super_d.data_offset;

    // 读取索引节点位图
    if (newfs_driver_read(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), 
                        NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    // 读取数据块位图
    if (newfs_driver_read(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), 
                        NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (is_init) {                                    /* 分配根节点 */
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }
    
    root_inode            = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted  = TRUE;

    return ret;
}

/**
 * @brief 
 * 
 * @return int 
 */
int newfs_umount() {
    struct newfs_super_d  newfs_super_d; 

    if (!newfs_super.is_mounted) {
        return NEWFS_ERROR_NONE;
    }

    newfs_sync_inode(newfs_super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                
    newfs_super_d.magic_num           = NEWFS_MAGIC_NUM;
    newfs_super_d.sz_usage            = newfs_super.sz_usage;

    newfs_super_d.map_inode_blks      = newfs_super.map_inode_blks;
    newfs_super_d.map_inode_offset    = newfs_super.map_inode_offset;
    newfs_super_d.map_data_blks       = newfs_super.map_data_blks;
    newfs_super_d.map_data_offset     = newfs_super.map_data_offset;

    newfs_super_d.inode_offset        = newfs_super.inode_offset;
    newfs_super_d.data_offset         = newfs_super.data_offset;

    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d, 
                     sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (newfs_driver_write(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), 
                         NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (newfs_driver_write(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), 
                         NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    free(newfs_super.map_inode);
    free(newfs_super.map_data);
    ddriver_close(NEWFS_DRIVER());

    return NEWFS_ERROR_NONE;
}

