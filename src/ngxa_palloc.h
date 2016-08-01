/*
 * Copyright (C) Qiaojun Xiao
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)

#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
  ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
	    NGX_POOL_ALIGNMENT)


typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;
/* 内存中有清理函数的内存块节点 */
struct ngx_pool_cleanup_s {
  ngx_pool_cleanup_pt   handler;      /* 内存块的清理函数 */
  void                 *data;         /* 指向从内存池中分配的空间 */
  ngx_pool_cleanup_t   *next;         /* 指向下一节点 */
};


typedef struct ngx_pool_large_s  ngx_pool_large_t;

/*
 * 内存池中大内存块节点
 * 该节点维护的内存没有进行预分配而是在申请的时候分配的
 */
struct ngx_pool_large_s {
  ngx_pool_large_t     *next;              /* 指向下一个节点 */
  void                 *alloc;             /* 指向分配的内存空间 */
};

/* 内存池中预分配的内存节点 */
typedef struct {
  u_char               *last;              /* 指向可用空间起始位置 */
  u_char               *end;               /* 指向可用空间结束位置 */
  ngx_pool_t           *next;              /* 指向下一个 ngx_pool_data_t 节点 */
  ngx_uint_t            failed;            /* 空间申请失败次数 */
} ngx_pool_data_t;


struct ngx_pool_s {
  ngx_pool_data_t       d;                 /*  */
  size_t                max;               /* 内存池能分配的最大内存大小*/
  ngx_pool_t           *current;           /* 指向 ngx_pool_data_t 链表中第一个失败计数没有超过3的节点*/
  ngx_chain_t          *chain;
  ngx_pool_large_t     *large;             /* 维护一个 ngx_pool_large_t 的链表 */
  ngx_pool_cleanup_t   *cleanup;           /* 指向可清理的内存块 */
  ngx_log_t            *log;               /* 日志对象 */
};

/* 内存池中清理文件结构 */
typedef struct {
  ngx_fd_t              fd;                /* 文件描述符 */
  u_char               *name;              /* 文件名 */
  ngx_log_t            *log;               /* 日志对象 */
} ngx_pool_cleanup_file_t;


void *ngx_alloc(size_t size, ngx_log_t *log);
void *ngx_calloc(size_t size, ngx_log_t *log);

ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);
void ngx_reset_pool(ngx_pool_t *pool);

void *ngx_palloc(ngx_pool_t *pool, size_t size);
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
void ngx_pool_cleanup_file(void *data);
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
