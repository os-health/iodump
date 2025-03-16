/**********************************************************************************
 *                                                                                *
 *                    SPDX-License-Identifier:  GPL-2.0 OR MIT                    *
 *                        Copyright (c) 2022, Alibaba Group                       *
 *                                                                                *
 **********************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/relay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/interrupt.h>
#include <trace/events/block.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mount.h>
#include <linux/tracepoint.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/stacktrace.h>
#include <linux/nsproxy.h>
#include <linux/mnt_namespace.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
#include <linux/genhd.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
#include <linux/bsearch.h>
#endif
#include "kiodump.h"

#define OSHEALTH_MODULE "kiodump"
#define SUBBUF_SIZE     262144        // 4096 * 64
#define N_SUBBUFS       4
#define INFO_MAX        256
#define BUF_SIZE        32
#define MAX_ST_ENTRIES  64
#define BLK_RQ          10001
#define BLK_BIO         10002

#define ARRAY_NR(array) (sizeof((array))/sizeof((array)[0]))

enum opts_flag_bits {
	__OPTS_DATETIME,
	__OPTS_TIMESTAMP,
	__OPTS_COMM,
	__OPTS_PID,
	__OPTS_TID,
	__OPTS_IOSIZE,
	__OPTS_SECTOR,
	__OPTS_PARTITION,
	__OPTS_RWPRI,
	__OPTS_RWSEC,
	__OPTS_LAUNCHER,
	__OPTS_INO,
	__OPTS_FULLPATH,
};

#define OPTS_DATETIME      (1UL << __OPTS_DATETIME)
#define OPTS_TIMESTAMP     (1UL << __OPTS_TIMESTAMP)
#define OPTS_COMM          (1UL << __OPTS_COMM)
#define OPTS_PID           (1UL << __OPTS_PID)
#define OPTS_TID           (1UL << __OPTS_TID)
#define OPTS_IOSIZE        (1UL << __OPTS_IOSIZE)
#define OPTS_SECTOR        (1UL << __OPTS_SECTOR)
#define OPTS_PARTITION     (1UL << __OPTS_PARTITION)
#define OPTS_RWPRI         (1UL << __OPTS_RWPRI)
#define OPTS_RWSEC         (1UL << __OPTS_RWSEC)
#define OPTS_LAUNCHER      (1UL << __OPTS_LAUNCHER)
#define OPTS_INO           (1UL << __OPTS_INO)
#define OPTS_FULLPATH      (1UL << __OPTS_FULLPATH)

static int     suspended;
static int     tracepoint_flag;
static int     enable;
static int     step_sampling;
static int     step_sampling_replica;
static int     probe_point_type;
static int     probe_point_key;         // block_getrq
static int     filter_pid;
static int     filter_pid_replica;
static int     filter_comm_replica_len;
static int     match_comm_replica_len;
static int     lockpid;
static int     effective_partno;
static ulong   opts_flag;
static void   *probe_point_func;
static char    disk_partition[16];
static char    filter_comm[16];
static char    filter_comm_replica[16];
static char    match_comm[16];
static char    match_comm_replica[16];
static char    clean_trace_buf[BUF_SIZE];
static char   *probe_point_name;
static struct rchan  *chan;
static struct dentry *dir;
static struct dentry *clean_trace;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
static struct tracepoint *block_tracepoint;
#endif

static atomic64_t step_counter = ATOMIC_INIT(0);
static spinlock_t trace_lock   = __SPIN_LOCK_UNLOCKED();

const struct blocktrace blocktraces[] = {
	[66] = {BLK_BIO,     "block_bio_bounce"},           // B
	[67] = {BLK_RQ,      "block_rq_complete"},          // C
	[68] = {BLK_RQ,      "block_rq_issue"},             // D
	[70] = {BLK_BIO,     "block_bio_frontmerge"},       // F
	[71] = {BLK_BIO,     "block_getrq"},                // G
	[73] = {BLK_RQ,      "block_rq_insert"},            // I
	[77] = {BLK_BIO,     "block_bio_backmerge"},        // M
	[81] = {BLK_BIO,     "block_bio_queue"},            // Q
	[82] = {BLK_RQ,      "block_rq_requeue"},           // R
	[83] = {BLK_BIO,     "block_sleeprq"},              // S
	[88] = {BLK_BIO,     "block_split"},                // X
};

static const unsigned short int mon_yday[2][13] = {
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

// must sorted
static char *ignore_list[] = {"do_syscall_64", "entry_SYSCALL_64_after_hwframe",
	"entry_SYSCALL_64_after_swapgs", "entry_SYSCALL_64_fastpath", "system_call_fastpath"};

/***********************************************************************************
 **                                                                                *
 **                               comman part                                      *
 **                                                                                *
 ***********************************************************************************/

static inline bool __must_check IS_INVALID(__force const void *ptr)
{
	if ((unsigned long)ptr == 0xffffffff00000000)
		return 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)                // just for 2.6.32 etc.
	else if ((unsigned long)ptr < 0xffff000000000000)
		return 1;
#endif
	else
		return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      int (*cmp)(const void *key, const void *elt))
{
	size_t start = 0, end = num;
	int result;
	size_t mid;

	while (start < end) {
		mid = start + (end - start) / 2;

		result = cmp(key, base + mid * size);
		if (result < 0)
			end = mid;
		else if (result > 0)
			start = mid + 1;
		else
			return (void *)base + mid * size;
	}

	return NULL;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
char *fmt_datetime(time64_t tv_sec, unsigned long tv_usec, char *fmt_datetime)
{
	time64_t days;
	time64_t rem;
	time64_t current_year;
	time64_t guess_year;
	time64_t current_mon;
#else
char *fmt_datetime(time_t tv_sec, unsigned long tv_usec, char *fmt_datetime)
{
	time_t days;
	time_t rem;
	time_t current_year;
	time_t guess_year;
	time_t current_mon;
#endif
	const unsigned short int *yday;
	long offset = 28800;    // 60 * 60 * 8, East eight area
	struct tm2 tm;

	tv_sec += offset;
	days = tv_sec / 86400;
	rem  = tv_sec % 86400;

	tm.tm_hour = rem / 3600;
	rem %= 3600;
	tm.tm_min = rem / 60;
	tm.tm_sec = rem % 60;

	guess_year = 1970 + days / 365;
	days -=  ((guess_year - 1) - (1970 - 1)) * 365 + ((guess_year - 1) / 4 - (1970 - 1) / 4);
	current_year = guess_year;

	if (days < 0) {
		guess_year = current_year - 1;
		days +=  365 + ((current_year - 1) / 4 - (guess_year - 1) / 4);
		current_year = guess_year;
	}

	tm.tm_year = current_year;
	yday = mon_yday[current_year % 4 == 0];
	for (current_mon = 11; days < (long)yday[current_mon]; --current_mon)
		continue;
	days -= yday[current_mon];
	tm.tm_mon = current_mon + 1;
	tm.tm_mday = days + 1;

	sprintf(fmt_datetime, "%d-%02d-%02dT%02d:%02d:%02d.%06lu",
		tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tv_usec);

	return fmt_datetime;
}

/***********************************************************************************
 **                                                                                *
 **                                 ext4 part                                      *
 **                                                                                *
 ***********************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

int mnt_has_parent(struct mount *mnt)
{
	return mnt != mnt->mnt_parent;
}
#endif

static inline int __must_check is_invalid_dentry(struct dentry *dentry)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(dentry))
		ret = -EFAULT;
	else if (!virt_addr_valid(dentry))
		ret = -EFAULT;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)                  // just for 2.6.32 etc.
	else if (IS_INVALID(dentry))
		ret = -EFAULT;
#endif
	else if (dentry->d_name.len < 1)
		ret = -EFAULT;
	else if (dentry->d_name.len > (NAME_MAX-1))
		ret = -EFAULT;
	else if (IS_ERR_OR_NULL(dentry->d_name.name))
		ret = -EFAULT;

	return ret;
}

static int prepending(char **buffer, int *buflen, const char *str, int namelen)
{
	*buflen -= namelen;
	if (*buflen < 0)
		return -ENAMETOOLONG;
	*buffer -= namelen;
	memcpy(*buffer, str, namelen);

	return 0;
}

static int prepend_dname(char **buffer, int *buflen, struct dentry *dentry)
{
	int ret;

	ret = is_invalid_dentry(dentry);
	if (ret == 0)
		ret = prepending(buffer, buflen, dentry->d_name.name, dentry->d_name.len);

	return ret;
}

static char *dentry2filepath(struct dentry *dentry, struct vfsmount *vfsmnt, char *fullpath, int buflen)
{
	int            error;
	char          *end;
	char          *retval;
	struct dentry *parent;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	struct mount  *mnt;
#endif

	end = fullpath + buflen;
	prepending(&end, &buflen, "\0", 1);
	if (buflen < 1)
		goto Elong;
	retval = end - 1;      //  Get '/' right
	*retval = '/';

	while (1) {
		// while dentry.dname is /, IS_ROOT is true
		if (IS_ROOT(dentry)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
			mnt = real_mount(vfsmnt);
			if (!mnt_has_parent(mnt))
				break;
			vfsmnt = &(mnt->mnt_parent->mnt);
			parent = mnt->mnt_mountpoint;
#else
			if (vfsmnt->mnt_parent == vfsmnt)
				break;
			parent = vfsmnt->mnt_mountpoint;
			vfsmnt = vfsmnt->mnt_parent;
			if (is_invalid_dentry(parent))
				break;
#endif
		} else {
			prefetch(dentry);
			error = prepend_dname(&end, &buflen, dentry);
			if (error != 0 || prepending(&end, &buflen, "/", 1) != 0)
				goto Elong;
			retval = end;

			parent = dentry->d_parent;
			if (is_invalid_dentry(parent))
				break;
		}

		dentry = parent;
	}

	return retval;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}

void inode_first_dentry(struct inode *inode, struct dentry **dentry)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	struct hlist_node *the_d_alias;

	the_d_alias = inode->i_dentry.first;
#else
	struct list_head *the_d_alias;

	the_d_alias = inode->i_dentry.next;
#endif

	if (!IS_ERR_OR_NULL(the_d_alias))
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 1)
		*dentry = container_of(the_d_alias, struct dentry, d_u.d_alias);
#else
		*dentry = container_of(the_d_alias, struct dentry, d_alias);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
void inode2vfsmount(struct inode *inode, struct vfsmount **vfsmnt)
{
	struct list_head *pos;
	struct list_head *head;
	struct mount     *mnt;

	if (IS_ERR_OR_NULL(inode))
		return;
	head = &(inode->i_sb->s_mounts);
	if (IS_ERR_OR_NULL(head))
		return;
	for (pos = head->next; pos != head && !IS_ERR_OR_NULL(pos); pos = pos->next) {
		mnt = container_of(pos, struct mount, mnt_instance);
		if (!IS_ERR_OR_NULL(mnt)) {
			*vfsmnt = &(mnt->mnt);
			return;
		}
	}
}
#else
void dentry2vfsmount(struct dentry *dentry, struct vfsmount **vfsmnt)
{
	struct dentry        *sb_root;
	struct super_block   *sb;
	struct nsproxy       *nsp;
	struct mnt_namespace *ns;
	struct list_head     *head;
	struct vfsmount      *mnt = NULL;

	sb      = dentry->d_sb;
	if (IS_ERR_OR_NULL(sb))
		return;

	sb_root = sb->s_root;
	if (IS_ERR_OR_NULL(sb_root))
		return;

	nsp     = current->nsproxy;
	if (IS_ERR_OR_NULL(nsp))
		return;

	ns      = nsp->mnt_ns;
	if (IS_ERR_OR_NULL(ns))
		return;

	list_for_each(head, &ns->list) {
		mnt = list_entry(head, struct vfsmount, mnt_list);
		if (mnt->mnt_root == sb_root) {
			*vfsmnt = mnt;
			return;
		}
	}
}
#endif

static struct inode *bio2inode(struct bio *bio, struct inode **inode)
{
	struct page          *bv_page;
	struct address_space *addr_space;
	struct inode         *ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	struct iomap_dio     *iomap_dio;
	struct kiocb         *iocb;
	struct file          *filp;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0) \
	|| (DISTRIBUTION == DISTRIBUTION_CENTOS && RHEL_MAJOR == 6 && RHEL_MINOR >= 5)
	struct dio           *dio;
#endif

	*inode = 0;

	if (IS_ERR_OR_NULL(bio))
		goto end;
	if (!bio->bi_vcnt)
		bio = (struct bio *)bio->bi_private;   // bio chain
	if (IS_ERR_OR_NULL(bio) || (!virt_addr_valid(bio)))
		goto end;
	if (!bio->bi_vcnt)
		goto end;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (!bio->bi_iter.bi_sector)
#else
	if (!bio->bi_sector)
#endif
		goto end;

	if (IS_ERR_OR_NULL(bio->bi_io_vec) || (!virt_addr_valid(bio->bi_io_vec)))
		goto end;

	bv_page = (struct page *)(bio->bi_io_vec[0].bv_page);

	if (IS_ERR_OR_NULL(bv_page) || PageSlab(bv_page)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		|| folio_test_swapcache(page_folio(bv_page)))
#else
		|| PageSwapCache(bv_page))
#endif
		goto end;

	if (PageAnon(bv_page)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
		iomap_dio = (struct iomap_dio  *)bio->bi_private;
		if (IS_ERR_OR_NULL(iomap_dio) || (!virt_addr_valid(iomap_dio))) {
			goto end;
		}

		iocb = (struct kiocb *)iomap_dio->iocb;
		if (IS_ERR_OR_NULL(iocb) || (!virt_addr_valid(iocb))) {
			goto end;
		}

		filp = (struct file *)iocb->ki_filp;
		if (IS_ERR_OR_NULL(filp)) {
			goto end;
		}

		*inode = (struct inode *)filp->f_inode;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0) \
	|| (DISTRIBUTION == DISTRIBUTION_CENTOS && RHEL_MAJOR == 6 && RHEL_MINOR >= 5)
		dio = (struct dio *)bio->bi_private;
		if (!IS_ERR_OR_NULL(dio))
			*inode = (struct inode *)dio->inode;
#endif
	} else {
		addr_space = (struct address_space *)(bv_page->mapping);
		if (IS_ERR_OR_NULL(addr_space) || !virt_addr_valid(addr_space))
			goto end;

		*inode = addr_space->host;
	}

end:
	ret = *inode;

	return ret;
}

/***********************************************************************************
 **                                                                                *
 **                                  blk part                                      *
 **                                                                                *
 ***********************************************************************************/

//////////////////////////////////// block /////////////////////////////////

static void block_secondary_rw(char *rwbs, unsigned int op)
{
	int i = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)         // start:  secondary_rw
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)          // start:      FUA option
	if (op & REQ_FUA)
		rwbs[i++] = 'F';
#endif                                                     // end  :      FUA option
	if (op & REQ_RAHEAD)
		rwbs[i++] = 'A';
	if (op & REQ_SYNC)
		rwbs[i++] = 'S';
	if (op & REQ_META)
		rwbs[i++] = 'M';
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)          // start:      REQ_SECURE
	if (op & REQ_SECURE)
		rwbs[i++] = 'E';
#endif                                                     // end  :      REQ_SECURE
#else                                                      // else :  secondary_rw
	if (op & 1 << BIO_RW_AHEAD)
		rwbs[i++] = 'A';
	if (op & 1 << BIO_RW_SYNCIO)
		rwbs[i++] = 'S';
	if (op & 1 << BIO_RW_META)
		rwbs[i++] = 'M';
#endif                                                     // end  :  secondary_rw
	if (!i)
		rwbs[i++] = 'V';
	rwbs[i] = '\0';
}

static char blk_primary_rw(unsigned int op)
{
	char rwchar;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)          // start:  primary_rw
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)         // start:      switch
	switch (op & REQ_OP_MASK) {
#else                                                      // else :      switch
	switch (op) {
#endif                                                     // end  :      switch
	case REQ_OP_WRITE:
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)          // start:      WRITE_SAME
	case REQ_OP_WRITE_SAME:
#endif                                                     // end  :      WRITE_SAME
		rwchar = 'W';
		break;
	case REQ_OP_DISCARD:
		rwchar = 'D';
		break;
	case REQ_OP_SECURE_ERASE:
		rwchar = 'E';
		break;
	case REQ_OP_FLUSH:
		rwchar = 'F';
		break;
	case REQ_OP_READ:
		rwchar = 'R';
		break;
	default:
		rwchar = 'N';
	}
#else                                                      // else :  primary_rw
	if (op & WRITE)
		rwchar = 'W';
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)         // start:      DISCARD
	else if (op & REQ_DISCARD)
#else                                                      // else :      DISCARD
	else if (op & 1 << BIO_RW_DISCARD)
#endif                                                     // end  :      DISCARD
		rwchar = 'D';
	else
		rwchar = 'R';
#endif                                                     // end  :  primary_rw

	return rwchar;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
static int get_partno(struct block_device *bdev)
{
	int                  partno;
	struct gendisk      *gd;
	struct hd_struct    *hd;

	partno = 0;
	if (!bdev)
		return partno;     // 0

	hd = bdev->bd_part;
	if (hd)
		partno = hd->partno;

	if (partno)
		return partno;

	gd = bdev->bd_disk;
	if (!gd)
		return partno;	   // 0

	partno = gd->part0.partno;
	if (!partno)
		partno = MINOR(bdev->bd_dev) - gd->first_minor;

	return partno;
}
#endif

static int get_effective_partno(struct gendisk *gendisk, sector_t sector)
{
	int partno;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0) \
	 || (DISTRIBUTION == DISTRIBUTION_CENTOS && RHEL_MAJOR == 8 && RHEL_MINOR >= 4)
	partno = 0;                // To be perfect
#else
	struct hd_struct    *hd;

	partno = 0;
	hd = disk_map_sector_rcu(gendisk, sector);
	if (hd)
		partno = hd->partno;
#endif

	return partno;
}

static char *disk_fullname(struct gendisk *gd, u8 partno, char *dname)
{
	char lchar;

	if (!partno) {
		sprintf(dname, "%s", gd->disk_name);
		return dname;
	}
	lchar = gd->disk_name[strlen(gd->disk_name)-1];
	if (lchar >= '0' && lchar <= '9')
		sprintf(dname, "%sp%d", gd->disk_name, (int)partno);
	else
		sprintf(dname, "%s%d",  gd->disk_name, (int)partno);

	return dname;
}

int bsearch_key_strcmp(const void *arg1, const void *arg2)
{
	return strcmp(*(char **)arg1, *(char **)arg2);
}

static char *get_launcher(char *lname)
{
	int           ret;
	int           i;
	unsigned int  nr_entries;
	unsigned long entries[MAX_ST_ENTRIES] = {0, };
	char         *launcher_name;
	char        **result;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0) \
	|| (DISTRIBUTION == DISTRIBUTION_KYLIN && defined(CONFIG_ARCH_STACKWALK))
	nr_entries = stack_trace_save(entries, 32, 0);
#else
	struct stack_trace trace;
	trace.nr_entries  = 0;
	trace.max_entries = MAX_ST_ENTRIES;
	trace.entries     = entries;
	trace.skip        = 2;              // skip save_stack_trace self
	save_stack_trace(&trace);
	nr_entries        = trace.nr_entries;
#endif
	launcher_name = lname;
	if (nr_entries < 2) {
		ret = sprintf(lname, "%s", "unparsed");
		goto end_launcher;
	}
	for (i = nr_entries - 1; i > 0; i--) {
		ret = sprintf(lname, "%ps", (void *)entries[i-1]);
		result = (char **)bsearch(&lname, ignore_list, ARRAY_NR(ignore_list), sizeof(char *), bsearch_key_strcmp);
		if (result)
			continue;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
		if (strstr(lname, "__x64_sys_") == lname)
			launcher_name = lname + 10;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
		if (strstr(lname, "SyS_") == lname)
			launcher_name = lname + 4;
#else
		if (strstr(lname, "sys_") == lname)
			launcher_name = lname + 4;
#endif

		break;

	}

end_launcher:
	return launcher_name;
}

static void blk_trace_general(struct bio *bio, struct ioinfo_t *iit)
{
#if ((DISTRIBUTION == DISTRIBUTION_ALIOS && LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 93)) \
	|| LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
#else
	struct block_device   *bdev;
#endif
	struct dentry         *dentry = 0;
	struct vfsmount       *vfsmnt = 0;
	struct gendisk        *gendisk;
	struct inode          *inode;
	char                   partition[32] = {0,};
	char                  *chan_buf = NULL;
	char                  *path_buf = NULL;
	char                  *fullpath = NULL;
	int                    buflen = PATH_MAX;
	int                    partno;
	unsigned long          ino;
	int                    out_len = 0;
	char                  *launcher_name;
	char                   pri_rw;
	char                   sec_rw[8] = {0, };
	char                   lname[128] = {0, };
	char                   datetime_us[32] = {0, };
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
	struct timespec64      ts;
#else
	struct timespec        ts;
#endif
	unsigned long          tv_usec = 0;
	unsigned int           bio_count;

	inode     = 0;
	ino       = 0;
	bio_count = 0;
	if (!bio)
		return;

	partno = iit->partno;
#if ((DISTRIBUTION == DISTRIBUTION_ALIOS && LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 93)) \
	|| LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)     // start:  get_gendisk
	gendisk = bio->bi_disk;
#else                                                                                                           // else :  get_gendisk
	bdev    = bio->bi_bdev;
	if (!bdev)
		return;

	gendisk = bdev->bd_disk;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)                                                               // start:      get_partno
	if (!partno)
		partno  = get_partno(bdev);
#endif                                                                                                          // end  :      get_partno
#endif                                                                                                          // end  :  get_gendisk

	if (IS_ERR_OR_NULL(gendisk))
		return;

	if (!partno && effective_partno)
		partno = get_effective_partno(gendisk, iit->sector);

	disk_fullname(gendisk, partno, partition);
	if (!strstr(partition, disk_partition))
		return;

        if (step_sampling_replica > 1) {
		if (atomic64_read(&step_counter) > (LONG_MAX - 10))
			atomic64_set(&step_counter, 0);

		atomic64_inc(&step_counter);
		if (atomic64_read(&step_counter) % step_sampling_replica)
			return;
        }

	if ((opts_flag & OPTS_TIMESTAMP) || (opts_flag & OPTS_DATETIME)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
		ktime_get_real_ts64(&ts);
#else
		getnstimeofday(&ts);
#endif
		tv_usec = ts.tv_nsec/1000;
	}

	chan_buf = kzalloc(sizeof(char) * (PATH_MAX + INFO_MAX), GFP_ATOMIC|__GFP_NORETRY);
	if (!chan_buf) {
		relay_write(chan, "memorylack1\n", strlen("memorylack1\n"));
		goto free_mems1;
	}

	if (opts_flag & OPTS_DATETIME)
		sprintf(chan_buf, "%26s", fmt_datetime(ts.tv_sec, tv_usec, datetime_us));

	if (opts_flag & OPTS_TIMESTAMP)
		sprintf(chan_buf, "%s %10lu%06lu ", chan_buf, (unsigned long)ts.tv_sec, tv_usec);

	if (opts_flag & OPTS_COMM)
		sprintf(chan_buf, "%s %-15s", chan_buf, current->comm);

	if (opts_flag & OPTS_PID)
		sprintf(chan_buf, "%s %7d", chan_buf, current->tgid);

	if (opts_flag & OPTS_TID)
		sprintf(chan_buf, "%s %7d", chan_buf, current->pid);

	if (opts_flag & OPTS_IOSIZE)
		sprintf(chan_buf, "%s %6d", chan_buf, iit->iosize);

	if (opts_flag & OPTS_SECTOR)
		sprintf(chan_buf, "%s %11ld", chan_buf, (long)iit->sector);

	if (opts_flag & OPTS_PARTITION)
		sprintf(chan_buf, "%s %-9s", chan_buf, partition);

	if (opts_flag & OPTS_RWPRI) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
		pri_rw = blk_primary_rw(bio->bi_opf);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		pri_rw = blk_primary_rw(bio_op(bio));
#else
		pri_rw = blk_primary_rw(bio->bi_rw);
#endif
		sprintf(chan_buf, "%s %2c", chan_buf, pri_rw);
	}

	if (opts_flag & OPTS_RWSEC) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		block_secondary_rw(sec_rw, bio->bi_opf);
#else
		block_secondary_rw(sec_rw, bio->bi_rw);
#endif
		sprintf(chan_buf, "%s %5s", chan_buf, sec_rw);
	}

	if (opts_flag & OPTS_LAUNCHER) {
		launcher_name = get_launcher(lname);
		sprintf(chan_buf, "%s %-13s ", chan_buf, launcher_name);
	}

	if (opts_flag & OPTS_INO || opts_flag & OPTS_FULLPATH)
		bio2inode(bio, &inode);

	if (opts_flag & OPTS_INO) {
		if (!IS_ERR_OR_NULL(inode))
			ino = inode->i_ino;
		sprintf(chan_buf, "%s %9lu", chan_buf, ino);
	}

	if (!(opts_flag & OPTS_FULLPATH)) {
		out_len = snprintf(chan_buf, PATH_MAX+INFO_MAX, "%s\n", chan_buf);
		goto write_buf;
	}
	if (!IS_ERR_OR_NULL(inode)) {
		path_buf = kzalloc(sizeof(char) * PATH_MAX, GFP_ATOMIC | __GFP_NORETRY);
		//path_buf = __getname();
		if (!path_buf) {
			relay_write(chan, "memorylack2\n", strlen("memorylack2\n"));
			goto free_mems2;
		}
		inode_first_dentry(inode, &dentry);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
		inode2vfsmount(inode, &vfsmnt);
#else
		dentry2vfsmount(dentry, &vfsmnt);
#endif
		if (is_invalid_dentry(dentry)) {
			out_len = snprintf(chan_buf, PATH_MAX + INFO_MAX, "%s %s\n", chan_buf, "-");
			goto write_buf;
		}
		fullpath = dentry2filepath(dentry, vfsmnt, path_buf, buflen);
		if (IS_ERR_OR_NULL(fullpath)) {
			out_len = snprintf(chan_buf, PATH_MAX + INFO_MAX, "%s %s\n", chan_buf, "-unparsed1");
			goto write_buf;
		}
		out_len = snprintf(chan_buf, PATH_MAX+INFO_MAX, "%s %s\n", chan_buf, fullpath);
	} else {
		out_len = snprintf(chan_buf, PATH_MAX+INFO_MAX, "%s %s\n", chan_buf, "-");
	}

write_buf:
	if (chan)
		relay_write(chan, chan_buf, out_len);

free_mems2:
	kfree(path_buf);
	path_buf = NULL;
free_mems1:
	kfree(chan_buf);
	chan_buf = NULL;
}

//////////////////////////////////// trace point /////////////////////////////////

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
void find_tracepoint(struct tracepoint *tp, void *priv)
{
	if (!strcmp(tp->name, probe_point_name))
		block_tracepoint = tp;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
static void blk_trace_rq_handler(void *ignore, struct request *rq)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
static void blk_trace_rq_handler(void *ignore, struct request_queue *q, struct request *rq)
#else
static void blk_trace_rq_handler(struct request_queue *q, struct request *rq)
#endif
{
	struct ioinfo_t iit;
	struct bio *bio;

	if (filter_pid_replica && current->tgid != filter_pid_replica)
		return;

	if (filter_comm_replica_len && strcmp(current->comm, filter_comm_replica))
		return;

	if (match_comm_replica_len && !strstr(current->comm, match_comm_replica))
		return;

	if (IS_ERR_OR_NULL(rq))
		return;

	iit.iosize = rq->__data_len;
	if (!iit.iosize)
		return;

	iit.sector = rq->__sector;
	iit.partno = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	if (rq->part)
		iit.partno = bdev_partno(rq->part);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	if (rq->part)
		iit.partno = rq->part->bd_partno;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
	if (rq->part)
		iit.partno = rq->part->partno;
#endif
	bio        = rq->bio;

	iit.type   = BLK_RQ;
	blk_trace_general(bio, &iit);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
static void blk_trace_bio_handler(void *ignore, struct bio *bio)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
static void blk_trace_bio_handler(void *ignore, struct request_queue *q, struct bio *bio)
#else
static void blk_trace_bio_handler(struct request_queue *q, struct bio *bio)
#endif
{
	struct ioinfo_t iit;

	if (filter_pid_replica && current->tgid != filter_pid_replica)
		return;

	if (filter_comm_replica_len && strcmp(current->comm, filter_comm_replica))
		return;

	if (match_comm_replica_len && !strstr(current->comm, match_comm_replica))
		return;

	if (IS_ERR_OR_NULL(bio))
		return;

	iit.partno = 0;
#if ((DISTRIBUTION == DISTRIBUTION_ALIOS && LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 93)) \
	|| LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
	iit.partno = bio->bi_partno;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	iit.iosize = bio->bi_iter.bi_size;
	iit.sector = bio->bi_iter.bi_sector;
#else
	iit.iosize = bio->bi_size;
	iit.sector = bio->bi_sector;
#endif
	if (!iit.iosize)
		return;

	iit.type   = BLK_BIO;
	blk_trace_general(bio, &iit);
}

static int get_probe_point_name(int key, char **probe_point_name)
{
	int probe_point_type = -1;

	if (key >= (ARRAY_NR(blocktraces)))
		return probe_point_type;

	if (blocktraces[key].tracename != NULL) {
		probe_point_type = blocktraces[key].type;
		*probe_point_name = (char *)blocktraces[key].tracename;
	}

	return probe_point_type;
}

/***********************************************************************************
 **                                                                                *
 **                               initialize part                                  *
 **                                                                                *
 ***********************************************************************************/

//////////////////////////////////// debugfs clean trace /////////////////////////////////

static ssize_t clean_trace_read(struct file *file, char __user *ubuf, size_t count, loff_t *f_pos)
{
	return simple_read_from_buffer(ubuf, count, f_pos, clean_trace_buf, sizeof(clean_trace_buf));
}

static int clean_trace_release(struct inode *inode, struct file *file)
{
	if (!enable || strcmp(current->comm, "iodump"))
		return -1;

	spin_lock(&trace_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	if (block_tracepoint && probe_point_func)
		tracepoint_probe_unregister(block_tracepoint, probe_point_func, &tracepoint_flag);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	tracepoint_probe_unregister(probe_point_name, probe_point_func, &tracepoint_flag);
#else
	tracepoint_probe_unregister(probe_point_name, probe_point_func);
#endif
	tracepoint_synchronize_unregister();
	pr_info("tracepoint %s unregister success in debugfs.\n", probe_point_name);

	enable = 0;
	spin_unlock(&trace_lock);

	pr_err("close debugfs %d %s\n", current->pid, current->comm);

	return 0;
}

static const struct file_operations clean_trace_fops = {
	.owner   = THIS_MODULE,
	.release = clean_trace_release,
	.read    = clean_trace_read,
	//.write   = clean_trace_write,
};

//////////////////////////////////// relay channel /////////////////////////////////

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
struct dentry *create_buf_file_handler(const char *filename, struct dentry *parent, umode_t mode, struct rchan_buf *buf, int *is_global)
#else
struct dentry *create_buf_file_handler(const char *filename, struct dentry *parent,     int mode, struct rchan_buf *buf, int *is_global)
#endif
{
	struct dentry *file;

	file = debugfs_create_file(filename, mode, parent, buf, &relay_file_operations);

	return file;
}

int remove_buf_file_handler(struct dentry *dentry)
{
	debugfs_remove(dentry);

	return 0;
}

int subbuf_start_handler(struct rchan_buf *buf, void *subbuf, void *prev_subbuf, size_t prev_padding)
{
	if (relay_buf_full(buf)) {
		if (!suspended) {
			suspended = 1;
			pr_info("cpu %d buffer full.\n", smp_processor_id());
		}

		return 0;
	} else if (suspended) {
		suspended = 0;
		pr_info("cpu %d buffer no longer full.\n", smp_processor_id());
	}

	return 1;
}

struct rchan_callbacks relay_callbacks = {
	.subbuf_start    = subbuf_start_handler,
	.create_buf_file = create_buf_file_handler,
	.remove_buf_file = remove_buf_file_handler,
};

//////////////////////////////////// param handler /////////////////////////////////

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
int param_set_func(const char *val, const struct kernel_param *kp)
#else
int param_set_func(const char *val,       struct kernel_param *kp)
#endif
{
	int old_enable;
	int new_enable;
	int rv;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	int ret;
#endif

	old_enable = enable;
	rv = param_set_bool(val, kp);
	if (rv)
		return rv;

	new_enable = enable;

	pr_info("set_enable: oldvalue is %d, newvalue is %d\n", old_enable, new_enable);
	if (!old_enable && new_enable) {
		pr_info("start enable probe, probe_point_key is %d\n", probe_point_key);
		spin_lock(&trace_lock);

		// get tracepoint func
		if (probe_point_key == 'B' || probe_point_key == 'F'
			|| probe_point_key == 'M' || probe_point_key == 'S')
			effective_partno = 0;
		else
			effective_partno = 1;

		probe_point_type = get_probe_point_name(probe_point_key, &probe_point_name);
		if (probe_point_type == BLK_RQ) {
			probe_point_func = blk_trace_rq_handler;
		} else if (probe_point_type == BLK_BIO) {
			probe_point_func = blk_trace_bio_handler;
		} else {
			spin_unlock(&trace_lock);
			pr_err("probe_point_type is err.\n");
			return -ENODEV;
		}

		// get filter
		filter_pid_replica = filter_pid;
		if (!strcmp(filter_comm, "\x0a") || strlen(filter_comm) == 0) {
			filter_comm_replica_len = 0;
			pr_info("filter comm disabled\n");
		} else {
			filter_comm_replica_len = strlen(filter_comm);
			strncpy(filter_comm_replica, filter_comm, filter_comm_replica_len);
			filter_comm_replica[filter_comm_replica_len] = '\0';
			pr_info("filter comm enabled, %s\n", filter_comm_replica);
		}
		if (!strcmp(match_comm, "\x0a") || strlen(match_comm) == 0) {
			match_comm_replica_len = 0;
			pr_info("match comm disabled\n");
		} else {
			match_comm_replica_len = strlen(match_comm);
			strncpy(match_comm_replica, match_comm, match_comm_replica_len);
			match_comm_replica[match_comm_replica_len] = '\0';
			pr_info("match comm enabled, %s\n", match_comm_replica);
		}

                step_sampling_replica = step_sampling;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
		for_each_kernel_tracepoint(find_tracepoint, NULL);
		if (!block_tracepoint) {
			spin_unlock(&trace_lock);
			pr_err("block_tracepoint is err.\n");
			return -ENODEV;
		}
		ret = tracepoint_probe_register(block_tracepoint, probe_point_func, &tracepoint_flag);
		if (ret)
			tracepoint_probe_unregister(block_tracepoint, probe_point_func, &tracepoint_flag);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
		tracepoint_probe_register(probe_point_name, probe_point_func, &tracepoint_flag);
#else
		tracepoint_probe_register(probe_point_name, probe_point_func);
#endif
		spin_unlock(&trace_lock);

		pr_info("tracepoint %s register success.\n", probe_point_name);
	} else if (old_enable && !new_enable) {
		spin_lock(&trace_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
		if (block_tracepoint && probe_point_func)
			tracepoint_probe_unregister(block_tracepoint, probe_point_func, &tracepoint_flag);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
		tracepoint_probe_unregister(probe_point_name, probe_point_func, &tracepoint_flag);
#else
		tracepoint_probe_unregister(probe_point_name, probe_point_func);
#endif
		tracepoint_synchronize_unregister();
		spin_unlock(&trace_lock);

		pr_info("tracepoint %s unregister success.\n", probe_point_name);
	} else {
		pr_info("tracepoint do nothing.\n");
	}

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
struct kernel_param_ops the_param_ops_bool = {
	.set = param_set_func,
	.get = param_get_bool,
};
#endif

//////////////////////////////////// module initialize /////////////////////////////////

int __init oshealth_init(void)
{
	struct dentry           *health_dir;
	struct file_system_type *fs;
	struct super_block      *sb;
	char  *health_name = "os_health";

	tracepoint_flag         = 1;
	enable                  = 0;
	step_sampling           = 1;
	step_sampling_replica   = 1;
	probe_point_type        = -1;
	probe_point_key         = 71;         // block_getrq
	filter_pid              = 0;
	filter_pid_replica      = 0;
	filter_comm_replica_len = 0;
	match_comm_replica_len  = 0;
	lockpid                 = 0;
	effective_partno        = 0;
	opts_flag               = 8191;

	filter_comm[0]          = '\0';
	filter_comm_replica[0]  = '\0';
	match_comm[0]           = '\0';
	match_comm_replica[0]   = '\0';
	strcpy(disk_partition, "sda");
	probe_point_func        =  NULL;

	health_dir = debugfs_create_dir(health_name, NULL);
	if (IS_ERR_OR_NULL(health_dir)) {
		pr_err("create dir health_name failed.\n");
		fs = get_fs_type("debugfs");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
		sb = hlist_entry(fs->fs_supers.first, struct super_block, s_instances);
#else
		sb =  list_entry(fs->fs_supers.next,  struct super_block, s_instances);
#endif
		health_dir = lookup_one_len(health_name, sb->s_root, strlen(health_name));
		if (IS_ERR(health_dir)) {
			pr_err("os_health dir lookup failed.\n");
			goto init_err0;
		} else {
			pr_info("os_health dir lookup success.\n");
		}
	} else {
		pr_info("os_health dir create success.\n");
	}

	dir = debugfs_create_dir(OSHEALTH_MODULE, health_dir);
	if (IS_ERR_OR_NULL(dir)) {
		pr_err("create dir kiodump failed first time.\n");
		dir = lookup_one_len(OSHEALTH_MODULE, health_dir, strlen(OSHEALTH_MODULE));
		if (IS_ERR(dir)) {
			pr_err("create dir kiodump failed second time.\n");
			goto init_err1;
		}
	}
	pr_info("module dir create success.\n");

	clean_trace = debugfs_create_file("clean_trace", 0744, dir, NULL, &clean_trace_fops);
	if (IS_ERR_OR_NULL(clean_trace)) {
		pr_err("create file clean_trace failed, maybe memory is not enough.\n");
		goto init_err1;
	}
	pr_info("clean_trace file create success.\n");

	chan = relay_open("kiodump_trace", dir, SUBBUF_SIZE, N_SUBBUFS, &relay_callbacks, NULL);
	if (IS_ERR_OR_NULL(chan)) {
		pr_err("relay_open() failed, maybe memory is not enough.\n");
		goto init_err2;
	}
	pr_info("relay_open() success! %lu\n", OPTS_FULLPATH);

	return 0;

init_err2:
	relay_close(chan);
init_err1:
	debugfs_remove_recursive(dir);
init_err0:

	return -ENOMEM;
}

void __exit oshealth_exit(void)
{
	int ret;

	if (!enable)
		goto umount;

	spin_lock(&trace_lock);

	enable = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	if (block_tracepoint) {
		ret = tracepoint_probe_unregister(block_tracepoint, probe_point_func, &tracepoint_flag);
		if (ret)
			pr_err("tracepoint %s unregister is fail, errcode is %d .\n", probe_point_name, ret);
	}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	ret = tracepoint_probe_unregister(probe_point_name, probe_point_func, &tracepoint_flag);
	if (ret)
		pr_err("tracepoint %s unregister is fail, errcode is %d .\n", probe_point_name, ret);
#else
	ret = tracepoint_probe_unregister(probe_point_name, probe_point_func);
	if (ret)
		pr_err("tracepoint %s unregister is fail, errcode is %d .\n", probe_point_name, ret);
#endif
	tracepoint_synchronize_unregister();

	spin_unlock(&trace_lock);
	pr_info("tracepoint %s unregister success.\n", probe_point_name);

umount:
	if (chan) {
		relay_close(chan);
		pr_info("relayfs remove success!\n");
	}
	chan = NULL;

	debugfs_remove_recursive(dir);
	dir = NULL;
	pr_info("debugfs remove success!\n");
}
module_init(oshealth_init);
module_exit(oshealth_exit);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
module_param_cb(enable, &the_param_ops_bool, &enable, 0644);
#else
module_param_call(enable, param_set_func, param_get_bool, &enable, 0644);
#endif
MODULE_PARM_DESC(enable, "enable oshealth");
module_param_string(disk_partition, disk_partition, 16, 0644);
MODULE_PARM_DESC(disk_partition, "switch partition");
module_param_string(match_comm, match_comm, 16, 0644);
MODULE_PARM_DESC(match_comm, "match comm");
module_param_string(filter_comm, filter_comm, 16, 0644);
MODULE_PARM_DESC(filter_comm, "filter comm");
module_param(filter_pid, int, 0644);
MODULE_PARM_DESC(filter_pid, "filter pid");
module_param(lockpid, int, 0644);
MODULE_PARM_DESC(lockpid, "lock pid");
module_param(probe_point_key, int, 0644);
MODULE_PARM_DESC(probe_point_key, "probe point key");
module_param(step_sampling, int, 0644);
MODULE_PARM_DESC(step_sampling, "step sampling");
module_param(opts_flag, ulong, 0644);
MODULE_PARM_DESC(opts_flag, "output format");
MODULE_AUTHOR("Miles Wen");
MODULE_DESCRIPTION("one of oshealth module about io details");
MODULE_LICENSE("Dual MIT/GPL");
