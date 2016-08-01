/*
 * Copyright (C) Qiaojun Xiao
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);

/*
 * 创建内存池
 */
ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
  ngx_pool_t  *p;

  p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
  if (p == NULL) {
    return NULL;
  }

  p->d.last = (u_char *) p + sizeof(ngx_pool_t);
  p->d.end = (u_char *) p + size;
  p->d.next = NULL;
  p->d.failed = 0;

  size = size - sizeof(ngx_pool_t);
  p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

  p->current = p;
  p->chain = NULL;
  p->large = NULL;
  p->cleanup = NULL;
  p->log = log;

  return p;
}

/* 销毁池 */
void
ngx_destroy_pool(ngx_pool_t *pool)
{
  ngx_pool_t          *p, *n;
  ngx_pool_large_t    *l;
  ngx_pool_cleanup_t  *c;
  /* 清理需要额外清理的资源 */
  for (c = pool->cleanup; c; c = c->next) {
    if (c->handler) {
      ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
		     "run cleanup: %p", c);
      c->handler(c->data);
    }
  }
  /* 释放所有大内存块 */
  for (l = pool->large; l; l = l->next) {

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);

    if (l->alloc) {
      ngx_free(l->alloc);
    }
  }

#if (NGX_DEBUG)

  /*
   * we could allocate the pool->log from this pool
   * so we cannot use this log while free()ing the pool
   */

  for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
		   "free: %p, unused: %uz", p, p->d.end - p->d.last);

    if (n == NULL) {
      break;
    }
  }

#endif
  
  /* 释放所有预分配的内存块 */
  for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
    ngx_free(p);

    if (n == NULL) {
      break;
    }
  }
}

/*
 * 释放内存池中所有大内存块
 * 回收内存池中所有预分配的内存
 */
void
ngx_reset_pool(ngx_pool_t *pool)
{
  ngx_pool_t        *p;
  ngx_pool_large_t  *l;

  for (l = pool->large; l; l = l->next) {
    if (l->alloc) {
      ngx_free(l->alloc);
    }
  }

  for (p = pool; p; p = p->d.next) {
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    p->d.failed = 0;
  }

  pool->current = pool;
  pool->chain = NULL;
  pool->large = NULL;
}

/*
 * 从内存池pool中分配size大小的空间，返回空间地址
 */
void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
  u_char      *m;
  ngx_pool_t  *p;

  if (size <= pool->max) {

    p = pool->current;

    do {
      m = ngx_align_ptr(p->d.last, NGX_ALIGNMENT);
      /* pool->current 维护的链表中找到有足够空间ngx_pool_data_t，就从 ngx_pool_data_t中分配*/
      if ((size_t) (p->d.end - m) >= size) {
	p->d.last = m + size;

	return m;
      }

      p = p->d.next;

    } while (p);
    /* 没有找到合适的ngx_pool_data_t,则新分配一个 ngx_pool_data_t */
    return ngx_palloc_block(pool, size);
  }
  /* szie 大于内存池能分配的最大值，新分配空间并将信息填入pool->large */
  return ngx_palloc_large(pool, size);
}

/*
 * 从内存池pool中分配size大小的空间，返回空间地址
 */
void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
  u_char      *m;
  ngx_pool_t  *p;

  if (size <= pool->max) {

    p = pool->current;

    do {
      m = p->d.last;

      if ((size_t) (p->d.end - m) >= size) {
	p->d.last = m + size;

	return m;
      }

      p = p->d.next;

    } while (p);

    return ngx_palloc_block(pool, size);
  }

  return ngx_palloc_large(pool, size);
}

/*
 * 在内存池中新增一个ngx_pool_data_t 并在其中分配 size 大小空间
 * 返回空间地址
 * 将之前的每个ngx_pool_data_t 的失败计数加1
 * poo->current 跳过所有失败次数超过3次的 ngx_pool_data_t
 */
static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
  u_char      *m;
  size_t       psize;
  ngx_pool_t  *p, *new;

  psize = (size_t) (pool->d.end - (u_char *) pool);

  m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
  if (m == NULL) {
    return NULL;
  }

  new = (ngx_pool_t *) m;

  new->d.end = m + psize;
  new->d.next = NULL;
  new->d.failed = 0;

  m += sizeof(ngx_pool_data_t);
  m = ngx_align_ptr(m, NGX_ALIGNMENT);
  new->d.last = m + size;

  for (p = pool->current; p->d.next; p = p->d.next) {
    if (p->d.failed++ > 4) {
      pool->current = p->d.next;
    }
  }

  p->d.next = new;

  return m;
}

/*
 * 分配大内存块
 * 将分配的内存块连接到pool->large中
 */
static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
  void              *p;
  ngx_uint_t         n;
  ngx_pool_large_t  *large;

  p = ngx_alloc(size, pool->log);
  if (p == NULL) {
    return NULL;
  }

  n = 0;
  /* 在 pool->large 链表前三个节点中查找空闲节点，未找到则新分配一个节点插到链表头部 */
  for (large = pool->large; large; large = large->next) {
    if (large->alloc == NULL) {
      large->alloc = p;
      return p;
    }

    if (n++ > 3) {
      break;
    }
  }

  large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
  if (large == NULL) {
    ngx_free(p);
    return NULL;
  }

  large->alloc = p;
  large->next = pool->large;
  pool->large = large;

  return p;
}

/*
 * 分配按alignment对齐的大内存块，并新建一个ngx_pool_large_t 插入到pool->large链表的头部
 */
void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
  void              *p;
  ngx_pool_large_t  *large;

  p = ngx_memalign(alignment, size, pool->log);
  if (p == NULL) {
    return NULL;
  }

  large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
  if (large == NULL) {
    ngx_free(p);
    return NULL;
  }

  large->alloc = p;
  large->next = pool->large;
  pool->large = large;

  return p;
}

/*
 * 释放大内存块p
 */
ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
  ngx_pool_large_t  *l;

  for (l = pool->large; l; l = l->next) {
    if (p == l->alloc) {
      ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
		     "free: %p", l->alloc);
      ngx_free(l->alloc);
      l->alloc = NULL;

      return NGX_OK;
    }
  }

  return NGX_DECLINED;
}

/*
 * 从内存池中分配size大小空间，并填0
 */
void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
  void *p;

  p = ngx_palloc(pool, size);
  if (p) {
    ngx_memzero(p, size);
  }

  return p;
}

/*
 * 新建一个ngx_pool_cleanup_t 节点并插入到pool->cleanup链表
 * ngx_pool_cleanup_t->data指向从内存池中分配的size大小的空间
 */
ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
  ngx_pool_cleanup_t  *c;

  c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
  if (c == NULL) {
    return NULL;
  }

  if (size) {
    c->data = ngx_palloc(p, size);
    if (c->data == NULL) {
      return NULL;
    }

  } else {
    c->data = NULL;
  }

  c->handler = NULL;
  c->next = p->cleanup;

  p->cleanup = c;

  ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

  return c;
}

/*
 * 从内存池p->cleanup找到要清理的文件进行清理
 */
void
ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
  ngx_pool_cleanup_t       *c;
  ngx_pool_cleanup_file_t  *cf;

  for (c = p->cleanup; c; c = c->next) {
    if (c->handler == ngx_pool_cleanup_file) {

      cf = c->data;

      if (cf->fd == fd) {
	c->handler(cf);
	c->handler = NULL;
	return;
      }
    }
  }
}

/*
 * 关闭文件
 */
void
ngx_pool_cleanup_file(void *data)
{
  ngx_pool_cleanup_file_t  *c = data;

  ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
		 c->fd);

  if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
    ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
		  ngx_close_file_n " \"%s\" failed", c->name);
  }
}

/*
 * 删除文件
 */
void
ngx_pool_delete_file(void *data)
{
  ngx_pool_cleanup_file_t  *c = data;

  ngx_err_t  err;

  ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
		 c->fd, c->name);

  if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
    err = ngx_errno;

    if (err != NGX_ENOENT) {
      ngx_log_error(NGX_LOG_CRIT, c->log, err,
		    ngx_delete_file_n " \"%s\" failed", c->name);
    }
  }

  if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
    ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
		  ngx_close_file_n " \"%s\" failed", c->name);
  }
}


#if 0

static void *
ngx_get_cached_block(size_t size)
{
  void                     *p;
  ngx_cached_block_slot_t  *slot;

  if (ngx_cycle->cache == NULL) {
    return NULL;
  }

  slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

  slot->tries++;

  if (slot->number) {
    p = slot->block;
    slot->block = slot->block->next;
    slot->number--;
    return p;
  }

  return NULL;
}

#endif
